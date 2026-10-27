"""Developer tool (needs internet): accuracy and speed check on real Sentinel-2 scenes.

Reference: ESA WorldCover 2021 v200 (10 m, public on AWS). It is itself about 75-80 %
accurate and five years older than the scenes, so treat the scores as indicative.

Run from the project root:  .conda-env\\python.exe scripts\\validate.py
"""
import json
import math
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import online_catalog as catalog  # noqa: E402
from online_reader import _read_asset, make_grid, read_scene  # noqa: E402
from satimg import analysis, render  # noqa: E402

SITES = {
    "Seoul (city, river)": (126.90, 37.48, 127.10, 37.62),
    # Each area must sit inside one 3-degree WorldCover tile.
    "Cairo (desert, farms, city)": (31.10, 30.00, 31.35, 30.20),
    "Geneva (lake, farms, city)": (6.05, 46.15, 6.35, 46.35),
    "Manaus (rainforest, river)": (-59.99, -3.20, -59.75, -3.00),
    "Iowa (cropland)": (-93.75, 41.75, -93.50, 41.95),
}
START, END = "2026-06-01", "2026-09-30"
WC_WATER, WC_BUILT, WC_BARE = 80, 50, 60
WC_VEGETATION = (10, 20, 30, 40, 90, 95)  # trees, shrubs, grass, crops, wetland, mangrove
# Group id per reference class / per estimated K-means label: 0 water, 1 vegetation, 2 built or bare
WC_GROUP = {WC_WATER: 0, WC_BUILT: 2, WC_BARE: 2, **{c: 1 for c in WC_VEGETATION}}
LABEL_GROUP = {"Water": 0, "Dense vegetation": 1, "Sparse vegetation": 1, "Built-up / bare": 2}


def worldcover_href(bbox) -> str:
    lat, lon = math.floor(bbox[1] / 3) * 3, math.floor(bbox[0] / 3) * 3
    tile = f"{'N' if lat >= 0 else 'S'}{abs(lat):02d}{'E' if lon >= 0 else 'W'}{abs(lon):03d}"
    return ("https://esa-worldcover.s3.eu-central-1.amazonaws.com/v200/2021/map/"
            f"ESA_WorldCover_10m_2021_v200_{tile}_Map.tif")


def binary_scores(pred: np.ndarray, truth: np.ndarray) -> dict:
    tp = int(np.count_nonzero(pred & truth))
    fp = int(np.count_nonzero(pred & ~truth))
    fn = int(np.count_nonzero(~pred & truth))
    if tp + fn == 0:
        return {"reference_pct": 0.0}
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn)
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {"reference_pct": round(100 * (tp + fn) / pred.size, 1), "precision": round(precision, 3),
            "recall": round(recall, 3), "f1": round(f1, 3), "iou": round(tp / (tp + fp + fn), 3)}


def timed(fn, *args):
    t = time.perf_counter()
    out = fn(*args)
    return out, round(time.perf_counter() - t, 2)


