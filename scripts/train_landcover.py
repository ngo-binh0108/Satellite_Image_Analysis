"""Developer tool (needs internet): train the named land cover classifier shipped with the app.

Labels come from ESA WorldCover 2021 (10 m); pixels from Sentinel-2 L2A scenes of sites on six
continents, taken in each site's growing season. Three model variants are trained so the app can
classify images with six bands, with four (visible + near-infrared), or with red/green/blue only.
Two class schemes are trained and compared on sites held out of training entirely:
  "5class": forest, farmland / grassland, urban, bare, water
  "4class": the same without bare
Each is written to satimg/models/landcover_<scheme>.joblib; copy the chosen one to landcover.joblib
and set CLASSIFIER_CLASSES in satimg/analysis.py to match.

Run from the project root:  .conda-env\\python.exe -u scripts\\train_landcover.py
"""
import os

# Before numpy / scikit-learn load: with one thread per core, training is many times slower on big machines.
os.environ.setdefault("OMP_NUM_THREADS", str(min(8, os.cpu_count() or 1)))

import json  # noqa: E402
import math  # noqa: E402
import sys  # noqa: E402
import time  # noqa: E402
from pathlib import Path  # noqa: E402

import joblib  # noqa: E402
import numpy as np  # noqa: E402
from sklearn.ensemble import HistGradientBoostingClassifier  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import online_catalog as catalog  # noqa: E402
from online_reader import _read_asset, make_grid, read_scene  # noqa: E402
from satimg import analysis  # noqa: E402

# Each box must sit inside one 3-degree WorldCover tile.
SITES = {
    "Seoul": (126.85, 37.45, 127.10, 37.65), "Cairo": (31.10, 30.00, 31.35, 30.20),
    "Manaus": (-59.95, -3.25, -59.70, -3.05), "Berlin": (13.25, 52.40, 13.55, 52.58),
    "Punjab": (75.70, 30.80, 75.95, 31.00), "Sao Paulo": (-46.75, -23.70, -46.50, -23.50),
    "Sydney": (150.85, -33.95, 151.10, -33.75), "Texas": (-97.90, 30.20, -97.65, 30.40),
    "Zaragoza": (-1.05, 41.55, -0.80, 41.75), "Java": (110.30, -7.90, 110.55, -7.70),
    "Hokkaido": (143.10, 42.85, 143.35, 43.05), "Lagos": (3.30, 6.45, 3.55, 6.65),
    "Paris": (2.20, 48.75, 2.45, 48.95), "Kyiv": (30.60, 49.90, 30.85, 50.10),
    "Beijing": (116.20, 39.80, 116.45, 40.00), "Bangkok": (100.40, 13.65, 100.65, 13.85),
    "Johannesburg": (27.90, -26.30, 28.15, -26.10), "Melbourne": (144.85, -37.95, 145.10, -37.75),
    "Mexico City": (-98.95, 19.25, -98.70, 19.45), "Riyadh": (46.60, 24.60, 46.85, 24.80),
    "Phoenix": (-112.20, 33.35, -111.95, 33.55), "Toronto": (-79.60, 43.60, -79.35, 43.80),
    "Rome": (12.35, 41.75, 12.60, 41.95), "Kolkata": (88.25, 22.45, 88.50, 22.65),
    # held out of training, used only to measure accuracy
    "Geneva": (6.05, 46.15, 6.35, 46.35), "Iowa": (-93.75, 41.75, -93.50, 41.95),
    "Da Nang": (108.10, 15.95, 108.35, 16.15), "Nairobi": (36.70, -1.40, 36.95, -1.20),
    "Chengdu": (104.00, 30.55, 104.25, 30.75), "Buenos Aires": (-58.60, -34.75, -58.35, -34.55),
}
HOLDOUT = {"Geneva", "Iowa", "Da Nang", "Nairobi", "Chengdu", "Buenos Aires"}
NORTH_SUMMER, SOUTH_SUMMER = ("2026-06-01", "2026-08-31"), ("2025-12-01", "2026-02-28")
# WorldCover code -> class index per scheme (wetland, snow and moss are not used).
SCHEMES = {
    "5class": {"names": ["Forest", "Farmland / grassland", "Urban / built-up", "Bare / sparse", "Water"],
               "codes": {10: 0, 20: 1, 30: 1, 40: 1, 50: 2, 60: 3, 80: 4}},
    "4class": {"names": ["Forest", "Farmland / grassland", "Urban / built-up", "Water"],
               "codes": {10: 0, 20: 1, 30: 1, 40: 1, 50: 2, 80: 3}},
}
CODES = (10, 20, 30, 40, 50, 60, 80)
PER_CODE = 2500         # training pixels per WorldCover class per scene
HOLDOUT_PIXELS = 20000  # per scene, in natural proportions


def worldcover_href(bbox) -> str:
    lat, lon = math.floor(bbox[1] / 3) * 3, math.floor(bbox[0] / 3) * 3
    assert math.floor(bbox[3] / 3) * 3 == lat and math.floor(bbox[2] / 3) * 3 == lon, f"{bbox} spans WorldCover tiles"
    tile = f"{'N' if lat >= 0 else 'S'}{abs(lat):02d}{'E' if lon >= 0 else 'W'}{abs(lon):03d}"
    return ("https://esa-worldcover.s3.eu-central-1.amazonaws.com/v200/2021/map/"
            f"ESA_WorldCover_10m_2021_v200_{tile}_Map.tif")


