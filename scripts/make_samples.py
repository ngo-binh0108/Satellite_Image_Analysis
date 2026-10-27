"""Developer tool (needs internet): save real Sentinel-2 crops as local test images in samples/.

The app itself never goes online; this only prepares files to open with it.
Run from the project root:  .conda-env\\python.exe scripts\\make_samples.py
"""
import sys
from pathlib import Path

import numpy as np
import rasterio
from PIL import Image
from rasterio.enums import Resampling
from rasterio.warp import transform_bounds
from rasterio.windows import from_bounds

sys.path.insert(0, str(Path(__file__).resolve().parent))
import online_catalog as catalog  # noqa: E402

OUT = Path(__file__).resolve().parent.parent / "samples"
BANDS = (("B02", "blue"), ("B03", "green"), ("B04", "red"), ("B08", "nir"), ("B11", "swir16"), ("B12", "swir22"))
SITES = {"busan": (129.02, 35.02, 129.18, 35.12), "seoul": (126.92, 37.49, 127.06, 37.59)}
ENV = dict(GDAL_DISABLE_READDIR_ON_OPEN="EMPTY_DIR", CPL_VSIL_CURL_ALLOWED_EXTENSIONS=".tif")


def save_scene(name: str, item: dict, bbox: tuple) -> Path:
    arrays, profile = [], None
    with rasterio.Env(**ENV):
        for _, key in BANDS:
            with rasterio.open(item["assets"][key]["href"]) as src:
                window = from_bounds(*transform_bounds("EPSG:4326", src.crs, *bbox), transform=src.transform)
                if profile is None:  # the 10 m blue band defines the grid
                    window = window.round_offsets().round_lengths()
                    shape = (int(window.height), int(window.width))
                    profile = dict(driver="GTiff", dtype="uint16", count=len(BANDS), height=shape[0], width=shape[1],
                                   crs=src.crs, transform=src.window_transform(window), nodata=0,
                                   compress="deflate", tiled=True)
                arrays.append(src.read(1, window=window, out_shape=shape, resampling=Resampling.bilinear))
    path = OUT / f"{name}.tif"
    with rasterio.open(path, "w", **profile) as dst:
        dst.write(np.stack(arrays))
        for i, (band, _) in enumerate(BANDS, start=1):
            dst.set_band_description(i, band)
    return path


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    for site, bbox in SITES.items():
        scenes = [s for s in catalog.search(bbox, "2026-01-01", "2026-10-01", 10, 80) if s["coverage"] > 0.99]
        scenes.sort(key=lambda s: s["cloud_cover"])
        first = scenes[0]
        second = next(s for s in scenes[1:] if abs(int(s["datetime"][5:7]) - int(first["datetime"][5:7])) >= 2)
        for scene in (first, second):
            path = save_scene(f"{site}_{scene['datetime'][:10]}", catalog.get_item(scene["id"]), bbox)
            print(path.name, round(path.stat().st_size / 1e6, 1), "MB")

    # Two more shapes of input: separate single-band files, and a plain picture with no georeferencing.
    source = sorted(OUT.glob("busan_*.tif"))[0]
    (OUT / "single_bands").mkdir(exist_ok=True)
    with rasterio.open(source) as src:
        for index, (band, _) in enumerate(BANDS[:4], start=1):
            with rasterio.open(OUT / "single_bands" / f"T52SED_{band}.tif", "w", **(src.profile | dict(count=1))) as dst:
                dst.write(src.read(index), 1)
        rgb = np.clip(src.read([3, 2, 1]).astype(np.float32) / 3000 * 255, 0, 255).astype(np.uint8)
    Image.fromarray(np.moveaxis(rgb, 0, -1)).save(OUT / "busan_picture.png")
    print("single_bands/, busan_picture.png")