def run_site(name: str, bbox: tuple) -> dict:
    grid = make_grid(bbox, 1024)
    scenes, t_search = timed(catalog.search, bbox, START, END, 20, 100)
    full = sorted((s for s in scenes if s["coverage"] > 0.99), key=lambda s: s["cloud_cover"])
    if not full:
        return {"site": name, "error": "no scene fully covering the area"}
    best = full[0]
    item = catalog.get_item(best["id"])
    scene, t_read = timed(read_scene, item, analysis.LANDCOVER_BANDS, grid, True)
    _, t_warm = timed(read_scene, item, analysis.LANDCOVER_BANDS, grid, True)
    indices, t_index = timed(lambda: {k: analysis.compute_index(k, scene) for k in analysis.INDICES})
    (labels, classes), t_kmeans = timed(analysis.landcover, scene, grid.pixel_area_m2, 6)
    ramp = analysis.INDICES["ndvi"]
    _, t_png = timed(render.colorize, indices["ndvi"], ramp["vmin"], ramp["mid"], ramp["vmax"], ramp["stops"])

    wc = _read_asset(worldcover_href(bbox), grid, "mode")
    ok = np.isfinite(indices["ndvi"]) & np.isfinite(indices["ndbi"]) & (wc > 0)
    ref = wc[ok]
    result = {
        "site": name, "scene": best["id"], "date": best["datetime"][:10], "scene_cloud_pct": best["cloud_cover"],
        "grid": f"{grid.width}x{grid.height}", "pixel_m": round(math.sqrt(grid.pixel_area_m2), 1),
        "usable_pct": round(100 * np.count_nonzero(ok) / ok.size, 1),
        "seconds": {"search": t_search, "read_7_bands_cold": t_read, "read_cached": t_warm,
                    "4_indices": t_index, "kmeans_6": t_kmeans, "png": t_png},
        "water (NDWI > 0)": binary_scores(indices["ndwi"][ok] > 0, ref == WC_WATER),
        "vegetation (NDVI >= 0.2)": binary_scores(indices["ndvi"][ok] >= 0.2, np.isin(ref, WC_VEGETATION)),
        "built-up (NDBI > 0)": binary_scores(indices["ndbi"][ok] > 0, ref == WC_BUILT),
        "built-up or bare (NDBI > 0)": binary_scores(indices["ndbi"][ok] > 0, np.isin(ref, (WC_BUILT, WC_BARE))),
    }

    # Land cover: compare 3 broad groups where both maps give one.
    to_group = np.array([LABEL_GROUP.get(c["group"], -1) for c in classes])
    pred_group = to_group[labels[ok]] if len(classes) else np.full(ref.shape, -1)
    ref_group = np.vectorize(lambda v: WC_GROUP.get(int(v), -1), otypes=[np.int8])(ref)
    both = (pred_group >= 0) & (ref_group >= 0)
    if both.any():
        result["landcover (3 groups)"] = {
            "overall_accuracy": round(float(np.mean(pred_group[both] == ref_group[both])), 3),
            "compared_pct": round(100 * float(np.mean(both)), 1),
            "cluster_labels": [c["label"] for c in classes],
        }

    # Change detection false alarms: two clear dates close together should show almost no change.
    pairs = [(a, b) for a in full for b in full
             if a["id"] < b["id"] and max(a["cloud_cover"], b["cloud_cover"]) <= 10
             and 0 < abs((np.datetime64(a["datetime"][:10]) - np.datetime64(b["datetime"][:10])).astype(int)) <= 15]
    if pairs:
        a, b = min(pairs, key=lambda p: p[0]["cloud_cover"] + p[1]["cloud_cover"])
        bands = analysis.INDICES["ndvi"]["bands"]
        diff = (analysis.compute_index("ndvi", read_scene(catalog.get_item(b["id"]), bands, grid, True))
                - analysis.compute_index("ndvi", read_scene(catalog.get_item(a["id"]), bands, grid, True)))
        valid = diff[np.isfinite(diff)]
        if valid.size:
            result["change false alarms (NDVI)"] = {
                "dates": sorted([a["datetime"][:10], b["datetime"][:10]]),
                "flagged_pct_at_0.2": round(100 * float(np.mean(np.abs(valid) > 0.2)), 2),
                "flagged_pct_at_0.1": round(100 * float(np.mean(np.abs(valid) > 0.1)), 2),
                "median_abs_diff": round(float(np.median(np.abs(valid))), 4),
            }
    return result


if __name__ == "__main__":
    results = []
    for site, bbox in SITES.items():
        try:
            results.append(run_site(site, bbox))
        except Exception as exc:  # keep going so one unreachable tile does not hide the rest
            results.append({"site": site, "error": f"{type(exc).__name__}: {exc}"})
        print(json.dumps(results[-1], indent=1), flush=True)
    out = Path(__file__).with_name("validation_results.json")
    out.write_text(json.dumps(results, indent=1), encoding="utf-8")
    print(f"\nSaved {out}")
