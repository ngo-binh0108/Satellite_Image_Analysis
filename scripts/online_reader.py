"""Read only the requested area of a scene from its cloud-optimized GeoTIFFs.

Everything is resampled onto one Web Mercator grid per request so layers line up
with the web map and with each other (10 m and 20 m bands, different dates).
"""
import math
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from functools import lru_cache

import numpy as np
import rasterio
from pyproj import Transformer
from rasterio.enums import Resampling
from rasterio.transform import from_bounds
from rasterio.vrt import WarpedVRT

GDAL_ENV = dict(
    GDAL_DISABLE_READDIR_ON_OPEN="EMPTY_DIR",
    CPL_VSIL_CURL_ALLOWED_EXTENSIONS=".tif",
    GDAL_HTTP_MERGE_CONSECUTIVE_RANGES="YES",
    GDAL_HTTP_MULTIPLEX="YES",
    GDAL_HTTP_MAX_RETRY="3",
    GDAL_HTTP_RETRY_DELAY="1",
    VSI_CACHE="TRUE",
)
NATIVE_RES_M = 10.0
MAX_SIDE_KM = 120.0
# Scene classification values treated as unusable: no data, saturated,
# cloud shadow, cloud (medium/high probability), thin cirrus.
SCL_BAD = (0, 1, 3, 8, 9, 10)

_to_mercator = Transformer.from_crs("EPSG:4326", "EPSG:3857", always_xy=True)
_to_lonlat = Transformer.from_crs("EPSG:3857", "EPSG:4326", always_xy=True)


@dataclass(frozen=True)
class Grid:
    bbox: tuple  # west, south, east, north in degrees
    bounds: tuple  # same corners in EPSG:3857 metres
    width: int
    height: int
    pixel_area_m2: float  # ground area, corrected for Mercator scale

    @property
    def transform(self):
        return from_bounds(*self.bounds, self.width, self.height)

    def pixel_of(self, lon: float, lat: float) -> tuple[int, int] | None:
        x, y = _to_mercator.transform(lon, lat)
        col, row = ~self.transform * (x, y)
        col, row = math.floor(col), math.floor(row)
        if 0 <= row < self.height and 0 <= col < self.width:
            return row, col
        return None


def lonlat_of(grid: Grid, row: float, col: float) -> tuple[float, float]:
    """Longitude and latitude of a (possibly fractional) pixel position; 0.5 is a pixel centre."""
    x, y = grid.transform * (col, row)
    lon, lat = _to_lonlat.transform(x, y)
    return round(lon, 6), round(lat, 6)


def make_grid(bbox: tuple, max_px: int = 1024) -> Grid:
    west, south, east, north = bbox
    if not (west < east and south < north and -84 <= south and north <= 84):
        raise ValueError("Area must be west<east, south<north, within ±84° latitude")
    x0, y0 = _to_mercator.transform(west, south)
    x1, y1 = _to_mercator.transform(east, north)
    k = math.cos(math.radians((south + north) / 2))  # ground metres per Mercator metre
    if max(x1 - x0, y1 - y0) * k > MAX_SIDE_KM * 1000:
        raise ValueError(f"Area is too large; keep each side under {MAX_SIDE_KM:.0f} km")
    res = max(NATIVE_RES_M / k, max(x1 - x0, y1 - y0) / max_px)
    width = max(1, round((x1 - x0) / res))
    height = max(1, round((y1 - y0) / res))
    pixel_area = ((x1 - x0) / width * k) * ((y1 - y0) / height * k)
    return Grid(tuple(bbox), (x0, y0, x1, y1), width, height, pixel_area)


@lru_cache(maxsize=96)
def _read_asset(href: str, grid: Grid, resampling: str) -> np.ndarray:
    with rasterio.Env(**GDAL_ENV):
        with rasterio.open(href) as src:
            with WarpedVRT(src, crs="EPSG:3857", transform=grid.transform,
                           width=grid.width, height=grid.height,
                           resampling=Resampling[resampling]) as vrt:
                data = vrt.read(1)
    data.flags.writeable = False  # shared through the cache
    return data


def _scale_offset(item: dict, key: str) -> tuple[float, float]:
    """DN to surface reflectance. Earth Search already shifted new-baseline pixel
    values when boa_offset_applied is set, so the advertised offset must be skipped."""
    band = (item["assets"][key].get("raster:bands") or [{}])[0]
    scale = band.get("scale", 0.0001)
    offset = 0.0 if item["properties"].get("earthsearch:boa_offset_applied") else band.get("offset", 0.0)
    return scale, offset


def read_scene(item: dict, keys: tuple, grid: Grid, mask_clouds: bool = True) -> dict[str, np.ndarray]:
    """Reflectance arrays (float32, NaN where there is no usable data) per band key."""
    jobs = [(item["assets"][k]["href"], grid, "bilinear") for k in keys]
    jobs.append((item["assets"]["scl"]["href"], grid, "nearest"))
    with ThreadPoolExecutor(max_workers=len(jobs)) as pool:
        *raw, scl = pool.map(lambda j: _read_asset(*j), jobs)

    bad = np.isin(scl, SCL_BAD) if mask_clouds else scl == 0
    for dn in raw:
        bad = bad | (dn == 0)
    out = {}
    for key, dn in zip(keys, raw):
        scale, offset = _scale_offset(item, key)
        refl = dn.astype(np.float32) * scale + offset
        refl[bad] = np.nan
        out[key] = refl
    return out
