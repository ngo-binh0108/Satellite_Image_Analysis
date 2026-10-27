"""Turn arrays into map images (PNG) and georeferenced exports (GeoTIFF)."""
import io

import numpy as np
from PIL import Image
from rasterio.io import MemoryFile


def _rgb(hex_color: str) -> tuple:
    return tuple(int(hex_color[i:i + 2], 16) for i in (1, 3, 5))


def _png(rgb: np.ndarray, visible: np.ndarray, alpha: int = 255) -> bytes:
    rgba = np.dstack([rgb, np.where(visible, alpha, 0)]).astype(np.uint8)
    buf = io.BytesIO()
    Image.fromarray(rgba, "RGBA").save(buf, "PNG")
    return buf.getvalue()


def colorize(values: np.ndarray, vmin: float, mid: float, vmax: float, stops) -> bytes:
    """Diverging colour ramp with the neutral stop pinned at `mid`; NaN is transparent."""
    visible = np.isfinite(values)
    v = np.where(visible, values, mid)
    t = np.where(v < mid, 0.5 * (v - vmin) / (mid - vmin), 0.5 + 0.5 * (v - mid) / (vmax - mid))
    t = np.clip(t, 0, 1)
    colors = np.array([_rgb(s) for s in stops], dtype=np.float32)
    rgb = np.stack([np.interp(t, [0, 0.5, 1], colors[:, c]) for c in range(3)], axis=-1)
    return _png(rgb, visible)


def composite(bands: list[np.ndarray], max_reflectance: float = 0.3, gamma: float = 1.6) -> bytes:
    """Fixed stretch (not per-image) so two dates stay visually comparable."""
    stack = np.stack(bands, axis=-1)
    visible = np.isfinite(stack).all(axis=-1)
    scaled = np.clip(np.nan_to_num(stack) / max_reflectance, 0, 1) ** (1 / gamma)
    return _png(scaled * 255, visible)


def categorical(labels: np.ndarray, colors, alpha: int = 255) -> bytes:
    """Negative labels are transparent; `alpha` below 255 lets the picture underneath show through."""
    palette = np.array([_rgb(c) for c in colors], dtype=np.uint8)
    visible = labels >= 0
    return _png(palette[np.where(visible, labels, 0)], visible, alpha)


def geotiff(layers: list[np.ndarray], width: int, height: int, crs, transform, names: list[str]) -> bytes:
    """Float32 GeoTIFF; `crs` is None for images without georeferencing (a plain TIFF results)."""
    data = np.stack(layers).astype(np.float32)
    profile = dict(driver="GTiff", dtype="float32", count=len(layers), width=width, height=height,
                   crs=crs, transform=transform, nodata=float("nan"), compress="deflate", tiled=True)
    with MemoryFile() as mem:
        with mem.open(**profile) as dst:
            dst.write(data)
            for i, name in enumerate(names, start=1):
                dst.set_band_description(i, name)
        return mem.read()


def annotate(png: bytes, boxes: list, color=(255, 210, 63), pad: int = 4) -> bytes:
    """Outline pixel boxes (row0, col0, row1, col1) on an existing PNG."""
    from PIL import ImageDraw
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    draw = ImageDraw.Draw(image)
    for row0, col0, row1, col1 in boxes:
        draw.rectangle([col0 - pad, row0 - pad, col1 + pad - 1, row1 + pad - 1], outline=color + (255,), width=2)
    buf = io.BytesIO()
    image.save(buf, "PNG")
    return buf.getvalue()

def composite_stretched(bands: list[np.ndarray], low: list[float], high: list[float], gamma: float = 1.6) -> bytes:
    """Colour picture with a per-band linear stretch supplied by the caller, so that every
    part of one image is drawn with the same brightness."""
    stack = np.stack(bands, axis=-1)
    visible = np.isfinite(stack).all(axis=-1)
    span = np.maximum(np.array(high, dtype=np.float32) - np.array(low, dtype=np.float32), 1e-6)
    scaled = np.clip((np.nan_to_num(stack) - np.array(low, dtype=np.float32)) / span, 0, 1) ** (1 / gamma)
    return _png(scaled * 255, visible)