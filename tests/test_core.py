import io

import numpy as np
import pytest
import rasterio
from PIL import Image

from rasterio.transform import from_origin

from satimg import analysis, local, render


def test_normalized_difference_handles_nodata_and_zero_sum():
    a = np.array([0.5, 0.0, np.nan, 0.1], dtype=np.float32)
    b = np.array([0.1, 0.0, 0.2, 0.3], dtype=np.float32)
    out = analysis.normalized_difference(a, b)
    assert out[0] == pytest.approx(0.6667, abs=1e-3)
    assert np.isnan(out[1]) and np.isnan(out[2])
    assert out[3] == pytest.approx(-0.5)


def test_summarize_class_areas():
    values = np.array([[-0.5, 0.1], [0.3, 0.8], [np.nan, 0.9]], dtype=np.float32)
    s = analysis.summarize(values, 1e6, analysis.INDICES["ndvi"]["classes"])
    assert s["total_km2"] == 6 and s["valid_km2"] == 5
    assert [c["km2"] for c in s["classes"]] == [1, 1, 1, 2]
    assert sum(s["histogram"]["km2"]) == 5
    assert all(type(c["pct"]) is float for c in s["classes"])


def test_summarize_all_nodata():
    s = analysis.summarize(np.full((2, 2), np.nan), 100.0, analysis.INDICES["ndvi"]["classes"])
    assert s["valid_pct"] == 0 and s["classes"] == []


def test_change_classes_cover_full_range():
    diff = np.array([-2.0, -0.3, 0.0, 0.3, 2.0])
    s = analysis.summarize(diff, 1e6, analysis.change_classes(0.2))
    assert [c["km2"] for c in s["classes"]] == [2, 1, 2]


def test_landcover_separates_water_and_vegetation():
    water = dict(blue=0.06, green=0.07, red=0.05, nir=0.02, swir16=0.01, swir22=0.01)
    forest = dict(blue=0.03, green=0.06, red=0.04, nir=0.40, swir16=0.18, swir22=0.08)
    rng = np.random.default_rng(1)
    scene = {}
    for band in analysis.LANDCOVER_BANDS:
        arr = np.empty((20, 20), dtype=np.float32)
        arr[:, :10], arr[:, 10:] = water[band], forest[band]
        scene[band] = arr + rng.normal(0, 0.002, arr.shape).astype(np.float32)
    scene["blue"][0, 0] = np.nan
    labels, info = analysis.landcover(scene, 100.0, k=2)
    assert labels[0, 0] == -1
    assert [c["label"] for c in info] == ["Water", "Dense vegetation"]
    assert (labels[1:, :10] == 0).all() and (labels[:, 10:] == 1).all()


def test_colorize_neutral_midpoint_and_transparency():
    values = np.array([[0.0, np.nan, 0.9]], dtype=np.float32)
    ramp = analysis.INDICES["ndvi"]
    png = render.colorize(values, ramp["vmin"], ramp["mid"], ramp["vmax"], ramp["stops"])
    px = np.array(Image.open(io.BytesIO(png)))
    assert tuple(px[0, 0]) == (0xF0, 0xEF, 0xEC, 255)
    assert px[0, 1, 3] == 0
    assert tuple(px[0, 2]) == (0x00, 0x63, 0x00, 255)


def _sea_scene(shape=(120, 120)):
    """Dark open water with a strip of land along the top edge."""
    rng = np.random.default_rng(2)
    water = dict(red=0.03, green=0.05, blue=0.06, nir=0.02)
    land = dict(red=0.12, green=0.10, blue=0.08, nir=0.30)
    scene = {}
    for band in analysis.VESSEL_BANDS:
        arr = np.full(shape, water[band], dtype=np.float32) + rng.normal(0, 0.001, shape).astype(np.float32)
        arr[:10, :] = land[band]
        scene[band] = arr
    return scene


def _paint(scene, rows, cols, value=0.25):
    for band in analysis.VESSEL_BANDS:
        scene[band][rows, cols] = value


def test_vessels_found_on_open_water_but_not_piers():
    scene = _sea_scene()
    _paint(scene, slice(60, 63), slice(40, 55))   # 150 m x 30 m ship
    _paint(scene, slice(90, 92), slice(90, 94))   # 40 m boat
    _paint(scene, slice(10, 40), slice(100, 103))  # pier attached to the land
    objects, summary = analysis.detect_vessels(scene, pixel_m=10.0)
    assert [o["length_m"] for o in objects] == pytest.approx([150, 40], abs=12)
    assert objects[0]["row"] == pytest.approx(61.5, abs=0.6) and objects[0]["col"] == pytest.approx(47.5, abs=0.6)
    assert summary["water_km2"] > 1.0


