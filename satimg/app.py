"""Local analysis engine for the desktop app. It serves 127.0.0.1 only and never goes online:
every image comes from a file the user opened.  Run with:  python -m satimg --port 8765"""
import threading
from functools import lru_cache

import numpy as np
from fastapi import Body, FastAPI, Query
from fastapi.responses import JSONResponse, Response
from rasterio.errors import RasterioError

from . import analysis, local, render
from .local import LocalImage, View

app = FastAPI(title="Satellite Image Analysis (offline engine)")
PNG_HEADERS = {"Cache-Control": "no-store"}
COMPOSITE_PURPOSE = {"true_color": "A true-colour picture", "false_color": "A false-colour picture",
                     "swir": "A short-wave infrared picture"}


@app.exception_handler(ValueError)
def _bad_request(_, exc: ValueError):
    return JSONResponse({"detail": str(exc)}, status_code=400)


@app.exception_handler(RasterioError)
def _read_failed(_, exc: Exception):
    return JSONResponse({"detail": f"Could not read the image file: {exc}"}, status_code=500)


_heavy = threading.Lock()


def _one_at_a_time(cached):
    """The picture and the statistics of one result are requested together. Running the slow
    step under a lock lets the second request wait and then take the first one's cached answer."""
    def wrapper(*args):
        with _heavy:
            return cached(*args)
    wrapper.cache_clear = cached.cache_clear
    return wrapper


# ---- reading -------------------------------------------------------------------------------

def _area(view: View) -> float:
    """Ground area of one view pixel in m². Without a known pixel size the 'area' is a count
    of image pixels instead (scaled so the km² fields come out as pixels)."""
    return view.pixel_area_m2 if view.pixel_area_m2 else 1e6 * view.step ** 2


def _unit(view: View) -> str:
    return "km²" if view.pixel_area_m2 else "px"


def _index_layer(product: str, image: LocalImage, other: LocalImage | None, view: View) -> np.ndarray:
    """An index for one image, or its change (B minus A) when a second image is given."""
    bands = analysis.INDICES[product]["bands"]
    layer = analysis.compute_index(product, local.read_roles(image, bands, view, product.upper()))
    if other:
        layer = analysis.compute_index(product, local.read_roles(other, bands, view, product.upper())) - layer
    return layer


@lru_cache(maxsize=32)
def _stretch(image_id: str, version: int, bands: tuple) -> tuple:
    """Display range for a colour picture, measured once over the whole image so every part of
    it is drawn alike. The three bands share one range, which keeps the colour balance."""
    image = local.get(image_id)
    scene = local.read_roles(image, bands, local.make_view(image, None, 512), "A colour picture")
    values = np.concatenate([scene[band][np.isfinite(scene[band])] for band in bands])
    low, high = (float(v) for v in np.percentile(values, [0.5, 99.5])) if values.size else (0.0, 1.0)
    return (low,) * len(bands), (high,) * len(bands)


def _composite(product: str, image: LocalImage, view: View) -> bytes:
    bands = analysis.COMPOSITES[product]["bands"]
    scene = local.read_roles(image, bands, view, COMPOSITE_PURPOSE[product])
    low, high = _stretch(image.id, image.version, bands)
    return render.composite_stretched([scene[b] for b in bands], low, high)


@_one_at_a_time
@lru_cache(maxsize=16)
def _landcover(view: View, k: int):
    image = local.get(view.image_id)
    bands = tuple(b for b in analysis.LANDCOVER_BANDS if image.mapping.get(b) is not None)
    if len(bands) < 3:
        raise ValueError("Land cover needs at least three assigned bands. Use Bands… to assign them.")
    return analysis.landcover(local.read_roles(image, bands, view, "Land cover"), _area(view), k)


