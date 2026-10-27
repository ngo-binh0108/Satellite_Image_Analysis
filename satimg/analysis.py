"""Spectral indices, land cover clustering, change detection and their statistics."""
import numpy as np
from scipy import ndimage
from sklearn.cluster import KMeans

# Diverging ramps: low colour, neutral grey at `mid`, high colour.
GREY = "#f0efec"
INDICES = {
    "ndvi": dict(label="NDVI – vegetation", bands=("nir", "red"), vmin=-0.4, mid=0.0, vmax=0.9,
                 stops=("#8a5a1c", GREY, "#006300"),
                 classes=((-1, 0.0, "Water / no vegetation"), (0.0, 0.2, "Bare / built-up"),
                          (0.2, 0.5, "Sparse vegetation"), (0.5, 1.01, "Dense vegetation"))),
    "ndwi": dict(label="NDWI – open water", bands=("green", "nir"), vmin=-0.8, mid=0.0, vmax=0.8,
                 stops=("#a8691c", GREY, "#1c5cab"),
                 classes=((-1, 0.0, "Land"), (0.0, 1.01, "Water"))),
    "ndbi": dict(label="NDBI – built-up", bands=("swir16", "nir"), vmin=-0.6, mid=0.0, vmax=0.6,
                 stops=("#0f7a55", GREY, "#c2491a"),
                 classes=((-1, 0.0, "Vegetation / water"), (0.0, 1.01, "Built-up / bare"))),
    "nbr": dict(label="NBR – burn ratio", bands=("nir", "swir22"), vmin=-0.5, mid=0.0, vmax=0.9,
                stops=("#b23a2e", GREY, "#006300"),
                classes=((-1, 0.1, "Burned / bare"), (0.1, 0.4, "Low vegetation"),
                         (0.4, 1.01, "Healthy vegetation"))),
}
CHANGE = dict(vmin=-0.6, mid=0.0, vmax=0.6, stops=("#c23a3a", GREY, "#1c5cab"))
COMPOSITES = {
    "true_color": dict(label="True colour", bands=("red", "green", "blue")),
    "false_color": dict(label="False colour (vegetation in red)", bands=("nir", "red", "green")),
    "swir": dict(label="Short-wave infrared", bands=("swir22", "nir", "red")),
}
LANDCOVER_BANDS = ("blue", "green", "red", "nir", "swir16", "swir22")
LANDCOVER_COLORS = ("#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948")
HIST_BINS = 40


