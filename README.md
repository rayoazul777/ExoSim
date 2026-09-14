# ExoSim

A retro-futurist real-time viewer for exoplanet/solar-system orbital data
from the [NASA Exoplanet Archive](https://exoplanetarchive.ipac.caltech.edu/).
Orbits are propagated from real Keplerian elements; the visual style
(scanlines, phosphor glow, vignette) comes from a CRT post-process shader.

## What's here

```
ExoSim/
  CMakeLists.txt        - build config (CMake, works directly in VS2022)
  vcpkg.json             - dependency manifest (glfw, glm, imgui, json, glad)
  data/
    fetch_exoplanets.py  - pulls real data from the NASA Exoplanet Archive TAP API
    exoplanets.json       - (generated - not checked in) the fetched dataset
  src/
    main.cpp              - window/app loop, camera, ImGui HUD
    KeplerOrbit.h/.cpp     - solves Kepler's equation, propagates orbit position
    Dataset.h/.cpp         - loads exoplanets.json into orbital elements
    Renderer.h/.cpp        - minimal OpenGL wireframe renderer
    CrtPostProcess.h/.cpp  - offscreen framebuffer + CRT composite pass
  shaders/
    crt.frag               - scanlines / glow / vignette / barrel distortion
```

## 1. Get the real data

You need Python 3.10+ with no extra packages (uses only the standard library).

```powershell
cd ExoSim\data
python fetch_exoplanets.py
```

This queries the Archive's public TAP endpoint (no API key required) and
writes `data/exoplanets.json`. Re-run it any time you want a fresh pull -
by default it grabs every confirmed planet's "best" parameter set (~5,900+
rows as of 2026; not all have every field measured, and the app skips any
planet missing the semi-major-axis + period it needs to draw an orbit).

If you skip this step, ExoSim still runs - it falls back to a small
hand-coded demo of our own solar system (`Dataset::DemoSolarSystem()`).

## 2. Install dependencies via vcpkg

If you don't already have vcpkg:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

Then tell Visual Studio to use it globally (one-time):

```powershell
C:\vcpkg\vcpkg integrate install
```

Because `vcpkg.json` is a manifest, you don't need to `vcpkg install`
anything by hand - VS2022's CMake integration reads it and restores
the dependencies (glfw3, glm, imgui, nlohmann-json, glad) automatically
the first time you configure the project.

## 3. Open in Visual Studio 2022

You have C++ development installed already, so:

1. **File > Open > Folder...** and select the `ExoSim` folder (not a
   `.sln` - this is a CMake project, and VS2022 has native support for
   that via "Open Folder").
2. VS2022 will detect `CMakeLists.txt` and `vcpkg.json`, then configure
   automatically. Watch the Output window (CMake tab) for the vcpkg
   dependency restore on first run - it can take a few minutes.
3. Once configured, select `ExoSim.exe` as the startup item (top toolbar
   dropdown) and hit **Run (F5)**.

If VS2022 doesn't auto-detect vcpkg, make sure the toolchain file is wired
up: **Project > CMake Settings** and confirm
`CMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake` is set
(the `vcpkg integrate install` step above usually handles this for you).

## Controls / HUD

- Left panel lists every loaded body; click one to see its orbital
  parameters (semi-major axis, eccentricity, period).
- Slider controls simulation speed (days of simulated time per real second).
- Camera currently auto-orbits the system; swapping in mouse-drag control
  is a natural next step (see below).

## Where to go next

- **Camera control**: replace the fixed auto-orbit in `main.cpp` with
  mouse-drag orbit + scroll-to-zoom.
- **Scale handling**: exoplanet systems span wildly different sizes: consider
  a log-scale toggle for semi-major axis so compact systems don't vanish
  next to sprawling ones.
- **Filtering**: the HUD lists everything loaded; you'll likely want a
  search box or "only show systems with 3+ planets" filter once you're
  pulling the full archive.
- **3D inclination**: `pl_orbincl` is included but many archive rows don't
  have it measured (defaults to 0/coplanar) - worth flagging in the HUD
  when a value is a fallback rather than measured.
