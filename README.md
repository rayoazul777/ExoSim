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
    main.cpp               - window/app loop, camera, ImGui HUD, view-mode switching
    KeplerOrbit.h/.cpp      - solves Kepler's equation, propagates orbit position
    Dataset.h/.cpp          - loads exoplanets.json, groups planets into StarSystems
    PlanetClassifier.h/.cpp - density/radius/mass-based composition estimates + curated real findings
    StarClassifier.h/.cpp   - spectral-type classification and color from stellar temperature
    Renderer.h/.cpp         - minimal OpenGL renderer: orbits, planets, star,
                              wireframe detail globe, and the procedural
                              solid-surface shader pass
    CrtPostProcess.h/.cpp   - offscreen framebuffer + CRT composite pass
  shaders/
    crt.frag               - scanlines / glow / vignette / barrel distortion
```

> **If you fetched data before this update**: re-run `fetch_exoplanets.py` -
> it now pulls a few more columns (density, equilibrium temperature,
> stellar spectral type, number of stars) that the star rendering, improved
> composition estimates, and multi-star flag depend on. An old
> `exoplanets.json` will still load, just without those refinements.

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

- **Host star**: rendered at the system's origin as a CRT wireframe globe,
  colored by real spectral type (O/B/A/F/G/K/M, from measured effective
  temperature) and sized from its real radius. A star's true AU-scale size
  is almost invisible next to planetary orbits, so a **Star size
  exaggeration** slider scales it up uniformly - relative sizes between
  different stars stay accurate even though the absolute size on screen
  isn't to scale. The star's size is also clamped so it never visually
  swallows the innermost orbit.
- **Composition/atmosphere estimates**: `PlanetClassifier` now prefers
  measured **bulk density** (g/cm^3) when available - it's far more
  diagnostic of rocky-vs-gas than radius or mass alone - falling back to
  radius, then mass, in decreasing order of confidence. Equilibrium
  temperature adds physically-grounded notes (e.g. flagging that a hot
  rocky planet likely had its atmosphere stripped). A small hand-curated
  table of **real published atmosphere findings** (JWST/Hubble results for
  ~10 well-studied planets like WASP-39 b, K2-18 b, TRAPPIST-1 b/c) is
  shown distinctly, labeled MEASURED, when the selected planet matches -
  everything else stays clearly labeled as a statistical estimate, since
  the Archive itself doesn't carry real atmosphere data for the vast
  majority of planets.
- **Moons**: inspecting a planet also shows any moons orbiting it as small
  wireframe spheres (with faint orbit-guide rings). For the built-in demo
  solar system these are **real** (Moon, the Galilean moons, Titan, etc.,
  with approximate real orbital periods). For every Archive-loaded
  exoplanet - which is effectively all of them - **no confirmed moons
  exist in the data at all** (exomoons are extraordinarily hard to detect;
  only a couple of disputed candidates exist in the literature, system-wide),
  so illustrative "candidate" moons are generated instead, deterministically
  seeded from the planet's name and loosely biased by its composition
  (gas giants get more, in keeping with the solar-system pattern). These
  are rendered dimmer and labeled "speculative, not a real detection"
  everywhere they appear - the HUD never lets the two be confused.
- **Distance-scale legend**: with log-scale on, up to three faint reference
  rings (1/10/100/1000 AU, whichever are relevant to the current system)
  are drawn and labeled in the HUD, so the compression doesn't silently
  imply planets are closer together than they really are.
- **Multi-star systems**: systems with more than one star (`sy_snum > 1`
  in the Archive) are flagged in the HUD. This build still only models and
  renders the primary star - companion stars aren't fetched or drawn -
  so circumbinary systems will look like a single-star system with the
  flag noting that's a simplification.
- **Smoother camera**: switching star systems (or changing log-scale/star-
  exaggeration settings) now eases the camera to its new distance over
  a fraction of a second instead of snapping instantly, which used to be
  jarring given how much scale varies between systems.
- **Camera**: left-click-drag to orbit, scroll to zoom. Works the same in both
  System view and Planet Detail view (each remembers its own zoom level).
- **System view**: search box + "min planets" slider filter the star-system
  list; click a system to fly to it, click a planet name for its orbital
  stats, click **Inspect Planet** to open the detail view.
- **Planet Detail view**: shows the selected planet as a solid, procedurally
  shaded globe (banded clouds for gas/ice/mini-Neptune types, a cratered
  pattern for rocky ones - both generated per-pixel by a GLSL shader, no
  textures involved) with the CRT wireframe grid overlaid on top like an
  old vector-arcade targeting display (think Elite/Star Fox). Scroll to
  zoom in close - because the surface is computed per-pixel rather than
  from a fixed-resolution texture, it stays sharp at any zoom level instead
  of pixelating. **This is a stylized approximation driven by the
  classification data, not a real image** - no exoplanet has ever actually
  been imaged in this kind of detail, and the HUD says so explicitly.
  Also shows an estimated composition/atmosphere readout (rocky /
  super-Earth / mini-Neptune / ice giant / gas giant) based on measured
  bulk density, or radius/mass as lower-confidence fallbacks. **Back to
  System View** returns.
- **Log-scale distances** checkbox (System view): compresses large orbits
  logarithmically so compact systems don't get lost in scale next to
  sprawling ones. Only the *radius* from the star is remapped - each planet's
  direction/angle stays exact.
- Inclination in the stats panel is flagged **(measured)** vs **not measured
  (assumed coplanar)** - most Archive rows don't have a measured inclination,
  and it defaults to 0 degrees (coplanar) rather than silently guessing.

## Where to go next

- **Render actual companion stars**: multi-star systems are flagged now,
  but only the primary is drawn. Fetching companion star separation/mass
  (from the Archive's `stellarhosts` table, which this simplified pipeline
  doesn't currently query) and rendering a second star at the right offset
  would make circumbinary systems visually accurate rather than just flagged.
- **Moon orbital inclination/eccentricity**: both real and speculative
  moons currently orbit flat in the planet's equatorial plane for
  simplicity - real moons (and any real exomoons, if ever confirmed) can
  have inclined or eccentric orbits.
- **Persist speculative moon seeds across the fetched dataset**: right now
  speculative moons are generated fresh each time you inspect a planet
  (deterministically, from a name hash), which is fine for consistency but
  means they aren't inspectable/filterable the way real data is - could be
  worth precomputing and caching them if that becomes useful.
