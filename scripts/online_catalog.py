"""Sentinel-2 L2A scene search against the Earth Search STAC API (public, no login)."""
import re
from functools import lru_cache

import httpx
from shapely.geometry import box, shape

STAC_URL = "https://earth-search.aws.element84.com/v1"
COLLECTION = "sentinel-2-l2a"
_ITEM_ID = re.compile(r"^[A-Za-z0-9_\-]+$")


def _coverage(item: dict, bbox: tuple) -> float:
    """Fraction of the bbox covered by the scene footprint (tiles often clip an area)."""
    aoi = box(*bbox)
    try:
        return shape(item["geometry"]).intersection(aoi).area / aoi.area
    except Exception:
        return 0.0


def search(bbox: tuple, start: str, end: str, max_cloud: float = 30, limit: int = 30) -> list[dict]:
    body = {
        "collections": [COLLECTION],
        "bbox": list(bbox),
        "datetime": f"{start}T00:00:00Z/{end}T23:59:59Z",
        "limit": limit,
        "query": {"eo:cloud_cover": {"lte": max_cloud}},
        "sortby": [{"field": "properties.datetime", "direction": "desc"}],
    }
    r = httpx.post(f"{STAC_URL}/search", json=body, timeout=30)
    r.raise_for_status()
    scenes = []
    for item in r.json()["features"]:
        props = item["properties"]
        scenes.append({
            "id": item["id"],
            "datetime": props["datetime"],
            "cloud_cover": round(props.get("eo:cloud_cover", 0), 1),
            "platform": props.get("platform", ""),
            "tile": props.get("grid:code", ""),
            "coverage": round(_coverage(item, bbox), 3),
            "thumbnail": item["assets"].get("thumbnail", {}).get("href"),
        })
    return scenes


@lru_cache(maxsize=256)
def get_item(item_id: str) -> dict:
    if not _ITEM_ID.match(item_id):
        raise ValueError(f"Invalid scene id: {item_id!r}")
    r = httpx.get(f"{STAC_URL}/collections/{COLLECTION}/items/{item_id}", timeout=30)
    if r.status_code == 404:
        raise ValueError(f"Scene not found: {item_id}")
    r.raise_for_status()
    return r.json()