def test_vessels_fixed_objects_removed_with_second_date():
    scene, other = _sea_scene(), _sea_scene()
    _paint(scene, slice(60, 63), slice(40, 55))   # ship, only on the first date
    for s in (scene, other):
        _paint(s, slice(85, 89), slice(80, 84))   # islet, on both dates
    assert len(analysis.detect_vessels(scene, 10.0)[0]) == 2
    objects, summary = analysis.detect_vessels(scene, 10.0, other_scene=other)
    assert len(objects) == 1 and summary["removed_fixed"] == 1


def test_vessels_none_without_water():
    scene = {b: np.full((50, 50), 0.2, dtype=np.float32) for b in analysis.VESSEL_BANDS}
    assert analysis.detect_vessels(scene, 10.0) == ([], {"water_km2": 0.0, "candidates": 0, "removed_fixed": 0})

# ---- local image files ----------------------------------------------------------------------

def _write_tif(path, data, descriptions=None, georeferenced=True, dtype="uint16"):
    extra = dict(crs="EPSG:32652", transform=from_origin(500000, 4000000, 10, 10)) if georeferenced else {}
    with rasterio.open(path, "w", driver="GTiff", count=data.shape[0], height=data.shape[1], width=data.shape[2],
                       dtype=dtype, **extra) as dst:
        dst.write(data.astype(dtype))
        for i, name in enumerate(descriptions or [], start=1):
            dst.set_band_description(i, name)


def test_band_guessing():
    assert local.guess_mapping(["B02", "B03", "B04", "B08", "B11", "B12"]) == dict(blue=0, green=1, red=2, nir=3, swir16=4, swir22=5)
    assert local.guess_mapping(["T52SED_B08", "T52SED_B04"])["nir"] == 0
    assert local.guess_mapping(["Band 1", "Band 2", "Band 3"]) == dict(red=0, green=1, blue=2, nir=None, swir16=None, swir22=None)
    assert local.guess_mapping(["Band 1"] * 4)["nir"] == 3
    assert local.guess_mapping(["B4", "B5"], "LC09_L2SP_115034")["nir"] == 1
    assert local.guess_mapping(["rededge"])["red"] == 0  # single band shown as grey, not matched as "red"


def test_open_read_and_window(tmp_path):
    data = np.zeros((4, 60, 80), dtype=np.uint16)
    data[0], data[1], data[2], data[3] = 500, 800, 600, 3000   # B, G, R, NIR -> NDVI = 2400/3600
    data[:, :5, :] = 0                                           # swath edge: no data
    _write_tif(tmp_path / "scene.tif", data, ["B02", "B03", "B04", "B08"])
    image = local.open_image([str(tmp_path / "scene.tif")])
    assert image.georeferenced and image.pixel_m == pytest.approx(10)
    view = local.make_view(image, "10,0,50,40", 1024)
    assert (view.width, view.height) == (40, 40) and view.pixel_area_m2 == pytest.approx(100)
    scene = local.read_roles(image, ("nir", "red"), view)
    ndvi = analysis.compute_index("ndvi", scene)
    assert np.isnan(ndvi[:5]).all() and ndvi[5:] == pytest.approx(2400 / 3600)
    with pytest.raises(ValueError, match="short-wave"):
        local.read_roles(image, ("swir16",), view, "NDBI")
    assert local.lonlat_of(image, 0, 0) == pytest.approx((129.0, 36.14), abs=0.01)


def test_large_view_is_downsampled(tmp_path):
    _write_tif(tmp_path / "big.tif", np.ones((3, 400, 300)), dtype="uint8", georeferenced=False)
    image = local.open_image([str(tmp_path / "big.tif")])
    assert not image.georeferenced and image.pixel_m is None
    view = local.make_view(image, None, 100)
    assert (view.width, view.height) == (75, 100) and view.step == pytest.approx(4) and view.pixel_area_m2 is None
    assert local.read_roles(image, ("red", "green", "blue"), view)["red"] == pytest.approx(1 / 255)


def test_single_band_files_and_second_date_alignment(tmp_path):
    for name, value in (("T52_B04", 600), ("T52_B08", 3000)):
        _write_tif(tmp_path / f"{name}.tif", np.full((1, 40, 40), value))
    first = local.open_image([str(tmp_path / "T52_B04.tif"), str(tmp_path / "T52_B08.tif")])
    assert first.mapping["red"] == 0 and first.mapping["nir"] == 1
    # A second date on a shifted, coarser grid is resampled onto the first image's grid.
    with rasterio.open(tmp_path / "later.tif", "w", driver="GTiff", count=2, height=30, width=30, dtype="uint16",
                       crs="EPSG:32652", transform=from_origin(499900, 4000100, 20, 20)) as dst:
        dst.write(np.stack([np.full((30, 30), 1500), np.full((30, 30), 1500)]).astype("uint16"))
        dst.set_band_description(1, "B04")
        dst.set_band_description(2, "B08")
    second = local.open_image([str(tmp_path / "later.tif")])
    view = local.make_view(first, None, 1024)
    red = local.read_roles(second, ("red",), view)["red"]
    assert red.shape == (40, 40) and red == pytest.approx(0.15)


