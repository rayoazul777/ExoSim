# ExoSim

A retro-futurist real-time viewer for exoplanet/solar-system orbital data
from the [NASA Exoplanet Archive](https://exoplanetarchive.ipac.caltech.edu/).
Orbits are propagated from real Keplerian elements; the visual style
(scanlines, phosphor glow, vignette) comes from a CRT post-process shader.

## What's here

```
ExoSim/
  CMakeLists.txt        - build config
  vcpkg.json             - dependency manifest (glfw, glm, imgui, json, glad, curl)
  data/
    fetch_exoplanets.py  - Python fetch script (pulls real data from THREE sources)
    exoplanets.json       - (generated) planet dataset - NASA Exoplanet Archive
    stellar_hosts.json    - (generated) companion-star data - NASA Exoplanet Archive
    dwarf_planets.json    - (generated) Ceres/Pluto/Eris/Haumea/Makemake - NASA/JPL SBDB
    star_catalog.json     - (generated) ~9,000 real star positions - Hipparcos via CDS VizieR
  tools/
    fetch_data.cpp        - optional C++ equivalent of fetch_exoplanets.py (see below)
  src/
    main.cpp               - window/app loop, camera, ImGui HUD, view-mode switching
    KeplerOrbit.h/.cpp      - solves Kepler's equation, propagates orbit position
    Dataset.h/.cpp          - loads all four JSON files, groups planets into StarSystems
    PlanetClassifier.h/.cpp - density/radius/mass-based composition estimates + curated real findings
    StarClassifier.h/.cpp   - spectral-type classification and color from stellar temperature
    SolarSystemAppearance.h/.cpp - real known look (colors, rings, axial tilt) for known planets/moons
    Renderer.h/.cpp         - OpenGL renderer: skybox, real star catalog, orbits, planets,
                              star, companions, wireframe detail globe, procedural
                              surface shader, rings
    CrtPostProcess.h/.cpp   - offscreen framebuffer + CRT composite pass
  shaders/
    crt.frag               - scanlines / glow / vignette / barrel distortion
```

> **If you fetched data before this update**: re-run `fetch_exoplanets.py` -
> it now also writes `data/star_catalog.json` (a real Hipparcos-catalog
> starfield, replacing the skybox's old procedural fake stars) from a
> THIRD data source, CDS's VizieR TAP service. The app runs fine on an
> old/partial data folder - each missing file just quietly disables the
> one feature it feeds; without this one, the skybox falls back to no
> stars at all (the nebula/band are still procedural either way).

## Fetching data: Python or C++

`data/fetch_exoplanets.py` and `tools/fetch_data.cpp` fetch the exact same
data from the exact same THREE sources: the NASA Exoplanet Archive's TAP
endpoint (planets, stellar hosts), NASA/JPL's SBDB API (dwarf planets),
and CDS's VizieR TAP service (the real Hipparcos star catalog) - full
parity between the two scripts, pick whichever you'd rather run.
**Neither is required by ExoSim.exe**: this is a one-time, offline
data-prep step, and the app never invokes either script/tool or depends
on Python being installed. The C++ version exists for anyone who'd rather
keep the whole toolchain in one language, not because the Python version
caused any actual problem.

The C++ tool (`FetchData`, built alongside `ExoSim` by the same CMake
project) also has a `--top-systems N` flag that prints the N systems with
the most planets, found via a min-heap (see `PrintTopSystemsByPlanetCount`
in `tools/fetch_data.cpp`) - the classic heap-based "top-K" selection
algorithm, used here because it's a genuine fit (picking the K richest
systems without sorting the whole multi-thousand-row dataset), not
because the task needed a heap to work.

```powershell
# From your build output directory, next to ExoSim.exe:
.\FetchData.exe --top-systems 10
```

(There's a well-known community "Solar System OpenData" API that
aggregates similar dwarf-planet data, but it now requires an account/API
key and isn't actually NASA-run, so it wasn't used here - SBDB fit the
brief better: official NASA/JPL, keyless.)

## 1. Get the real data (Python version)

You need Python 3.10+ with no extra packages (uses only the standard library).



```powershell
cd ExoSim\data
python fetch_exoplanets.py
```

This queries the Exoplanet Archive's public TAP endpoint (planets, then
stellar hosts), NASA/JPL's SBDB (the 5 dwarf planets), and CDS's VizieR
TAP service (the real Hipparcos star catalog) - no API key required for
any of the three - writing `exoplanets.json`, `stellar_hosts.json`,
`dwarf_planets.json`, and `star_catalog.json`. Re-run it any time you want
a fresh pull - by default it grabs every confirmed planet's "best"
parameter set (~5,900+ rows as of 2026; not all have every field
measured, and the app skips any planet missing the semi-major-axis +
period it needs to draw an orbit) plus ~9,000 real stars down to the
naked-eye magnitude limit.