@_one_at_a_time
@lru_cache(maxsize=16)
def _vessels(view: View, other_id: str | None, other_version: int, min_contrast: float, min_length: float):
    image = local.get(view.image_id)
    if not view.pixel_area_m2:
        raise ValueError("Object detection needs the ground size of a pixel. This image has no georeferencing: "
                         "enter the pixel size in Bands….")
    pixel_m = view.pixel_area_m2 ** 0.5
    if pixel_m > analysis.VESSEL_MAX_PIXEL_M:
        raise ValueError(f"The selected area would be analysed at {pixel_m:.0f} m per pixel, too coarse to find "
                         "vessels. Draw a smaller area.")
    scene = local.read_roles(image, analysis.VESSEL_BANDS, view, "Object detection")
    other = local.read_roles(local.get(other_id), analysis.VESSEL_BANDS, view, "Object detection") if other_id else None
    objects, summary = analysis.detect_vessels(scene, pixel_m, min_contrast, min_length, other_scene=other)
    for obj in objects:
        col = view.window[0] + obj.pop("col") * view.step
        row = view.window[1] + obj.pop("row") * view.step
        obj["col"], obj["row"] = round(col, 1), round(row, 1)
        position = local.lonlat_of(image, col, row)
        if position:
            obj["lon"], obj["lat"] = position
    return objects, summary


@_one_at_a_time
@lru_cache(maxsize=16)
def _classes(image_id: str, version: int, view: View):
    """Named land cover of an image on a view (the view may belong to another image, for comparing dates)."""
    image = local.get(image_id)
    bands = next((b for b in analysis.CLASSIFIER_VARIANTS.values() if all(image.mapping.get(r) is not None for r in b)), None)
    if bands is None:
        raise ValueError(f"Land cover classes need at least red, green and blue bands, which '{image.name}' does "
                         "not have assigned. Use Bands… to assign them.")
    return analysis.classify(local.read_roles(image, bands, view, "Land cover"))


def _forest_change(img: LocalImage, other: LocalImage, view: View):
    labels_a, _, _ = _classes(img.id, img.version, view)
    labels_b, _, _ = _classes(other.id, other.version, view)
    return analysis.forest_change(labels_a, labels_b, _area(view))


def _to_image(view: View, regions: list[dict]) -> list[dict]:
    """Region outlines from view pixels to pixels of the full image, which is what the viewer draws in."""
    place = lambda p: [round(view.window[0] + p[0] * view.step, 1), round(view.window[1] + p[1] * view.step, 1)]
    return [r | {"polygon": [place(p) for p in r["polygon"]], "anchor": place(r["anchor"])} for r in regions]


def _check_product(product: str, image_b: str | None):
    known = (product in analysis.INDICES or product in analysis.COMPOSITES
             or product in ("landcover", "vessels", "classes", "forest_change"))
    if not known:
        raise ValueError(f"Unknown product: {product}")
    if product == "forest_change" and not image_b:
        raise ValueError("Forest change needs a second image (image B)")
    if image_b and product not in analysis.INDICES and product not in ("vessels", "forest_change"):
        raise ValueError("Change detection needs an index product (ndvi, ndwi, ndbi, nbr) or forest change")


# ---- images ---------------------------------------------------------------------------------

@app.get("/api/products")
def products():
    ramp = lambda d: {k: d[k] for k in ("vmin", "mid", "vmax", "stops")}
    return {
        "composites": [{"id": k, "label": v["label"]} for k, v in analysis.COMPOSITES.items()],
        "indices": [{"id": k, "label": v["label"], **ramp(v)} for k, v in analysis.INDICES.items()],
        "change": ramp(analysis.CHANGE),
    }


@app.post("/api/open")
def open_image(paths: list[str] = Body(embed=True)):
    return local.open_image(paths).info()


@app.get("/api/images")
def images():
    return {"images": [image.info() for image in local.all_images()]}


@app.post("/api/images/{image_id}/bands")
def set_bands(image_id: str, mapping: dict = Body(), pixel_m: float | None = Body(None)):
    image = local.get(image_id)
    for role, index in mapping.items():
        if role not in local.ROLES or not (index is None or 0 <= int(index) < len(image.sources)):
            raise ValueError(f"Invalid band assignment for {role}")
    image.mapping = {role: (None if mapping.get(role) is None else int(mapping[role])) for role in local.ROLES}
    if not image.georeferenced:  # a georeferenced file already knows its pixel size
        image.pixel_m = pixel_m if pixel_m and pixel_m > 0 else None
    image.version += 1
    return image.info()