def growing_season_scenes(bbox: tuple) -> list[dict]:
    """Up to two clear scenes at least ten days apart, from the local summer (either window in the tropics)."""
    latitude = (bbox[1] + bbox[3]) / 2
    windows = [NORTH_SUMMER] if latitude > 23 else [SOUTH_SUMMER] if latitude < -23 else [NORTH_SUMMER, SOUTH_SUMMER]
    found = [s for start, end in windows for s in catalog.search(bbox, start, end, 15, 60) if s["coverage"] > 0.99]
    chosen = []
    for scene in sorted(found, key=lambda s: s["cloud_cover"]):
        day = np.datetime64(scene["datetime"][:10])
        if all(abs((day - np.datetime64(c["datetime"][:10])).astype(int)) >= 10 for c in chosen):
            chosen.append(scene)
        if len(chosen) == 2:
            break
    return chosen


def collect(site: str, bbox: tuple, rng) -> list[dict]:
    grid = make_grid(bbox, 1024)
    reference = _read_asset(worldcover_href(bbox), grid, "mode")
    out = []
    for best in growing_season_scenes(bbox):
        scene = read_scene(catalog.get_item(best["id"]), analysis.LANDCOVER_BANDS, grid, True)
        valid = np.all([np.isfinite(scene[b]) for b in analysis.LANDCOVER_BANDS], axis=0) & np.isin(reference, CODES)
        if site in HOLDOUT:
            index = np.flatnonzero(valid)
            index = rng.choice(index, min(HOLDOUT_PIXELS, index.size), replace=False)
        else:
            index = np.concatenate([rng.choice(found, min(PER_CODE, found.size), replace=False)
                                    for code in CODES for found in [np.flatnonzero(valid & (reference == code))] if found.size])
        pixels = {b: scene[b].ravel()[index] for b in analysis.LANDCOVER_BANDS}
        # The RGB variant normalises brightness by the image median, so keep that per scene.
        pixels["_median_brightness"] = float(np.nanmedian(scene["red"] + scene["green"] + scene["blue"]))
        out.append({"site": site, "date": best["datetime"][:10], "pixels": pixels, "codes": reference.ravel()[index]})
        print(f"  {site} {best['datetime'][:10]} cloud {best['cloud_cover']}%: {index.size} pixels", flush=True)
    if not out:
        print(f"  {site}: no clear growing-season scene", flush=True)
    return out


def dataset(samples: list[dict], variant: str, codes: dict) -> tuple[np.ndarray, np.ndarray, list[str]]:
    """Features, class labels and site per pixel, keeping only pixels whose WorldCover code the scheme uses."""
    xs, ys, sites = [], [], []
    for s in samples:
        keep = np.isin(s["codes"], list(codes))
        xs.append(analysis.classifier_features(s["pixels"], variant, s["pixels"]["_median_brightness"])[keep])
        ys.append(np.vectorize(codes.get, otypes=[np.int8])(s["codes"][keep]))
        sites += [s["site"]] * int(keep.sum())
    return np.vstack(xs), np.concatenate(ys), np.array(sites)


if __name__ == "__main__":
    rng = np.random.default_rng(0)
    started = time.time()
    samples = []
    for site, bbox in SITES.items():
        try:
            samples += collect(site, bbox, rng)
        except Exception as exc:  # one unreachable site should not stop the run
            print(f"  {site}: skipped ({type(exc).__name__}: {exc})", flush=True)
    print(f"collected in {time.time() - started:.0f} s", flush=True)
    train = [s for s in samples if s["site"] not in HOLDOUT]
    test = [s for s in samples if s["site"] in HOLDOUT]
    make = lambda: HistGradientBoostingClassifier(max_iter=300, max_depth=8, learning_rate=0.1, random_state=0)
    report = {"train_scenes": [f"{s['site']} {s['date']}" for s in train],
              "holdout_scenes": [f"{s['site']} {s['date']}" for s in test], "schemes": {}}
    (ROOT / "satimg" / "models").mkdir(exist_ok=True)
    for scheme, spec in SCHEMES.items():
        models, variants = {}, {}
        for variant in analysis.CLASSIFIER_VARIANTS:
            x_train, y_train, _ = dataset(train, variant, spec["codes"])
            x_test, y_test, test_sites = dataset(test, variant, spec["codes"])
            predicted = make().fit(x_train, y_train).predict(x_test)
            per_class = {}
            for cls, name in enumerate(spec["names"]):
                tp = int(np.sum((predicted == cls) & (y_test == cls)))
                per_class[name] = {"reference_pixels": int(np.sum(y_test == cls)),
                                   "recall": round(tp / max(1, int(np.sum(y_test == cls))), 3),
                                   "precision": round(tp / max(1, int(np.sum(predicted == cls))), 3)}
            variants[variant] = {
                "train_pixels": int(len(y_train)), "holdout_pixels": int(len(y_test)),
                "holdout_accuracy": round(float(np.mean(predicted == y_test)), 3),
                "holdout_accuracy_by_site": {site: round(float(np.mean(predicted[test_sites == site] == y_test[test_sites == site])), 3)
                                             for site in sorted(set(test_sites))},
                "per_class": per_class}
            print(scheme, variant, "holdout accuracy", variants[variant]["holdout_accuracy"],
                  variants[variant]["holdout_accuracy_by_site"], f"({time.time() - started:.0f} s)", flush=True)
            # The shipped model is refitted on every site.
            x_all, y_all, _ = dataset(samples, variant, spec["codes"])
            models[variant] = make().fit(x_all, y_all)
        report["schemes"][scheme] = variants
        joblib.dump({"models": models, "classes": spec["names"], "report": variants},
                    ROOT / "satimg" / "models" / f"landcover_{scheme}.joblib", compress=3)
    Path(__file__).with_name("landcover_report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print(f"done in {time.time() - started:.0f} s", flush=True)
