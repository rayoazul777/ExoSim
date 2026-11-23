# Changelog

Versions 0.1–0.5 were reconstructed from the development history (there
were no release tags at the time). The README describes only the
**current** state; historical behavior lives here.

## 0.8.0: Trajectory design, validation and the Milky Way map

**Added**
- **Lambert trajectory design.** Universal-variable Lambert solver,
  state↔elements conversion and universal Kepler propagation (hyperbolic
  legs included). Missions now default to the optimal 3-D Lambert
  transfer between the planets' real positions; Hohmann remains
  selectable as "Trajectory model".
- **Porkchop plots.** A 100 × 70 launch/arrival grid of C3, departure Δv
  from LEO, arrival v∞ or total cost, with contours and Type I/II lobes.
  Hover for details, click a cell to fly that transfer.
- **Gravity-assist chains.** Patched-conic powered flybys: turn limit
  sin(δ/2) = 1/e, periapsis burn when the turn falls short, Laplace
  sphere of influence, and SOI-IN / FLYBY / SOI-OUT events on the
  timeline. Five sequences (EJS, EJU, EJN, EJP, EVEJ); the optimizer
  finds the Voyager (1977-09-05) and New Horizons (2006-01-19) launch
  dates.
- **JPL Horizons validation.** 8 planets × 51 epochs, 1950–2050, against
  DE441: error envelopes in `VALIDATION.md` (`--horizons-table`).
- **Recorded-input replay.** `--record`, `--replay` and
  `--verify-replay`, plus Record and Replay controls in the app. A
  recording is tick-stamped commands in JSON lines plus a final state
  hash; playback reproduces the session bit for bit.
- **Milky Way map.** Zoom from 1 pc to 80 kpc in a top-down map or a
  3-D view: 4,668 exoplanet hosts at their real positions, Hipparcos
  stars within 1 kpc, and an illustrative disk/bar/bulge/arm model with
  distance rings and Galactic longitude marks. Search, hover and click
  hosts; "Open system" jumps to the simulation. `--galaxy`,
  `--galaxy-3d`, `--galaxy-distance <pc>`.
- **Configuration file** `exosim.json` (window, paths, speed, logging),
  validated, with an example `exosim.example.json`.
- **Structured logging.** Levels and components, readable on the
  console, plus an optional JSON-lines log file.
- **Static analysis in CI.** clang-format and clang-tidy (warnings as
  errors) gate the Linux and Windows jobs.
- 94 new automated checks (LAM, GA, EPH-H, ENG, GAL): 181 in total.

**Changed**
- **`main.cpp` split** into `sim/` (state, commands, replay, catalog),
  `sky/`, `galaxy/`, `app/` (loop, input, view state), `scene/` (one per
  view) and `ui/` (panels). The core builds as `ExoSimCore` with no
  OpenGL. 12 reference views were pixel-identical before and after the
  split.
- **Ephemeris:** Standish Table 1 secular rates are now applied. Error
  over 1950–2050 drops 2–63× for the inner planets. Near 2025, Jupiter
  and Saturn opposition times shift from 12/31 min to 69/153 min, which
  is the cost of a century-wide fit; see `VALIDATION.md`.
- Cruise CSV export uses the actual trajectory legs; the vis-viva column
  was removed because it doesn't apply to multi-leg flyby trajectories.

**Fixed**
- The CI workflow only triggered on `main`, but the repository's default
  branch is `master`, so pushes never ran CI. It now triggers on both.
- Screenshots now report a failed write (a missing folder, say) instead
  of claiming success, and the exit code reflects it.
- Found by clang-tidy: four floating-point loop counters, an incorrect
  `(int)(x + 0.5)` rounding, and an empty `catch` that hid a parse error.
- `fetch_exoplanets.py` now also fetches `sy_dist` from `stellarhosts`.
  107 hosts, TRAPPIST-1 among them, have no distance in the `ps` default
  parameter set; the map falls back to the `stellarhosts` value.

## 0.7.0: Mission operations and landing simulation