def normalized_difference(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    with np.errstate(divide="ignore", invalid="ignore"):
        out = (a - b) / (a + b)
    out[~np.isfinite(out)] = np.nan
    return np.clip(out, -1, 1)


def compute_index(name: str, scene: dict) -> np.ndarray:
    a, b = INDICES[name]["bands"]
    return normalized_difference(scene[a], scene[b])


def _km2(count, pixel_area_m2: float) -> float:
    return round(float(count) * pixel_area_m2 / 1e6, 4)


def summarize(values: np.ndarray, pixel_area_m2: float, classes) -> dict:
    """Statistics of a [-1, 1] layer; `classes` are (low, high, label) with high exclusive."""
    valid = values[np.isfinite(values)]
    out = {
        "total_km2": _km2(values.size, pixel_area_m2),
        "valid_km2": _km2(valid.size, pixel_area_m2),
        "valid_pct": round(100 * valid.size / values.size, 1),
    }
    if valid.size == 0:
        return out | {"classes": [], "histogram": None}
    p10, p50, p90 = np.percentile(valid, [10, 50, 90])
    counts, edges = np.histogram(np.clip(valid, -1, 1), bins=HIST_BINS, range=(-1, 1))
    out |= {
        "mean": round(float(valid.mean()), 4), "std": round(float(valid.std()), 4),
        "min": round(float(valid.min()), 4), "max": round(float(valid.max()), 4),
        "p10": round(float(p10), 4), "median": round(float(p50), 4), "p90": round(float(p90), 4),
        "classes": [
            {"label": label, "km2": _km2(n, pixel_area_m2), "pct": round(100 * n / valid.size, 1)}
            for lo, hi, label in classes
            for n in [int(np.count_nonzero((valid >= lo) & (valid < hi)))]
        ],
        "histogram": {"edges": [round(float(e), 3) for e in edges],
                      "km2": [_km2(c, pixel_area_m2) for c in counts]},
    }
    return out


def change_classes(threshold: float):
    return ((-2.01, -threshold, "Decrease"), (-threshold, threshold, "No significant change"),
            (threshold, 2.01, "Increase"))


def _guess_label(ndvi: float, ndwi: float, ndbi: float) -> str:
    if ndwi > 0.0:
        return "Water"
    if ndvi > 0.5:
        return "Dense vegetation"
    if ndvi > 0.2:
        return "Sparse vegetation"
    if ndbi > 0.0:
        return "Built-up / bare"
    return "Other"


def landcover(scene: dict, pixel_area_m2: float, k: int = 6, sample: int = 20000) -> tuple[np.ndarray, list[dict]]:
    """Unsupervised K-means over whichever of the six bands the image has. Returns labels
    (-1 = no data) and class info. With a near-infrared band the classes get a rule-of-thumb
    name from their mean indices (not ground truth); without one they are just numbered."""
    bands = [b for b in LANDCOVER_BANDS if b in scene]
    stack = np.stack([scene[b] for b in bands], axis=-1)
    valid = np.isfinite(stack).all(axis=-1)
    pixels = stack[valid]
    labels = np.full(valid.shape, -1, dtype=np.int16)
    if len(pixels) < k:
        return labels, []
    rng = np.random.default_rng(0)
    fit_on = pixels if len(pixels) <= sample else pixels[rng.choice(len(pixels), sample, replace=False)]
    model = KMeans(n_clusters=k, n_init=4, random_state=0).fit(fit_on)
    assigned = model.predict(pixels)

    centre = dict(zip(bands, model.cluster_centers_.T))
    ratio = lambda a, b: (centre[a] - centre[b]) / (centre[a] + centre[b] + 1e-9) if a in centre and b in centre else None
    ndvi, ndwi, ndbi = ratio("nir", "red"), ratio("green", "nir"), ratio("swir16", "nir")
    # Stable numbering: least to most vegetated, or darkest to brightest without near-infrared.
    order = np.argsort(ndvi if ndvi is not None else model.cluster_centers_.mean(axis=1))
    rank = np.empty(k, dtype=np.int16)
    rank[order] = np.arange(k)
    labels[valid] = rank[assigned]

    counts = np.bincount(rank[assigned], minlength=k)
    info = []
    for i, c in enumerate(order):
        name = _guess_label(ndvi[c], ndwi[c], ndbi[c] if ndbi is not None else -1.0) if ndvi is not None else "Class"
        info.append({
            "id": int(i), "color": LANDCOVER_COLORS[i], "label": name, "group": name,
            "mean_ndvi": round(float(ndvi[c]), 3) if ndvi is not None else None,
            "km2": _km2(counts[i], pixel_area_m2),
            "pct": round(100 * int(counts[i]) / len(pixels), 1),
        })
    # Several clusters often get the same estimated name; number them so rows stay distinguishable.
    names = [c["group"] for c in info]
    for c in info:
        if names.count(c["group"]) > 1:
            c["label"] = f"{c['group']} {names[:c['id']].count(c['group']) + 1}"
    return labels, info

# ---- vessel candidates -------------------------------------------------------------------
VESSEL_BANDS = ("red", "green", "blue", "nir")
VESSEL_MAX_PIXEL_M = 12.0
VESSEL_SIZES = ((0, 50, "Under 50 m"), (50, 150, "50-150 m"), (150, 1e9, "Over 150 m"))


MIN_WATER_BODY_KM2 = 0.05
BACKGROUND_WINDOW_M = 500


def water_mask(scene: dict) -> np.ndarray:
    """Open water: NDWI above zero and dark in near-infrared (bright roofs can also have NDWI > 0)."""
    ndwi = np.nan_to_num(normalized_difference(scene["green"], scene["nir"]), nan=-1.0)
    return (ndwi > 0) & (np.nan_to_num(scene["nir"], nan=1.0) < 0.15)


def large_regions(mask: np.ndarray, min_km2: float, pixel_m: float) -> np.ndarray:
    """The connected parts of a mask that cover at least `min_km2`."""
    labels, count = ndimage.label(mask)
    if count == 0:
        return mask
    big = np.bincount(labels.ravel()) * pixel_m ** 2 / 1e6 >= min_km2
    big[0] = False
    return big[labels]


def _open_water(scene: dict, pixel_m: float) -> np.ndarray:
    """Water mask without ponds and stray pixels, which are too small to hold a vessel."""
    return large_regions(water_mask(scene), MIN_WATER_BODY_KM2, pixel_m)


def detect_vessels(scene: dict, pixel_m: float, min_contrast: float = 8.0, min_length_m: float = 20.0,
                   max_length_m: float = 500.0, other_scene: dict | None = None) -> tuple[list[dict], dict]:
    """Compact objects that stand out from the surrounding open water. This is a rule-based
    detector, not a trained model: small islands, rocks, platforms and breaking waves can also
    qualify. With `other_scene` (another date), anything that is also not water there is dropped
    as a fixed object."""
    valid = np.all([np.isfinite(scene[b]) for b in VESSEL_BANDS], axis=0)
    water = _open_water(scene, pixel_m) & valid
    radius = max(2, int(np.ceil(40 / pixel_m)))  # closes gaps up to ~80 m: wider than any hull
    yy, xx = np.ogrid[-radius:radius + 1, -radius:radius + 1]
    sea = ndimage.binary_closing(water, yy * yy + xx * xx <= radius * radius)
    summary = {"water_km2": round(float(np.count_nonzero(water)) * pixel_m ** 2 / 1e6, 3),
               "candidates": 0, "removed_fixed": 0}
    if not water.any():
        return [], summary

    # Brightness above the local water background, in units of the water's own variability.
    bright = np.nan_to_num(np.maximum.reduce([scene[b] for b in VESSEL_BANDS]))
    window = max(5, int(BACKGROUND_WINDOW_M / pixel_m) | 1)
    share = ndimage.uniform_filter(water.astype(np.float32), window)
    local_mean = ndimage.uniform_filter(np.where(water, bright, 0).astype(np.float32), window) / np.maximum(share, 1e-3)
    residual = bright - local_mean
    spread = 1.4826 * float(np.median(np.abs(residual[water]))) + 1e-4
    anomaly = residual > min_contrast * spread

    candidates = sea & valid & (anomaly | ~water)
    labels, count = ndimage.label(candidates, structure=np.ones((3, 3)))
    summary["candidates"] = int(count)
    if count == 0:
        return [], summary
    # Anything that touches land or missing data is a pier, a bridge or a cloud edge, not a vessel.
    outside = ndimage.binary_dilation(~sea | ~valid, np.ones((3, 3)))
    touching = set(np.unique(labels[outside & candidates]).tolist())
    fixed = None
    if other_scene is not None:
        other_valid = np.all([np.isfinite(other_scene[b]) for b in VESSEL_BANDS], axis=0)
        fixed = ~water_mask(other_scene) & other_valid
    objects = []
    for index, box in enumerate(ndimage.find_objects(labels), start=1):
        if index in touching or box is None:
            continue
        inside = labels[box] == index
        rows, cols = np.nonzero(inside)
        if rows.size < 2:
            continue
        # Length and width from the spread of the pixels along their principal axes.
        if np.ptp(rows) + np.ptp(cols) > 0:
            eigen = np.linalg.eigvalsh(np.cov(np.vstack([rows, cols])))
        else:
            eigen = np.zeros(2)
        length = (np.sqrt(12 * max(eigen[1], 0)) + 1) * pixel_m
        width = (np.sqrt(12 * max(eigen[0], 0)) + 1) * pixel_m
        if not (min_length_m <= length <= max_length_m):
            continue
        contrast = float(residual[box][inside].max()) / spread
        if contrast < min_contrast:
            continue
        if fixed is not None and fixed[box][inside].mean() > 0.5:
            summary["removed_fixed"] += 1
            continue
        objects.append({
            "row": float(rows.mean() + box[0].start + 0.5), "col": float(cols.mean() + box[1].start + 0.5),
            "box": [box[0].start, box[1].start, box[0].stop, box[1].stop],
            "length_m": round(float(length)), "width_m": round(float(width)),
            "area_m2": round(float(rows.size) * pixel_m ** 2), "contrast": round(contrast, 1),
        })
    objects.sort(key=lambda o: -o["length_m"])
    for number, obj in enumerate(objects, start=1):
        obj["id"] = number
    return objects, summary


def vessel_size_classes(objects: list[dict]) -> list[dict]:
    out = []
    for low, high, label in VESSEL_SIZES:
        group = [o for o in objects if low <= o["length_m"] < high]
        out.append({"label": label, "count": len(group),
                    "km2": round(sum(o["area_m2"] for o in group) / 1e6, 4),
                    "pct": round(100 * len(group) / len(objects), 1) if objects else 0.0})
    return out

# ---- named land cover (trained classifier) ---------------------------------------------------
# Colour and map-label wording per class name. The list of classes itself comes from the model file.
CLASS_STYLE = {
    "Forest": ("#1f8a3b", "Forest area"), "Farmland / grassland": ("#eda100", "Farmland / grassland"),
    "Grass / shrub": ("#8fc866", "Grassland"), "Agriculture": ("#eda100", "Farmland"),
    "Urban / built-up": ("#e34948", "Urban area"), "Bare / sparse": ("#a8998a", "Bare ground"),
    "Water": ("#2a78d6", "Water"),
}
# Model variants by the bands an image offers, best first.
CLASSIFIER_VARIANTS = {"6band": LANDCOVER_BANDS, "4band": ("blue", "green", "red", "nir"), "rgb": ("red", "green", "blue")}
FOREST = 0


def classifier_features(scene: dict, variant: str, median_brightness: float | None = None) -> np.ndarray:
    """One row of features per pixel. Used for training and for prediction, so both always agree."""
    ratio = lambda a, b: (scene[a] - scene[b]) / (scene[a] + scene[b] + 1e-6)
    if variant == "6band":
        columns = [scene[b] for b in LANDCOVER_BANDS] + [ratio("nir", "red"), ratio("green", "nir"),
                                                         ratio("swir16", "nir"), ratio("nir", "swir22")]
    elif variant == "4band":
        columns = [scene[b] for b in CLASSIFIER_VARIANTS["4band"]] + [ratio("nir", "red"), ratio("green", "nir"),
                                                                     scene["red"] + scene["green"] + scene["blue"]]
    else:
        # A plain picture has no known brightness scale: use colour proportions, and brightness
        # relative to the picture's own median.
        total = scene["red"] + scene["green"] + scene["blue"] + 1e-6
        median = median_brightness if median_brightness else float(np.nanmedian(total))
        columns = [scene["red"] / total, scene["green"] / total, scene["blue"] / total,
                   ratio("green", "red"), ratio("blue", "red"), total / max(median, 1e-6)]
    return np.stack([np.asarray(c, dtype=np.float32).ravel() for c in columns], axis=1)


_classifier = None


def load_classifier() -> dict:
    global _classifier
    if _classifier is None:
        import joblib
        from pathlib import Path
        path = Path(__file__).resolve().parent / "models" / "landcover.joblib"
        if not path.exists():
            raise ValueError("The land cover model file is missing (satimg/models/landcover.joblib)")
        _classifier = joblib.load(path)
    return _classifier


def classifier_classes() -> list[tuple]:
    """(name, colour, map label) of each class the shipped model predicts, in class-index order."""
    return [(name, *CLASS_STYLE.get(name, ("#9085e9", name))) for name in load_classifier()["classes"]]


def classify(scene: dict) -> tuple[np.ndarray, np.ndarray, str]:
    """Class index per pixel (-1 = no data), the model's confidence per pixel, and the variant used."""
    variant = next((v for v, bands in CLASSIFIER_VARIANTS.items() if all(b in scene for b in bands)), None)
    if variant is None:
        raise ValueError("Land cover classes need at least red, green and blue bands assigned")
    bands = CLASSIFIER_VARIANTS[variant]
    valid = np.all([np.isfinite(scene[b]) for b in bands], axis=0)
    labels = np.full(valid.shape, -1, dtype=np.int16)
    confidence = np.zeros(valid.shape, dtype=np.float32)
    if not valid.any():
        return labels, confidence, variant
    median = float(np.median(sum(scene[b][valid] for b in ("red", "green", "blue"))))
    model = load_classifier()["models"][variant]
    probability = np.zeros(valid.shape + (len(model.classes_),), dtype=np.float32)
    probability[valid] = model.predict_proba(classifier_features({b: scene[b][valid] for b in bands}, variant, median))
    # Averaging each class's probability over a 3x3 neighbourhood removes single-pixel speckle.
    for cls in range(probability.shape[-1]):
        probability[..., cls] = ndimage.uniform_filter(probability[..., cls], 3)
    labels[valid] = probability.argmax(axis=-1)[valid]
    confidence[valid] = probability.max(axis=-1)[valid]
    return labels, confidence, variant


def class_summary(labels: np.ndarray, confidence: np.ndarray, pixel_area_m2: float) -> list[dict]:
    valid = int(np.count_nonzero(labels >= 0))
    min_region = max(4, int(10000 / pixel_area_m2))  # regions under one hectare are not counted
    out = []
    for cls, (name, color, _) in enumerate(classifier_classes()):
        mask = labels == cls
        count = int(np.count_nonzero(mask))
        parts, found = ndimage.label(mask)
        regions = int(np.count_nonzero(np.bincount(parts.ravel())[1:] >= min_region)) if found else 0
        out.append({"id": cls, "label": name, "color": color, "count": regions, "km2": _km2(count, pixel_area_m2),
                    "pct": round(100 * count / valid, 1) if valid else 0.0,
                    "confidence": round(float(confidence[mask].mean()), 2) if count else None})
    return out


def outline_regions(mask_by_name: list[tuple], pixel_area_m2: float, total_pixels: int,
                    limit: int = 8, per_name: int = 2, min_share: float = 0.004) -> list[dict]:
    """The largest connected regions of the given (name, color, mask) layers as simplified
    polygons in pixel coordinates, each with a point inside it for a label."""
    from rasterio import features
    from shapely.geometry import shape
    candidates = []
    for name, color, mask in mask_by_name:
        parts, found = ndimage.label(mask)
        if not found:
            continue
        sizes = np.bincount(parts.ravel())
        sizes[0] = 0
        for index in np.argsort(sizes)[::-1][:per_name]:
            if sizes[index] >= max(30, min_share * total_pixels):
                candidates.append((int(sizes[index]), name, color, parts == index))
    regions = []
    for size, name, color, region in sorted(candidates, key=lambda c: -c[0])[:limit]:
        filled = ndimage.binary_fill_holes(region)
        polygons = [shape(geometry) for geometry, value in features.shapes(filled.astype(np.uint8), mask=filled) if value]
        polygon = max(polygons, key=lambda p: p.area).simplify(1.5)
        anchor = polygon.representative_point()
        regions.append({"label": name, "color": color, "km2": _km2(size, pixel_area_m2),
                        "polygon": [[round(x, 1), round(y, 1)] for x, y in polygon.exterior.coords],
                        "anchor": [round(anchor.x, 1), round(anchor.y, 1)]})
    return regions


def forest_change(labels_a: np.ndarray, labels_b: np.ndarray, pixel_area_m2: float) -> tuple[np.ndarray, list[dict]]:
    """0 = no forest change, 1 = forest in A but not in B (loss), 2 = the reverse (gain), -1 = no data.
    Patches of only a few pixels are dropped as noise."""
    both = (labels_a >= 0) & (labels_b >= 0)
    clean = lambda mask: ndimage.binary_opening(mask, np.ones((3, 3)))
    loss = clean(both & (labels_a == FOREST) & (labels_b != FOREST))
    gain = clean(both & (labels_a != FOREST) & (labels_b == FOREST))
    change = np.where(both, 0, -1).astype(np.int16)
    change[loss], change[gain] = 1, 2
    valid = int(np.count_nonzero(both))
    stayed = int(np.count_nonzero(both & (labels_a == FOREST) & (labels_b == FOREST)))
    rows = (("Forest loss", "#e34948", int(np.count_nonzero(loss))), ("Forest gain", "#2a78d6", int(np.count_nonzero(gain))),
            ("Forest in both", "#1f8a3b", stayed), ("Not forest", "#5d6f8c", valid - stayed - int(np.count_nonzero(loss | gain))))
    return change, [{"label": name, "color": color, "km2": _km2(count, pixel_area_m2),
                     "pct": round(100 * count / valid, 1) if valid else 0.0} for name, color, count in rows]