@app.post("/api/images/{image_id}/close")
def close_image(image_id: str):
    local.close(image_id)
    return images()


@app.get("/api/cache")
def cache_info():
    return {"images": len(local.all_images()), "bands_in_memory": local._read_raw.cache_info().currsize}


@app.post("/api/cache/clear")
def cache_clear():
    for cached in (local._read_raw, _landcover, _vessels, _stretch, _classes):
        cached.cache_clear()
    return cache_info()


# ---- products --------------------------------------------------------------------------------

@app.get("/api/render.png")
def render_png(image: str, window: str | None = None, product: str = "true_color", image_b: str | None = None,
               grid: str | None = None, k: int = Query(6, ge=2, le=8),
               min_contrast: float = Query(8, ge=1, le=100), min_length: float = Query(20, ge=1, le=400),
               max_px: int = Query(1024, ge=64, le=2048)):
    """`grid` names another open image whose pixel grid and `window` to draw on, so that two
    images can be laid over each other (pictures and indices only)."""
    _check_product(product, image_b)
    img, other = local.get(image), local.get(image_b) if image_b else None
    if grid and product in ("landcover", "vessels"):
        raise ValueError("Comparing side by side works for pictures and indices")
    view = local.make_view(local.get(grid) if grid else img, window, max_px)
    if product == "vessels":
        objects, _ = _vessels(view, image_b, other.version if other else 0, min_contrast, min_length)
        png = render.annotate(_composite("true_color", img, view), [o["box"] for o in objects])
    elif product in analysis.COMPOSITES:
        png = _composite(product, img, view)
    elif product == "landcover":
        labels, _ = _landcover(view, k)
        png = render.categorical(labels, analysis.LANDCOVER_COLORS)
    elif product == "classes":
        labels, _, _ = _classes(img.id, img.version, view)
        png = render.categorical(labels, [color for _, color, _ in analysis.classifier_classes()], alpha=150)
    elif product == "forest_change":
        change, _ = _forest_change(img, other, view)
        png = render.categorical(change - 1, ["#e34948", "#2a78d6"], alpha=200)  # only loss and gain are painted
    else:
        ramp = analysis.CHANGE if other else analysis.INDICES[product]
        png = render.colorize(_index_layer(product, img, other, view), ramp["vmin"], ramp["mid"], ramp["vmax"], ramp["stops"])
    return Response(png, media_type="image/png", headers=PNG_HEADERS)


@app.get("/api/stats")
def stats(image: str, product: str, window: str | None = None, image_b: str | None = None,
          k: int = Query(6, ge=2, le=8), threshold: float = Query(0.2, gt=0, lt=1),
          max_px: int = Query(1024, ge=64, le=2048)):
    _check_product(product, image_b)
    if product in analysis.COMPOSITES or product == "vessels":
        raise ValueError("Statistics are available for indices and land cover only")
    img, other = local.get(image), local.get(image_b) if image_b else None
    view = local.make_view(img, window, max_px)
    if product == "landcover":
        labels, classes = _landcover(view, k)
        valid = int(np.count_nonzero(labels >= 0))
        return {"product": product, "area_unit": _unit(view), "classes": classes, "histogram": None,
                "total_km2": round(labels.size * _area(view) / 1e6, 4),
                "valid_km2": round(valid * _area(view) / 1e6, 4),
                "valid_pct": round(100 * valid / labels.size, 1)}
    if product == "classes":
        labels, confidence, variant = _classes(img.id, img.version, view)
        valid = int(np.count_nonzero(labels >= 0))
        layers = [(region, color, labels == cls) for cls, (_, color, region) in enumerate(analysis.classifier_classes())]
        return {"product": product, "area_unit": _unit(view), "histogram": None,
                "classes": analysis.class_summary(labels, confidence, _area(view)),
                "regions": _to_image(view, analysis.outline_regions(layers, _area(view), labels.size)),
                "model": {"variant": variant, **analysis.load_classifier()["report"][variant]},
                "valid_km2": round(valid * _area(view) / 1e6, 4), "valid_pct": round(100 * valid / labels.size, 1)}
    if product == "forest_change":
        change, rows = _forest_change(img, other, view)
        valid = int(np.count_nonzero(change >= 0))
        layers = [("Forest loss", "#e34948", change == 1), ("Forest gain", "#2a78d6", change == 2)]
        return {"product": product, "area_unit": _unit(view), "histogram": None, "classes": rows,
                "regions": _to_image(view, analysis.outline_regions(layers, _area(view), change.size, min_share=0.0005)),
                "valid_km2": round(valid * _area(view) / 1e6, 4), "valid_pct": round(100 * valid / change.size, 1)}
    layer = _index_layer(product, img, other, view)
    classes = analysis.change_classes(threshold) if other else analysis.INDICES[product]["classes"]
    return {"product": product, "change": bool(other), "area_unit": _unit(view),
            **analysis.summarize(layer, _area(view), classes)}