**Added**
- **Click the spacecraft to follow it.** Clicking the rocket (or "Follow
  spacecraft") locks a chase camera onto it, with lock-on brackets. Zoom
  works down to close range.
- **Landing view.** At arrival the mission continues into a simulated
  entry, descent and landing, watched in real time from a chase camera
  that zooms in as the craft descends. It shows a terrain grid, the
  flown and remaining trajectory, the guidance target, and a vehicle
  glyph for each phase (heat-shield plasma, parachute, powered lander
  with an engine plume).
- `LandingSimulator`: deterministic 2-D EDL simulation. Physics runs with
  RK4 at a fixed 100 Hz; guidance and control run at 20 Hz with
  zero-order hold. A sequencer moves between phases on measured
  conditions. Four vehicle classes:
  - Mars, Perseverance-class: lifting entry, then parachute, heat-shield
    jettison, backshell separation, and ZEM/ZEV powered descent to a
    0.75 m/s touchdown.
  - Venus, Venera-class: parachute, then free fall on a drag disk.
  - Titan, Huygens-class: main parachute, then drogue to a 4.5 m/s
    touchdown.
  - Airless bodies, Apollo LM-class: powered descent from a low orbit.
  - Giant planets end at orbit insertion.
- `MissionTimeline`: a sequence of events (launch, injection burn,
  correction slots, approach, orbit insertion or atmospheric entry, every
  landing event) and the mission phase at any time.
  - Orbit-insertion Δv is now computed. This closes the "capture burn not
    included" gap; for example, lunar orbit insertion is ~0.8 km/s.
  - Mission elapsed time, and one-way light time (the signal delay to
    Earth).
- **Flight Director** window:
  - MET, UTC, phase and next event; warp controls (pause, 1×, 60×,
    1 h/s, 1 d/s, 10 d/s); "warp to events".
  - Live telemetry: Sun distance, heliocentric speed, Earth and target
    range, light time.
  - A scrolling sequence-of-events log.
  - CSV export of the cruise state vectors and of the landing telemetry.
- **Landing Telemetry** window: altitude, velocity components, sensed g,
  dynamic pressure, heat flux, propellant and throttle; altitude and speed
  strip charts; limit monitors (green/yellow/red) for peak load, heat
  flux, touchdown speed, propellant margin and landing error.
- `SimClock`: a deterministic fixed-tick clock. Simulation time depends
  only on tick count and warp, not on frame rate. It stops exactly on
  events: at launch it drops to 60×, and at the landing to real time.
- `BodyConstants`: GM, radius and atmosphere for 23 Solar System bodies,
  with tabulated density profiles for Venus and Titan.
- 24 new validation checks (FSW-1…24), for 87 in total. They include
  touchdown speeds and durations against Perseverance, Apollo 11 and
  Huygens; a bit-for-bit determinism re-run; the capture-Δv identity and
  Juno JOI; light time; and clock behavior.
- `--scenario <body> [--edl-time s | --cruise f] [--screenshot file.ppm]`
  opens a planned mission at a given moment, for reproducible
  documentation screenshots.

**Changed**
- The simulation speed slider is logarithmic, so real time (1×) is
  reachable.

## 0.6.0: Validation and documentation

**Added**
- `ExoSimTests`: a 63-check validation suite (Kepler invariants,
  ephemeris vs. published events, closed-form mission checks, time
  conversion, data robustness, classifier sanity). It is registered with
  CTest and prints its results table with `--markdown`.
- Documentation restructure: portfolio README, `docs/VALIDATION.md`,
  `docs/ARCHITECTURE.md` (time systems and frames defined precisely),
  `docs/DATA_SOURCES.md` (citations, acknowledgments, licenses), and
  `docs/REQUIREMENTS.md` (traceability).
- Project version in CMake (`0.6.0`).
- **MIT License** for the source code (bundled catalog data stays under
  its providers' terms).
- **Portable Windows release** and **GitHub Actions CI**:
  - static build (`x64-windows-static` plus the static MSVC runtime, so no
    DLLs are needed);
  - `CMakePresets.json`, and CPack zip packaging with a data snapshot;
  - an end-user `README.txt` and `LICENSE` inside the zip;
  - a workflow that validates on Linux, then builds, tests, smoke-tests
    and packages on Windows, and publishes a Release on `v*` tags.
- `ExoSim --selftest` (headless install check), `--version` and `--help`.
- `tools/validate_shaders.py` (GLSL reference-compiler check, used in CI).

**Fixed** (all found by the new suite or while documenting)
- Kepler solver returned wrong roots for e ≥ 0.8 just past apoapsis
  (positions jumped by up to ~3.6 AU). It now uses a sign-aware starting
  guess plus a guaranteed bisection fallback.
- A single malformed catalog row (wrong JSON type, or a `null` name)
  crashed the app. Field access is now type-checked.
- Jupiter was classified as a mini-Neptune. Radius now takes precedence
  at ≥ 6 R⊕ (Kepler size classes).
- The real starfield was tilted 23.4° from the planetary orbits
  (equatorial vs. ecliptic frame). Stars are now rotated by the J2000
  obliquity.
- Star proper motion is now applied from the Hipparcos catalog epoch
  (J1991.25), not from the present day.
- Assets now load relative to the executable rather than the launch
  directory, so the app works from shortcuts and other folders.
- `GroupByHost` now uses a hash map (O(n)) instead of a linear scan per
  row: ~26 ms → ~8.5 ms at today's catalog size.
- Visual Studio could no longer configure the project once
  `CMakePresets.json` was added: the presets expected a `VCPKG_ROOT`
  environment variable, so vcpkg was never loaded. `CMakeLists.txt` now
  finds vcpkg itself (`VCPKG_ROOT`, then `VCPKG_INSTALLATION_ROOT`, then
  `C:/vcpkg`), and a default `windows-dev` preset (Debug/Release) was
  added for everyday IDE use.
- Mercury and Venus were shown with illustrative "candidate" moons, which
  are meant only for exoplanets. Sol's bodies now never get invented moons,
  and the moon count reads "1 moon" rather than "1 moons".

## 0.5.0: Real sky, mission planner, rendering accuracy
- **Mission Planner** (Sol): Hohmann transfers, launch-window search,
  patched-conic Δv, animated trajectory, published vehicle figures.
- Sol planets moved to real J2000 elements; the simulation starts at
  today's date. Dwarf-planet mean anomalies propagated to J2000.
- Real **Hipparcos** starfield via CDS VizieR (replacing procedural fake
  stars), re-projected from each host's real position using parallax,
  with a proper-motion epoch slider.
- Real companion separations from the **Washington Double Star** catalog
  (projected, i.e. lower bounds) with Kepler's-3rd-law periods.
- Real ring radii for Saturn, Uranus, Jupiter and Neptune; Europa lineae.
- Moon inspection view. Zoom-adaptive 3-D surface noise with bump
  shading (fixes the close-up blur). Exoplanet surface variants by type
  and temperature.
- TLS: `certifi` support (Python) and the OS-native CA store (C++), with
  a narrow, logged insecure retry kept only as a last resort.
- Fixed: orbits were drawn in a different plane from the rings and
  camera; `std::max` broken by the `windows.h` macro (`NOMINMAX`); a
  GLSL built-in name collision (`noise3`) that broke the planet shader.

## 0.4.0: Home system, companions, appearance
- Sol always included as the pinned "home system". Dwarf planets from
  NASA/JPL SBDB.
- Companion stars rendered from the Archive's `stellarhosts` table
  (schematic positions at this stage).
- Real known appearance for Sol's planets and major moons (colors,
  spots, polar caps, haze, axial tilt, rings).
- Moons propagated on Kepler orbits with inclination and eccentricity.
  Speculative moons cached per planet.
- Optional C++ fetch tool (`FetchData`) with a min-heap top-K report.
- Procedural skybox, with galactic-band orientation from each system's
  galactic coordinates.

## 0.3.0: Detail views and classification
- Planet Detail view with a procedural surface and a wireframe
  targeting-grid overlay.
- Composition estimates from density, then radius, then mass; published
  atmosphere detections for about ten planets.
- Host stars rendered and colored by spectral type, with a size
  exaggeration control.
- Moons (real for Sol, speculative and labeled for exoplanets),
  camera easing, log-scale reference rings, multi-star flag.

## 0.2.0: Navigable systems
- Planets grouped into star systems; one system shown at a time.
- Mouse-drag orbit camera and scroll zoom; logarithmic distance option;
  system search and a minimum-planets filter; measured-vs-assumed
  inclination labeling.

## 0.1.0: Initial prototype
- C++20 / CMake / vcpkg project; Kepler orbit propagation.
- NASA Exoplanet Archive fetch script (TAP).
- OpenGL wireframe orbit renderer, CRT post-process, ImGui HUD.
