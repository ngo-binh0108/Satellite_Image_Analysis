"""Open image files from disk and read any part of them as reflectance-like bands.

An image is one multi-band file, or several single-band files (for example the separate
B02/B03/B04/B08 files of a Sentinel-2 download). Everything here works without a network.
"""
import hashlib
import math
import re
import warnings
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path

import numpy as np
import rasterio
from affine import Affine
from rasterio.enums import ColorInterp, Resampling
from rasterio.errors import NotGeoreferencedWarning
from rasterio.vrt import WarpedVRT
from rasterio.warp import transform as warp_points
from rasterio.windows import Window

warnings.filterwarnings("ignore", category=NotGeoreferencedWarning)

ROLES = ("blue", "green", "red", "nir", "swir16", "swir22")
ROLE_NAMES = {"blue": "blue", "green": "green", "red": "red", "nir": "near-infrared",
              "swir16": "short-wave infrared 1 (1.6 µm)", "swir22": "short-wave infrared 2 (2.2 µm)"}
# Band names as written in file names and band descriptions (Sentinel-2 numbering).
_NAME_PATTERNS = {"blue": r"b0?2|blue", "green": r"b0?3|green", "red": r"b0?4|red", "nir": r"b0?8|nir",
                  "swir16": r"b11|swir1|swir16", "swir22": r"b12|swir2|swir22"}
_LANDSAT_PATTERNS = {"blue": r"b2", "green": r"b3", "red": r"b4", "nir": r"b5", "swir16": r"b6", "swir22": r"b7"}
# Band order assumed when nothing names the bands, by band count.
_ORDER_BY_COUNT = {
    3: ("red", "green", "blue"),
    4: ("blue", "green", "red", "nir"),
    6: ("blue", "green", "red", "nir", "swir16", "swir22"),
    12: (None, "blue", "green", "red", None, None, None, "nir", None, None, "swir16", "swir22"),
    13: (None, "blue", "green", "red", None, None, None, "nir", None, None, None, "swir16", "swir22"),
}


@dataclass
class Source:
    path: str
    band: int  # 1-based band number inside the file
    name: str
    scale: float
    offset: float
    nodata: float | None
    zero_is_nodata: bool


@dataclass
class LocalImage:
    id: str
    name: str
    paths: list[str]
    sources: list[Source]
    width: int
    height: int
    crs: object | None
    transform: Affine
    georeferenced: bool
    pixel_m: float | None  # ground size of one pixel; None when unknown
    mapping: dict = field(default_factory=dict)  # role -> index into sources, or None
    version: int = 0  # bumped when bands or pixel size change, so cached results are not reused

    def info(self) -> dict:
        return {"id": self.id, "name": self.name, "paths": self.paths, "width": self.width, "height": self.height,
                "bands": [s.name for s in self.sources], "mapping": self.mapping, "pixel_m": self.pixel_m,
                "georeferenced": self.georeferenced, "crs": self.crs.to_string() if self.crs else None}


@dataclass(frozen=True)
class View:
    """Part of an image (`window`, in that image's pixels) resampled to `width` x `height`."""
    image_id: str
    version: int
    window: tuple  # col0, row0, col1, row1
    width: int
    height: int
    pixel_area_m2: float | None

    @property
    def step(self) -> float:  # image pixels per view pixel
        return (self.window[2] - self.window[0]) / self.width


_images: dict[str, LocalImage] = {}


def get(image_id: str) -> LocalImage:
    if image_id not in _images:
        raise ValueError("That image is not open any more. Open it again.")
    return _images[image_id]


def all_images() -> list[LocalImage]:
    return list(_images.values())


def close(image_id: str | None = None) -> None:
    if image_id is None:
        _images.clear()
    else:
        _images.pop(image_id, None)
    _read_raw.cache_clear()


def _scale_for(ds, band: int) -> tuple[float, float]:
    scale, offset = ds.scales[band - 1], ds.offsets[band - 1]
    if (scale, offset) != (1.0, 0.0):
        return scale, offset
    dtype = np.dtype(ds.dtypes[band - 1])
    if dtype == np.uint8:
        return 1 / 255, 0.0
    if dtype.kind in "ui":
        return 1e-4, 0.0  # the usual surface-reflectance scaling of Sentinel-2 and similar products
    return 1.0, 0.0