@app.get("/api/detect")
def detect(image: str, window: str | None = None, image_b: str | None = None,
           min_contrast: float = Query(8, ge=1, le=100), min_length: float = Query(20, ge=1, le=400),
           max_px: int = Query(2048, ge=64, le=2048)):
    img, other = local.get(image), local.get(image_b) if image_b else None
    view = local.make_view(img, window, max_px)
    objects, summary = _vessels(view, image_b, other.version if other else 0, min_contrast, min_length)
    return {"product": "vessels", "count": len(objects), **summary, "area_unit": "km²",
            "pixel_m": round(view.pixel_area_m2 ** 0.5, 2), "classes": analysis.vessel_size_classes(objects),
            "objects": [{k: v for k, v in o.items() if k != "box"} for o in objects]}


@app.get("/api/point")
def point(image: str, product: str, col: float, row: float, window: str | None = None,
          image_b: str | None = None, max_px: int = Query(1024, ge=64, le=2048)):
    if product not in analysis.INDICES:
        raise ValueError("Point values are available for index products only")
    img, other = local.get(image), local.get(image_b) if image_b else None
    view = local.make_view(img, window, max_px)
    x, y = int((col - view.window[0]) / view.step), int((row - view.window[1]) / view.step)
    if not (0 <= x < view.width and 0 <= y < view.height):
        raise ValueError("Point is outside the area")
    value = float(_index_layer(product, img, other, view)[y, x])
    return {"value": None if np.isnan(value) else round(value, 4)}


@app.get("/api/export.tif")
def export_tif(image: str, product: str, window: str | None = None, image_b: str | None = None,
               k: int = Query(6, ge=2, le=8), max_px: int = Query(4096, ge=64, le=8192)):
    _check_product(product, image_b)
    img, other = local.get(image), local.get(image_b) if image_b else None
    view = local.make_view(img, window, max_px)
    if product in analysis.COMPOSITES:
        names = list(analysis.COMPOSITES[product]["bands"])
        scene = local.read_roles(img, tuple(names), view, COMPOSITE_PURPOSE[product])
        layers = [scene[b] for b in names]
    elif product == "vessels":
        raise ValueError("Vessel detections have no raster export; save the map image instead")
    elif product == "landcover":
        labels, _ = _landcover(view, k)
        layers, names = [np.where(labels >= 0, labels, np.nan)], ["landcover_class"]
    elif product == "classes":
        labels, confidence, _ = _classes(img.id, img.version, view)
        layers = [np.where(labels >= 0, labels, np.nan), np.where(labels >= 0, confidence, np.nan)]
        names = ["class: " + ", ".join(f"{i}={name}" for i, (name, _, _) in enumerate(analysis.classifier_classes())), "confidence"]
    elif product == "forest_change":
        change, _ = _forest_change(img, other, view)
        layers, names = [np.where(change >= 0, change, np.nan)], ["forest change: 0=none, 1=loss, 2=gain"]
    else:
        layers = [_index_layer(product, img, other, view)]
        names = [f"{product}_change" if other else product]
    crs = img.crs if img.georeferenced else None
    data = render.geotiff(layers, view.width, view.height, crs, local.view_transform(img, view), names)
    return Response(data, media_type="image/tiff")