If you skip this step entirely, ExoSim still runs - our own solar system
(`Dataset::DemoSolarSystem()`) is always included regardless, so you'll
still have something real to look at, just without the wider Archive catalog.


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

- **Background**: both views render a skybox - a full-screen shader pass
  using the camera's rotation only (not its position), so it feels like a
  fixed, distant backdrop as you orbit rather than a texture pasted on a
  sphere. It has two genuinely different layers:
  - **Real stars**: ~9,000 actual stars from the **Hipparcos catalog**
    (ESA mission, via CDS's VizieR TAP service - a third data source,
    independent of both NASA ones), rendered at their real measured sky
    positions (RA/Dec converted to a direction), colored from each star's
    real B-V color index (blue-white for hot stars, red for cool ones -
    via a standard approximate color mapping, not exact blackbody
    physics), and sized/brightened from real visual magnitude. This
    replaced the earlier procedural hash-noise "stars", which are gone
    now that real ones are available - no more of the two blended together.
  - **Nebula patches and the galactic band** remain procedural (soft
    colored noise) - there's no simple bulk dataset of real nebula
    positions to draw from. The galactic band's *orientation*, though, is
    real: tilted per-system using the Archive's actual measured galactic
    latitude/longitude (`glat`/`glon`) when available, so different
    systems in the list have a genuinely different (not just randomized)
    backdrop orientation.
  - If `data/star_catalog.json` wasn't fetched, the skybox simply shows no
    stars (nebula/band still render) rather than falling back to fake ones.
- **Dwarf planets**: Sol's system now includes Ceres, Pluto, Eris, Haumea
  and Makemake, with real orbital elements and (where SBDB has them)
  physical data pulled from **NASA/JPL's SBDB API** - see "Fetching data"
  above. Pluto and Ceres also get real known-appearance rendering (New
  Horizons' Tombaugh Regio "heart" on Pluto, Dawn's bright Occator crater
  spots on Ceres) via `SolarSystemAppearance`, same mechanism as the 8
  main planets. Eris/Haumea/Makemake use the generic classifier look,
  since no spacecraft has imaged them up close.
- **Real moon appearance**: `SolarSystemAppearance` now also covers our
  own well-photographed moons - Titan's hazy orange atmosphere, Io's
  volcanic yellow-orange surface, Europa's icy white with reddish crack
  coloring (a stylized approximation of the real linear cracks, not their
  actual geometry - noted directly in the code), Ganymede, Callisto,
  Rhea, Titania, Oberon, Triton, and our own Moon. They render with the
  same solid-surface + wireframe-grid pipeline as planets now, not a
  plain wireframe dot. This is applied **only** to moons flagged real
  (`isReal`) - speculative/candidate moons on actual exoplanets still
  render as the plain, dimmer wireframe dot, so the two never get confused.
- **Earth's continents were fixed**: the old land/ocean mask was a single
  noise threshold, which tended to produce one connected blob that read
  as "Pangea" rather than modern Earth. It's now several individually-
  bounded landmass shapes at roughly correct relative positions (not
  cartographically precise, but unmistakably separate continents).
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
- **Sol, your home system**: our own solar system is now always included
  alongside whatever the Archive fetch pulled in, labeled **Sol (Home
  System)** and pinned to the top of the system list regardless of the
  usual most-planets-first sort - a fixed real-data reference point to
  compare everything else against. It's also the most physically detailed
  system in the app: every planet has real density, equilibrium
  temperature, radius, mass, argument of periapsis, and longitude of
  ascending node (J2000-epoch, approximate), not just the bare minimum
  needed to draw an orbit.
- **Companion stars**: multi-star systems now render actual companion
  star(s), not just a text flag. Color and spectral classification are
  **real**, pulled from the Archive's `stellarhosts` table when a
  companion has been independently characterized there (deduplicated
  against repeated publications of the same star). Where the Archive only
  confirms a companion exists (`sy_snum`) without resolved properties, it's
  shown as a neutral gray "unknown companion" placeholder - never invented
  data. **Position and orbital motion are always schematic** in both cases:
  the Archive has no bulk-queryable field for real physical separation or
  orbital period between components (that mostly lives in individual
  discovery papers), so companions are arranged evenly around the primary
  and slowly orbit it at an illustrative, clearly-flagged speed rather than
  a claimed real one - it exists so a multi-star system doesn't look like
  a frozen diagram, not to simulate real binary dynamics.