def _pixel_size_m(ds) -> float | None:
    if ds.crs is None or ds.transform.is_identity:
        return None
    size = math.sqrt(abs(ds.transform.a * ds.transform.e))
    if ds.crs.is_geographic:
        latitude = (ds.bounds.top + ds.bounds.bottom) / 2
        return size * 111320 * math.sqrt(abs(math.cos(math.radians(latitude))))
    if ds.crs.to_epsg() == 3857:  # Web Mercator stretches distances away from the equator
        _, lats = warp_points(ds.crs, "EPSG:4326", [ds.bounds.left], [(ds.bounds.top + ds.bounds.bottom) / 2])
        return size * math.cos(math.radians(lats[0]))
    return size * (ds.crs.linear_units_factor[1] if ds.crs.is_projected else 1.0)


def guess_mapping(names: list[str], hint: str = "") -> dict:
    """Which band plays which role: from band names when they say so, otherwise from the band count."""
    patterns = _LANDSAT_PATTERNS if re.search(r"l[ceto]0[4-9]", hint.lower()) else _NAME_PATTERNS
    mapping = {role: None for role in ROLES}
    for role, pattern in patterns.items():
        regex = re.compile(rf"(?<![0-9a-z])({pattern})(?![0-9a-z])")
        for index, name in enumerate(names):
            if regex.search(name.lower().replace("_", " ")):
                mapping[role] = index
                break
    if any(v is not None for v in mapping.values()):
        return mapping
    if len(names) == 1:
        return mapping | {"red": 0, "green": 0, "blue": 0}
    order = _ORDER_BY_COUNT.get(len(names), ("blue", "green", "red", "nir") if len(names) >= 4 else ())
    for index, role in enumerate(order):
        if role:
            mapping[role] = index
    return mapping


def _expand(paths: list[str]) -> list[str]:
    """A Sentinel-2 .SAFE folder stands for its 10 m and 20 m band files."""
    if len(paths) == 1 and Path(paths[0]).is_dir():
        found = {}
        for file in sorted(Path(paths[0]).rglob("*.jp2")):
            match = re.search(r"_(B02|B03|B04|B08)_10m|_(B11|B12)_20m", file.name)
            if match:
                found.setdefault(match.group(1) or match.group(2), str(file))
        if not found:
            raise ValueError("No Sentinel-2 band files (B02, B03, B04, B08, B11, B12) were found in that folder")
        return list(found.values())
    return paths


def open_image(paths: list[str]) -> LocalImage:
    paths = _expand([str(Path(p)) for p in paths])
    if not paths:
        raise ValueError("No file given")
    image_id = hashlib.md5("|".join(paths).encode("utf-8")).hexdigest()[:12]
    sources, reference = [], None
    for path in paths:
        try:
            ds = rasterio.open(path)
        except rasterio.errors.RasterioIOError as exc:
            raise ValueError(f"Cannot read '{Path(path).name}' as an image: {exc}") from None
        with ds:
            if len(paths) > 1 and ds.count > 1:
                raise ValueError("Select either one multi-band file, or several single-band files")
            if reference is None or abs(ds.transform.a) < abs(reference["transform"].a):
                reference = dict(width=ds.width, height=ds.height, crs=ds.crs, transform=ds.transform,
                                 georeferenced=ds.crs is not None and not ds.transform.is_identity,
                                 pixel_m=_pixel_size_m(ds))
            interp = ds.colorinterp
            for band in range(1, ds.count + 1):
                if interp[band - 1] == ColorInterp.alpha:
                    continue
                if len(paths) > 1:
                    name = Path(path).stem
                else:
                    colour = interp[band - 1].name if interp[band - 1] in (ColorInterp.red, ColorInterp.green, ColorInterp.blue) else ""
                    name = ds.descriptions[band - 1] or colour or f"Band {band}"
                scale, offset = _scale_for(ds, band)
                dtype = np.dtype(ds.dtypes[band - 1])
                sources.append(Source(path, band, name, scale, offset, ds.nodata,
                                      zero_is_nodata=ds.nodata is None and dtype.kind in "ui" and dtype.itemsize > 1))
    name = Path(paths[0]).name if len(paths) == 1 else f"{Path(paths[0]).parent.name} ({len(paths)} bands)"
    image = LocalImage(image_id, name, paths, sources, mapping=guess_mapping([s.name for s in sources], name), **reference)
    _images[image_id] = image
    return image


