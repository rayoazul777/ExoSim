# ExoSim

**A C++ orbital simulation and visualization system.** It ingests real astronomical catalogs, propagates orbits, and computes mission-transfer opportunities, and it keeps validated physical data clearly separate from assumptions and visualization choices.

Version 0.6.0 · C++20 · OpenGL 3.3 · [MIT License](LICENSE) · [Changelog](CHANGELOG.md)

<!-- Screenshots and demo video: see "Demo" below. -->

## At a glance

- **Real data, four sources.**
  - NASA Exoplanet Archive: 6,366 planets and 47,903 stellar-host records.
  - NASA/JPL Small-Body Database: dwarf planets.
  - Hipparcos catalog: ~9,000 naked-eye stars with positions, colors, proper motions and parallaxes.
  - Washington Double Star catalog: companion-star separations.
- **Orbital mechanics.**
  - Kepler-equation propagation on real J2000 elements, so Sol's planets sit where they really are on today's date.
  - A Hohmann mission planner that finds real launch windows and computes patched-conic Δv. For example, it finds the late-2026 Mars window: 265 days, 3.68 km/s from low Earth orbit.
- **Validated.**
  - 63 automated checks against conservation laws, closed-form solutions and published astronomical events. Modeled oppositions of Mars, Jupiter and Saturn land within **12–70 minutes** of the published times.
  - Clean under AddressSanitizer and UndefinedBehaviorSanitizer.
  - All shaders pass the Khronos reference compiler.
  - Writing the suite found and fixed four real defects. See [Validation](docs/VALIDATION.md).
- **Honest about uncertainty.** Every value is labeled measured, derived, estimated or illustrative, and the UI never presents a guess as a measurement. See [Data sources](docs/DATA_SOURCES.md).
- **Real-time rendering.** A CRT/vector-display aesthetic, zoom-adaptive procedural surfaces, real ring radii, and the real sky re-projected as seen from each exoplanet host.

## Main engineering contributions