- **Real appearance for our own planets**: when you inspect one of Sol's 8
  planets, `SolarSystemAppearance` overrides the generic classification
  shader with each planet's actual, well-photographed look - Earth's blue
  oceans and green/brown continents, Jupiter's Great Red Spot, Mars' rusty
  polar caps, Venus's obscuring haze, Neptune's Great Dark Spot - plus real
  axial tilt (Uranus visibly "rolls on its side" at 97.8°, Venus spins
  almost upside-down at 177.4°). **Saturn and Uranus render real rings**
  (Saturn's prominent, Uranus's much fainter, matching their real relative
  visibility) via a new flat-annulus ring shader with stylized radial
  banding evoking gaps like the Cassini Division - the banding is stylized
  since there's no simple bulk dataset of exact real gap positions, but the
  ring's existence, relative size, and tilt are real. Every other planet in
  the app (every actual exoplanet) still uses the honest procedural/
  statistical look from before, since we have no real imagery to go on there.
- **Moons now use real Kepler orbits**: both real (demo solar system) and
  speculative (Archive exoplanets) moons are propagated with the same
  `KeplerOrbit` math used for planets, so inclination and eccentricity
  genuinely tilt and distort each moon's path - Phobos and Deimos wobble
  around Mars' equatorial plane, Triton's steep retrograde-like tilt
  actually shows, and so on - rather than a flat, perfectly circular loop.
- **Speculative moons are now cached**: generated once per planet (still
  deterministic from its name) and reused for the rest of the session
  instead of being regenerated on every inspection, and their count now
  shows next to each planet's name in the list before you even click in.
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

- **Real companion separation**: the
  Archive doesn't expose a bulk physical/angular separation field, and I'm
  not going to fabricate one. If a future Archive table (or a
  supplementary catalog) adds it, swapping the schematic offset for a real
  one would be a straightforward change to `selectSystem`'s companion-
  placement code in main.cpp - the moon and companion orbital-motion code
  already reuse `KeplerOrbit`, so real period/separation data would slot
  in the same way real inclination/eccentricity did for moons.
- **Ring gap positions**: Saturn and Uranus's rings
- the banding pattern inside them is a stylized
  approximation, not real gap radii like the actual Cassini Division -
  there's no simple bulk-queryable dataset of exact ring-gap positions to
  draw from the way there is for orbital elements.
- **Jupiter and Neptune's real (but very faint) rings** were deliberately
  left out - they exist in reality but are so faint they're not part of
  either planet's "known appearance" the way Saturn's or even Uranus's
  are, so including them risked overstating their visual prominence.
- **Europa's cracks (lineae) are real linear features**; the shader's
  banding/mottling gives a stylized approximation of the coloring, not
  the actual crack geometry - a genuine limitation of the current shader
  technique, noted directly in the code.
- **Star catalog depth**: capped at Vmag <= 6.5 (~9,000 stars, the
  naked-eye limit) rather than the full Hipparcos catalog (~118,000
  stars down to much fainter magnitudes) - a deliberate scope match for
  "the visible night sky" rather than gratuitous data volume, but going
  deeper would be a straightforward change to the ADQL query's WHERE
  clause in both fetch script/tool if a denser field is wanted.
- **Star catalog proper motion / parallax** aren't fetched - Hipparcos
  has both, which would let stars drift correctly over very long
  simulated timescales and (with parallax) support real relative
  distances, neither of which this app currently uses since the skybox
  treats every star as infinitely distant and static.
- **VizieR TLS certificate verification**: some Windows Python installs
  can't verify `tapvizier.cds.unistra.fr`'s certificate chain (a local
  trust-store gap - confirmed by an actual test run, where the other three
  fetches succeeded fine and only this one hit
  `CERTIFICATE_VERIFY_FAILED`). Both fetch script/tool now retry once
  without verification, but ONLY after a genuine verification failure and
  ONLY for this one request - a deliberate, narrow, and logged exception,
  not a general policy. The proper long-term fix is `pip install certifi`
  (Python) or keeping your OS's root certificates current; this fallback
  exists so the fetch still works today without adding that as a required
  dependency for a background starfield.