def make_view(image: LocalImage, window: str | None, max_px: int) -> View:
    if window:
        try:
            c0, r0, c1, r1 = (int(round(float(v))) for v in window.split(","))
        except ValueError:
            raise ValueError("window must be 'col0,row0,col1,row1'") from None
    else:
        c0, r0, c1, r1 = 0, 0, image.width, image.height
    c0, c1 = max(0, min(c0, c1)), min(image.width, max(c0, c1))
    r0, r1 = max(0, min(r0, r1)), min(image.height, max(r0, r1))
    if c1 - c0 < 2 or r1 - r0 < 2:
        raise ValueError("The selected area is outside the image or too small")
    step = max(1.0, max(c1 - c0, r1 - r0) / max_px)
    width, height = max(1, round((c1 - c0) / step)), max(1, round((r1 - r0) / step))
    area = (image.pixel_m * (c1 - c0) / width) * (image.pixel_m * (r1 - r0) / height) if image.pixel_m else None
    return View(image.id, image.version, (c0, r0, c1, r1), width, height, area)


def view_transform(image: LocalImage, view: View) -> Affine:
    c0, r0, c1, r1 = view.window
    return image.transform * Affine.translation(c0, r0) * Affine.scale((c1 - c0) / view.width, (r1 - r0) / view.height)


@lru_cache(maxsize=160)
def _read_raw(path: str, band: int, window: tuple | None, width: int, height: int, warp: tuple | None) -> np.ndarray:
    """One band resampled to width x height: a pixel window of the file, or warped onto another grid."""
    with rasterio.open(path) as src:
        if warp is None:
            c0, r0, c1, r1 = window
            data = src.read(band, window=Window(c0, r0, c1 - c0, r1 - r0), out_shape=(height, width),
                            resampling=Resampling.bilinear, boundless=True)
        else:
            crs_wkt, coefficients = warp
            with WarpedVRT(src, crs=crs_wkt, transform=Affine(*coefficients), width=width, height=height,
                           resampling=Resampling.bilinear) as vrt:
                data = vrt.read(band)
    data.flags.writeable = False  # shared through the cache
    return data


def read_roles(image: LocalImage, roles: tuple, view: View, purpose: str = "This analysis") -> dict[str, np.ndarray]:
    """Bands by role as float32 with NaN where there is no data. `view` may belong to another
    image: the result is then aligned to that image's grid, so two dates can be compared."""
    grid = get(view.image_id)
    missing = [ROLE_NAMES[r] for r in roles if image.mapping.get(r) is None]
    if missing:
        raise ValueError(f"{purpose} needs a {' and a '.join(missing)} band, which '{image.name}' does not have "
                         "assigned. Use Bands… to assign it if the image contains one.")
    raw = {}
    for role in roles:
        source = image.sources[image.mapping[role]]
        with rasterio.open(source.path) as probe:
            same_grid = image is grid and (probe.width, probe.height) == (grid.width, grid.height)
            size = (probe.width, probe.height)
        if same_grid:
            data = _read_raw(source.path, source.band, view.window, view.width, view.height, None)
        elif image.georeferenced and grid.georeferenced:
            warp = (grid.crs.to_wkt(), tuple(view_transform(grid, view))[:6])
            data = _read_raw(source.path, source.band, None, view.width, view.height, warp)
        else:
            if abs(size[0] / size[1] - grid.width / grid.height) > 0.01:
                raise ValueError(f"'{image.name}' and '{grid.name}' have different shapes and no georeferencing, "
                                 "so they cannot be lined up")
            fx, fy = size[0] / grid.width, size[1] / grid.height
            c0, r0, c1, r1 = view.window
            window = (round(c0 * fx), round(r0 * fy), round(c1 * fx), round(r1 * fy))
            data = _read_raw(source.path, source.band, window, view.width, view.height, None)
        raw[role] = (data, source)

    # Integer products mark missing pixels with zeros in every band (the edge of a satellite swath).
    empty = np.all([data == 0 for data, source in raw.values() if source.zero_is_nodata] or [np.zeros((view.height, view.width), bool)], axis=0)
    out = {}
    for role, (data, source) in raw.items():
        values = data.astype(np.float32) * source.scale + source.offset
        bad = empty if source.nodata is None else empty | (data == source.nodata)
        if np.issubdtype(data.dtype, np.floating):
            bad = bad | ~np.isfinite(data)
        values[bad] = np.nan
        out[role] = values
    return out


def lonlat_of(image: LocalImage, col: float, row: float) -> tuple[float, float] | None:
    if not image.georeferenced:
        return None
    x, y = image.transform * (col, row)
    lons, lats = warp_points(image.crs, "EPSG:4326", [x], [y])
    return round(lons[0], 6), round(lats[0], 6)