| Area | What was built | Evidence |
|---|---|---|
| Propagation | Kepler solver with sign-aware Newton start and a guaranteed bisection fallback; J2000 heliocentric-ecliptic ephemeris | KEP-1…6, EPH-1…6 |
| Mission analysis | Launch-window root-finding on real, eccentric orbits; patched-conic Δv; synodic periods | MIS-1…7 |
| Data engineering | Four-source ingestion (Python and C++ tools), type-safe parsing, graceful degradation, positional cross-match of 150k double stars in under 0.5 s | DAT-1…5, [Architecture → Performance](docs/ARCHITECTURE.md#performance) |
| Astrometry | Equatorial→ecliptic frame alignment; parallax re-projection of the sky from other stars; linear proper motion from epoch J1991.25 | [Architecture → Frames](docs/ARCHITECTURE.md#time-systems-and-reference-frames) |
| Rendering | GL 3.3 renderer with procedural 3-D-noise surfaces, bump shading, real-radius ring bands, a point-sprite star catalog, and a CRT post-process | 13/13 shaders validated |

## Validation summary

| Check | Result |
|---|---|
| Kepler invariants, e = 0 … 0.99 | Worst relative error ~1e-7 |
| Mars / Jupiter / Saturn opposition times vs. published | 70 / 12 / 31 min |
| 2026 equinox vs. published (precession-corrected) | 33 min |
| Hohmann time and Δv vs. closed form | Exact to reported precision |
| Planned trajectory end point vs. target | < 2e-11 AU |
| Malformed / missing / mistyped input | Handled without crashing |

Full table, methods, tolerances, and what is **not** yet validated:
[docs/VALIDATION.md](docs/VALIDATION.md).

## Platforms

| Platform | Status |
|---|---|
| Windows 10/11, Visual Studio 2022, vcpkg | Primary. Builds and runs; portable zip built by CI. |
| Linux (GCC 13) | Compiles, links, and passes the test suite and sanitizers; rendering not exercised |
| macOS | Untested; likely needs the GLFW forward-compatible core-profile hint |

## Download and run (no build needed)

Each tagged release on GitHub has a portable Windows zip:
`ExoSim-<version>-win64.zip` under **Releases**.

1. Extract the whole folder and double-click `ExoSim.exe`. There is
   nothing to install: libraries and the C++ runtime are linked in, and
   the zip ships a real data snapshot.
2. The build is unsigned, so Windows SmartScreen will warn. Choose
   *More info → Run anyway*.
3. `ExoSim.exe --selftest` checks the install without opening a window.

Requirements: Windows 10/11 x64 with OpenGL 3.3.

## Build and run (Windows)

1. **Install vcpkg once:**
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   C:\vcpkg\vcpkg.exe integrate install
   ```
2. **Fetch data** (optional; Sol works without it):
   ```powershell
   cd ExoSim\data
   pip install certifi          # recommended; fixes TLS verification for VizieR on some installs
   python fetch_exoplanets.py
   ```
   The C++ `FetchData` target does the same job. Useful flags for either
   tool: `--star-vmag-limit 8.0` (fetch ~40k stars instead of 9k),
   `--skip-star-catalog`, `--skip-double-stars`.
3. **Build:** in Visual Studio, File → Open → Folder → `ExoSim`. vcpkg
   restores the dependencies automatically. Then select `ExoSim.exe` and
   press F5.
4. **Test:** build `ExoSimTests`, then run `ctest --output-on-failure`.

**Portable package locally** (same settings CI uses). Set `VCPKG_ROOT`
(e.g. `C:\vcpkg`), then in a Developer PowerShell for VS 2022:
```powershell
cmake --preset windows-portable
cmake --build --preset windows-portable
ctest --preset windows-portable
cd build\windows-portable; cpack -C Release   # -> ExoSim-0.6.0-win64.zip
```
The first configure builds every dependency statically, so it's slow once.

If a data file is missing, only the feature it feeds is disabled. The
console states which one.

## Using it

- **Navigate:** left-drag to orbit the camera, scroll to zoom. Pick a system from the searchable list; filter by planet count.
- **Inspect:** "Inspect Planet" or "Inspect <moon>" opens a close-up with physical data and labeled estimates. Zooming in reveals more surface detail.
- **Plan a mission (Sol):** in the Mission Planner, choose a vehicle and destination, then "Plan next launch window". This shows the launch and arrival dates, transfer time, Δv, arrival speed and window cadence. "Jump to launch" plays the trajectory.
- **Controls:** simulation speed and date ("Today" resets it), logarithmic distance scaling, star-size exaggeration, and a sky-epoch slider that shows stellar proper motion over ±100,000 years.

## Demo

*To be added:* `docs/images/` screenshots and a 60–90 s video. Suggested
sequence:
1. Load a real multi-planet exoplanet system.
2. Show measured vs. estimated values in the detail panel.
3. Toggle log scale and run time forward.
4. Open Sol and plan a Mars mission, showing the launch window, Δv and duration.
5. End on a terminal running `ctest` and the data fetch.

## CI and releases

`.github/workflows/build-and-release.yml` runs on every push and pull request:

1. **Linux:** the validation suite, the same suite under ASan + UBSan, and
   GLSL validation of every shader.
2. **Windows** (only if Linux passes):
   - builds the portable exe and runs the tests;
   - packages the zip (including `LICENSE`);
   - fails if the exe depends on any non-system DLL;
   - extracts the zip and runs `--selftest` from a different directory;
   - uploads the zip as a build artifact.

**To cut a release:**
1. Set `VERSION` in `CMakeLists.txt`.
2. Add a matching `## x.y.z` section to `CHANGELOG.md`.
3. Commit, then run `git tag vX.Y.Z` and `git push origin vX.Y.Z`.

The workflow refuses a tag that doesn't match the CMake version. It then
publishes a GitHub Release with the zip, taking the release notes from
that CHANGELOG section. `0.x` versions are marked as pre-releases.

## Known limitations

- **Two-body motion** only, with Standish elements held fixed. Accuracy is excellent near 2024–2026 but unmeasured for distant eras.
- **Hohmann transfers are an idealization.** There are no gravity assists, so outer-planet times far exceed real missions (Voyager 2 reached Neptune in ~12 yr, where Hohmann takes ~30). The transfer is computed in the ecliptic plane, and no capture burn is modeled.
- **Exoplanet orbital phases are unknown.** Orbit shapes and periods are real; positions along the orbit are not.
- **The simulation clock is not deterministic.** It advances by wall-clock time, so sessions can't be replayed exactly (see [Requirements](docs/REQUIREMENTS.md), ENG-4).
- **Not flight software.** See [Requirements → Out of scope](docs/REQUIREMENTS.md#out-of-scope).

## Roadmap

Priorities favor mission-analysis depth and engineering rigor over more visuals.

1. **Lambert solver and porkchop plots.** Arbitrary-date transfers, and launch/arrival Δv contour maps.
2. **JPL Horizons validation.** Position error envelopes over 1950–2050.
3. **Deterministic fixed-step clock with recorded-input replay**, plus a refactor of `main.cpp` into testable state and UI units (see [Architecture → Coupling](docs/ARCHITECTURE.md#coupling-and-what-id-refactor-first)).
4. **Static analysis in CI** (clang-tidy, clang-format checks), plus a configuration file and structured logging.
5. **Gravity-assist chains** with sphere-of-influence transitions.

## Documentation

| Document | Contents |
|---|---|
| [docs/VALIDATION.md](docs/VALIDATION.md) | Method, full results table, defects found, what isn't validated |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Components, data flow, time systems and frames, assumptions, performance, refactor plan |
| [docs/DATA_SOURCES.md](docs/DATA_SOURCES.md) | Provenance, citations, required acknowledgments, dependency licenses |
| [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) | Requirements with verification traceability and status |
| [CHANGELOG.md](CHANGELOG.md) | Version history |

## Acknowledgments

This research has made use of the NASA Exoplanet Archive, which is
operated by the California Institute of Technology, under contract with
the National Aeronautics and Space Administration under the Exoplanet
Exploration Program. It has also made use of the VizieR catalogue access
tool, CDS, Strasbourg, France. The Hipparcos catalogue is © ESA (1997),
and the Washington Double Star Catalog is maintained by the US Naval
Observatory. Full citations are in [docs/DATA_SOURCES.md](docs/DATA_SOURCES.md).

## License

ExoSim's source code is released under the [MIT License](LICENSE).
Catalog data fetched or bundled from NASA, JPL, CDS/VizieR and others is
**not** covered by that license; it remains under each provider's terms
(see [docs/DATA_SOURCES.md](docs/DATA_SOURCES.md)).