def test_geotiff_round_trip():
    layer = np.arange(12, dtype=np.float32).reshape(3, 4)
    data = render.geotiff([layer], 4, 3, "EPSG:32652", from_origin(500000, 4000000, 10, 10), ["ndvi"])
    with rasterio.open(io.BytesIO(data)) as src:
        assert src.crs.to_epsg() == 32652 and src.descriptions == ("ndvi",)
        assert np.array_equal(src.read(1), layer)


def test_landcover_without_infrared_numbers_the_classes():
    rng = np.random.default_rng(3)
    scene = {b: np.where(np.arange(400).reshape(20, 20) % 20 < 10, 0.1, 0.6).astype(np.float32)
             + rng.normal(0, 0.002, (20, 20)).astype(np.float32) for b in ("red", "green", "blue")}
    labels, info = analysis.landcover(scene, 100.0, k=2)
    assert [c["label"] for c in info] == ["Class 1", "Class 2"] and info[0]["mean_ndvi"] is None
    assert (labels[:, :10] == 0).all() and (labels[:, 10:] == 1).all()

# ---- named land cover -------------------------------------------------------------------------

def _two_class_scene():
    """Left half deep water, right half dense forest, in Sentinel-2-like reflectance."""
    water = dict(blue=0.05, green=0.05, red=0.03, nir=0.02, swir16=0.01, swir22=0.01)
    forest = dict(blue=0.02, green=0.04, red=0.03, nir=0.35, swir16=0.15, swir22=0.06)
    rng = np.random.default_rng(4)
    scene = {}
    for band in analysis.LANDCOVER_BANDS:
        arr = np.empty((40, 60), dtype=np.float32)
        arr[:, :30], arr[:, 30:] = water[band], forest[band]
        scene[band] = arr + rng.normal(0, 0.002, arr.shape).astype(np.float32)
    return scene


def test_classifier_names_water_and_forest():
    scene = _two_class_scene()
    scene["blue"][0, 0] = np.nan
    labels, confidence, variant = analysis.classify(scene)
    names = [name for name, _, _ in analysis.classifier_classes()]
    assert names[analysis.FOREST] == "Forest"
    assert variant == "6band" and labels[0, 0] == -1
    assert names[int(np.bincount(labels[2:, :28].ravel()).argmax())] == "Water"
    assert names[int(np.bincount(labels[:, 32:].ravel()).argmax())] == "Forest"
    summary = analysis.class_summary(labels, confidence, 400.0)
    assert sum(c["pct"] for c in summary) == pytest.approx(100, abs=0.5)
    assert {c["label"]: c["count"] for c in summary}["Water"] == 1


def test_classifier_picks_variant_by_bands():
    scene = _two_class_scene()
    assert analysis.classify({b: scene[b] for b in ("blue", "green", "red", "nir")})[2] == "4band"
    assert analysis.classify({b: scene[b] for b in ("red", "green", "blue")})[2] == "rgb"
    with pytest.raises(ValueError):
        analysis.classify({"nir": scene["nir"]})


def test_region_outline_and_forest_change():
    labels_a = np.full((40, 60), 3, dtype=np.int16)   # urban everywhere ...
    labels_a[10:30, 10:40] = analysis.FOREST          # ... except one forest block
    labels_b = labels_a.copy()
    labels_b[10:30, 10:25] = 3                        # half of the forest is gone on the second date
    regions = analysis.outline_regions([("Forest area", "#1f8a3b", labels_a == analysis.FOREST)], 100.0, labels_a.size)
    assert len(regions) == 1 and regions[0]["km2"] == pytest.approx(600 * 100 / 1e6)
    xs, ys = zip(*regions[0]["polygon"])
    assert (min(xs), max(xs), min(ys), max(ys)) == pytest.approx((10, 40, 10, 30), abs=1.5)
    assert 10 < regions[0]["anchor"][0] < 40 and 10 < regions[0]["anchor"][1] < 30
    change, rows = analysis.forest_change(labels_a, labels_b, 100.0)
    by_name = {r["label"]: r["km2"] for r in rows}
    assert by_name["Forest loss"] == pytest.approx(300 * 100 / 1e6) and by_name["Forest gain"] == 0
    assert (change[10:30, 10:25] == 1).all() and (change[10:30, 25:40] == 0).all()