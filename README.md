# EarthView AI – Satellite Image Analysis

[![tests](https://github.com/ngo-binh0108/Satellite_Image_Analysis/actions/workflows/tests.yml/badge.svg)](https://github.com/ngo-binh0108/Satellite_Image_Analysis/actions/workflows/tests.yml)

Desktop app for viewing and analysing satellite or aerial images stored on this computer.
It works fully offline: images are opened from disk and analysed locally, and the app makes no
internet connection. (The window and its analysis engine talk to each other on `127.0.0.1` only.)

## Use

1. **Open image…** – GeoTIFF, JPEG2000, PNG or JPEG. Select one multi-band file, or several single-band
   files (for example Sentinel-2 `B02`, `B03`, `B04`, `B08`) and choose "as the bands of one image".
2. **Bands…** – check which band is red, green, blue, near-infrared and short-wave infrared. The app guesses
   from band names or the band count; correct it here if colours look wrong. For images without
   georeferencing, enter the ground size of a pixel to get areas in km².
3. Optionally draw an analysis area with the rectangle tool; otherwise the whole image is analysed.
4. Pick an analysis and **Run Analysis**. To compare two dates, open both and choose the second as Image B.

| Analysis | Needs |
|---|---|
| True colour picture, land cover (named classes or K-means clusters) | any image |
| False colour, NDVI, NDWI, vessel candidates | near-infrared band |
| Short-wave infrared picture, NDBI, NBR | short-wave infrared bands |
| Change detection, forest loss / gain, A \| B swipe | two images of the same place |

Two images are lined up by their georeferencing; without it they must have the same shape.
Results can be saved as GeoTIFF (float32) or as a picture.

`samples/` holds real Sentinel-2 crops for trying the app: two dates of Busan and Seoul (6 bands),
the same Busan scene as separate single-band files, and as a plain PNG picture.

## Build

```bat
desktop\build.bat        rem CMake + portable MSVC v142 -> desktop\build\EarthView.exe
desktop\EarthView.sln    rem open in Visual Studio 2019 or later (v142 toolset) -> desktop\bin\Release
desktop\make_dist.bat    rem standalone folder -> dist\EarthView (no Python or Qt needed on the target PC)
```

- Qt, CMake and the compiler come from `D:\KJS_02\work\Stable Diffusion\ALPR\packages`. If that moves, change
  `PackagesDir` in `desktop/EarthView.props`, `PACKAGES_DIR` in `desktop/CMakeLists.txt` and `PKG` in `build.bat`.
- The analysis engine is Python (`satimg/`). `make_dist.bat` freezes it with PyInstaller into
  `engine\satimg-engine.exe`. When that file sits next to `EarthView.exe` the app uses it; otherwise it runs
  the engine from this source tree with `.conda-env`.

First-time setup of the Python environment (only needed to build or to run from source):

```powershell
conda create -y -p .\.conda-env python=3.12
.\.conda-env\python.exe -m pip install -r requirements.txt
```

Tests: `.\.conda-env\python.exe -m pytest tests`

End-to-end check: `EarthView.exe --selftest <output folder> <image A> <image B> [<plain picture>]` opens the
files, runs every analysis and saves window captures.

## Sign-in

The app opens with a sign-in window; **Create an account** registers a new user. Accounts are local to
this computer (the app is offline): `users.json` in `%APPDATA%\EarthView\EarthView AI` holds each user name
with a salted PBKDF2-SHA256 hash, never the password. This keeps casual users out of the app; it does not
protect the image files, which anyone with access to the PC can read. There is no password recovery:
deleting `users.json` removes all accounts.

## Named land cover (Land Cover tab)

A classifier trained on Sentinel-2 pixels with ESA WorldCover 2021 as reference gives four named classes:
forest, farmland / grassland, urban / built-up, water. The app outlines and labels the largest regions on
the image ("Forest area 2.34 km²"), and lists count, area and model confidence per class. With two images,
**Change → Forest loss / gain** outlines where forest disappeared or appeared.

Agreement with the reference map on six sites held out of training (`scripts/landcover_report.json`):

| Bands in the image | Accuracy |
|---|---|
| Six (visible, near-infrared, two short-wave infrared) | 76% |
| Four (visible + near-infrared) | 69% |
| Red, green, blue only | 62% |

- Water and urban are the dependable classes; forest and farmland / grassland are confused with each other
  fairly often (forest is right about 6 times in 10 when predicted).
- There is no "bare" class: a scheme with one was trained too and did worse (72%), because bare ground was
  almost always a wrong call. Deserts and bare soil are therefore reported as one of the four classes.
- It was trained on growing-season Sentinel-2 scenes at 10–20 m. Winter images, other sensors, very different
  resolutions and plain pictures are less reliable. Compare forest between images of the same season only.
- "Urban / built-up" is an area class. Individual buildings, roads or vehicles cannot be counted at this
  resolution; that needs sub-metre imagery and a dedicated trained model.
- The model file `satimg/models/landcover.joblib` is produced by `scripts/train_landcover.py` (a developer
  step that needs internet) and ships inside the app. The class list is read from the model file.
## Vessel candidates (object detection)

A rule-based detector, not a trained model: compact objects brighter than the surrounding open water, at
least the chosen length, and not attached to land. Known false alarms: small islands and rocks, shallow
sandbanks, breaking waves. Giving an Image B from another date drops anything that is also not water there
(fixed objects), which also drops a vessel moored in the same place on both dates. It needs red, green, blue
and near-infrared bands, a known pixel size, and an analysis area fine enough (about 12 m per pixel or better).

## Layout

- `satimg/local.py` – opens image files, band guessing, reads any window of an image, aligns a second image
- `satimg/analysis.py` – indices, statistics, land cover, change classes, vessel detector
- `satimg/render.py` – PNG and GeoTIFF output
- `satimg/app.py` – the engine's local API
- `desktop/` – Qt 5 Widgets app (C++17): `MapWidget` image viewer, `Charts`, `Backend` engine connector,
  `MainWindow` workspace, `MainWindowPages` navigation, pages and the band dialog
- `scripts/` – developer tools that are **not part of the app** and do need internet:
  `make_samples.py` (fetches the sample images), `validate.py` (accuracy check against ESA WorldCover),
  and `retired_online/` (the earlier browser version that searched the Sentinel-2 archive)

## Limits

- Integer images without scaling information are read as reflectance × 10000 (Sentinel-2 style) and 8-bit
  images as 0–255. Indices are ratios, so they do not depend on this; the vessel detector's water test does.
- Large areas are analysed at reduced resolution (1024 pixels on the long side; 2048 for detection).
  The viewer itself shows full detail when zoomed in.
- There is no cloud masking: clouds in the image are analysed like anything else.
