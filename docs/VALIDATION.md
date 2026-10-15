# Validation

This page documents how ExoSim's physics and data handling were checked,
the measured results, and what has **not** been validated. The results
table below is the verbatim output of `ExoSimTests --markdown`, not a
hand-typed summary. Regenerate it after any change to the core.

## How to reproduce

```powershell
# Visual Studio 2022 (CMake project): build the ExoSimTests target, then
ctest --output-on-failure            # or: ExoSimTests.exe
ExoSimTests.exe --markdown           # prints the table in this document
```

The tests need no GPU and no network. They link only the simulation core
(`KeplerOrbit`, `MissionPlanner`, `Dataset`, `PlanetClassifier`).

**Environment for the results below:** Ubuntu 24.04, GCC 13.3, `-O1`, single
2.8 GHz Xeon core. The same suite was also run under AddressSanitizer +
UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) with 0
failures and no sanitizer reports. It has not yet been run on MSVC; that
is the next step.

## What each group checks

| Group | Method | Why it's meaningful |
|---|---|---|
| **KEP** Kepler propagation | Conservation laws and conic geometry at e = 0, 0.1, 0.5, 0.9, 0.99, plus an inclined orbit | The Kepler-equation solver is private. If it returned a wrong root anywhere on the orbit, angular momentum (Kepler's 2nd law) and vis-viva energy would visibly break. Tested across 720 points per orbit. |
| **EPH** Solar System ephemeris | Model event times vs. published times for the 2026 equinox and perihelion, and the Mars/Jupiter/Saturn oppositions | An *external* check. It exercises the real J2000 elements, epoch handling, and frame, not just internal consistency. |
| **MIS** Mission planner | Transfer time, Δv and synodic period vs. independent closed-form formulas; trajectory end point vs. target position | Confirms the window solver and the transfer ellipse actually connect the two bodies. |
| **TIM** Time conversion | Days-since-J2000 → calendar date, including a leap day and a pre-epoch date | Guards the date shown on every launch window. |
| **DAT** Data loading | Malformed JSON, missing files, rows with missing fields or wrong field types, WDS separation conversion | A failure-handling check: bad input must degrade gracefully, not crash. |
| **CLS** Classifier | Known Solar System bodies classified from their real data | Sanity check that the heuristics give the right answer where the truth is known. |

## Results

| ID | Check | Expected | Computed | Tolerance | Unit | Reference | Result |
|---|---|---|---|---|---|---|---|
| KEP-1 e=0.00 | Periapsis distance a(1-e) at t=0 | 2 | 2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.00 | Apoapsis distance a(1+e) at t=P/2 | 2 | 2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.00 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.00 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 8.89659e-10 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.00 | Worst vis-viva energy error, relative | 0 | 9.75249e-10 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.10 | Periapsis distance a(1-e) at t=0 | 1.8 | 1.8 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.10 | Apoapsis distance a(1+e) at t=P/2 | 2.2 | 2.2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.10 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.10 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 1.2534e-09 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.10 | Worst vis-viva energy error, relative | 0 | 1.43616e-09 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.50 | Periapsis distance a(1-e) at t=0 | 1 | 1 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.50 | Apoapsis distance a(1+e) at t=P/2 | 3 | 3 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.50 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.50 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 1.44715e-09 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.50 | Worst vis-viva energy error, relative | 0 | 1.86518e-09 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.90 | Periapsis distance a(1-e) at t=0 | 0.2 | 0.2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.90 | Apoapsis distance a(1+e) at t=P/2 | 3.8 | 3.8 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.90 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.90 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 2.20412e-09 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.90 | Worst vis-viva energy error, relative | 0 | 2.62037e-09 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.99 | Periapsis distance a(1-e) at t=0 | 0.02 | 0.02 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.99 | Apoapsis distance a(1+e) at t=P/2 | 3.98 | 3.98 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.99 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.99 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 6.66205e-08 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.99 | Worst vis-viva energy error, relative | 0 | 1.31595e-07 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-6 i=30 | Max |z|/r equals sin(i) for an inclined orbit | 0.5 | 0.5 | 0.0001 | - | Analytic (rotation geometry) | PASS |
| EPH-1 | 2026 March equinox (Earth helio. longitude = 180 deg) | 9575.11 | 9575.46 | 0.5 | days since J2000 | Published: 2026-03-20 14:46 UTC | PASS |
| EPH-1b | 2026 March equinox, corrected for precession of the equinoxes | 9575.49 | 9575.46 | 0.1 | days since J2000 | Published time + 50.29"/yr general precession | PASS |
| EPH-2 | 2026 perihelion date | 9499.22 | 9499.17 | 2 | days since J2000 | Published: 2026-01-03 17:16 UTC (Earth centre; model is EM barycentre) | PASS |
| EPH-3 | Perihelion distance a(1-e) | 0.98329 | 0.983291 | 0.0001 | AU | Published: 0.98329 AU (Standish a, e) | PASS |
| EPH-4 | Mars opposition (Jan 2025) | 9146.61 | 9146.56 | 2 | days since J2000 | Published: 2025-01-16 02:32 UTC | PASS |
| EPH-5 | Jupiter opposition (Dec 2024) | 9107.38 | 9107.37 | 2 | days since J2000 | Published: 2024-12-07 ~21:00 UTC | PASS |
| EPH-6 | Saturn opposition (Sep 2025) | 9394.71 | 9394.69 | 2 | days since J2000 | Published: 2025-09-21 ~05:00 UTC | PASS |
| MIS-1 Mars | Transfer time, circular orbits | 258.915 | 258.915 | 0.001 | days | Closed-form Hohmann: pi*sqrt(a^3/mu) | PASS |
| MIS-2 Mars | Departure dv from 200 km LEO | 3.61173 | 3.61173 | 0.0001 | km/s | Closed-form patched conic | PASS |
| MIS-3 Mars | Synodic period | 779.907 | 779.907 | 1e-06 | days | Closed-form 1/|1/P1-1/P2| | PASS |
| MIS-4 Mars | Arrival miss distance (trajectory hits target) | 0 | 3.0168e-12 | 0.0001 | AU | Consistency (window solver) | PASS |
| MIS-1 Venus | Transfer time, circular orbits | 146.033 | 146.033 | 0.001 | days | Closed-form Hohmann: pi*sqrt(a^3/mu) | PASS |
| MIS-2 Venus | Departure dv from 200 km LEO | 3.50443 | 3.50443 | 0.0001 | km/s | Closed-form patched conic | PASS |
| MIS-3 Venus | Synodic period | 583.932 | 583.932 | 1e-06 | days | Closed-form 1/|1/P1-1/P2| | PASS |
| MIS-4 Venus | Arrival miss distance (trajectory hits target) | 0 | 9.99206e-13 | 0.0001 | AU | Consistency (window solver) | PASS |
| MIS-1 Jupiter | Transfer time, circular orbits | 997.53 | 997.53 | 0.001 | days | Closed-form Hohmann: pi*sqrt(a^3/mu) | PASS |
| MIS-2 Jupiter | Departure dv from 200 km LEO | 6.30485 | 6.30485 | 0.0001 | km/s | Closed-form patched conic | PASS |
| MIS-3 Jupiter | Synodic period | 398.876 | 398.876 | 1e-06 | days | Closed-form 1/|1/P1-1/P2| | PASS |
| MIS-4 Jupiter | Arrival miss distance (trajectory hits target) | 0 | 1.59255e-11 | 0.0001 | AU | Consistency (window solver) | PASS |
| MIS-5 | Earth->Moon Hohmann time | 4.98 | 4.97795 | 0.01 | days | Closed-form; cf. Artemis I ~5-day outbound coast | PASS |
| MIS-6 | Trans-lunar injection dv from 200 km LEO | 3.13 | 3.13135 | 0.01 | km/s | Closed-form; typical TLI ~3.1 km/s | PASS |
| MIS-7 | First Mars window after 2026-01-01 (plausibility) | 9800 | 9804.16 | 60 | days since J2000 | Real-world 2026 Mars window: late 2026 (approx. Nov) | PASS |
| TIM-1 | Calendar date for day 0.000000 -> 2000-01-01 (got 2000-01-01) | 1 | 1 | 0 | match | Python datetime reference | PASS |
| TIM-2 | Calendar date for day 8825.000000 -> 2024-02-29 (got 2024-02-29) | 1 | 1 | 0 | match | Python datetime reference | PASS |
| TIM-3 | Calendar date for day 9497.000000 -> 2026-01-01 (got 2026-01-01) | 1 | 1 | 0 | match | Python datetime reference | PASS |
| TIM-4 | Calendar date for day -0.600000 -> 1999-12-31 (got 1999-12-31) | 1 | 1 | 0 | match | Python datetime reference | PASS |
| DAT-1 | WDS projected separation = arcsec x pc | 100 | 100 | 1e-09 | AU | Definition of the parsec | PASS |
| DAT-2 | No WDS match -> separation left unknown (0) | 0 | 0 | 0 | AU | Expected behaviour | PASS |
| DAT-3 | Malformed JSON file rejected without crashing | 0 | 0 | 0 | loaded | Expected behaviour | PASS |
| DAT-4 | Missing file rejected without crashing | 0 | 0 | 0 | loaded | Expected behaviour | PASS |
| DAT-5 | Rows missing orbital elements are skipped | 1 | 1 | 0 | rows | Expected behaviour | PASS |
| CLS-1 Mercury | Classified as rocky (from real density) | 1 | 1 | 0 | enum | Known composition | PASS |
| CLS-1 Venus | Classified as rocky (from real density) | 1 | 1 | 0 | enum | Known composition | PASS |
| CLS-1 Earth | Classified as rocky (from real density) | 1 | 1 | 0 | enum | Known composition | PASS |
| CLS-1 Mars | Classified as rocky (from real density) | 1 | 1 | 0 | enum | Known composition | PASS |
| CLS-2 Jupiter | Classified as gas giant | 5 | 5 | 0 | enum | Known composition | PASS |
| CLS-2 Saturn | Classified as gas giant | 5 | 5 | 0 | enum | Known composition | PASS |

**63 checks, 0 failed.**

## Reading the ephemeris results

Sol's planets are propagated as fixed two-body orbits from JPL's approximate
J2000 elements (Standish). Against published event times:

- **Oppositions** (Mars Jan 2025, Jupiter Dec 2024, Saturn Sep 2025) land
  within **12 min (Jupiter), 31 min (Saturn) and 70 min (Mars)** of the
  published times.
- **The 2026 March equinox** crossing lands 0.35 days after the published
  time (EPH-1). This is expected, not an error. The model's frame is the
  *fixed* J2000 equinox, while published equinoxes use the equinox *of
  date*, which precesses at about 50.29″/yr. That is 0.366° over 26.2
  years, which Earth covers in 0.37 days. With that correction applied
  (EPH-1b), the residual is **0.023 days (≈33 min)**.
- **Perihelion 2026** is within 0.05 days of the published time. The
  tolerance is looser (±2 days) for a physical reason: Standish's Earth
  elements describe the Earth–Moon *barycentre*, while published
  perihelion times refer to Earth's centre, and the Moon can shift Earth's
  perihelion by a day or more.

Tolerances were chosen from these physical arguments *before* looking at
the results, not tuned to make checks pass.

## Defects the suite found (and fixed)

Writing the suite surfaced four real defects in code that had previously
only been compile-checked:

1. **Kepler solver returned wrong roots at high eccentricity (KEP-4/5).**
   For e ≥ 0.8, Newton's method always started at E = +π. For negative mean
   anomaly (just past apoapsis), that threw the iteration toward E ≈ 0,
   where the derivative 1 − e·cos E nearly vanishes, and it converged to
   the wrong root. Positions jumped by up to ~3.6 AU. Real exoplanets have
   orbits like this (e.g. HD 80606 b, e ≈ 0.93), so the app drew them
   incorrectly. **Fix:** start at sign(M)·π (monotonic convergence for all
   e < 1), plus a guaranteed bisection fallback. The worst relative
   conservation error at e = 0.99 is now ~1e-7.
2. **One malformed catalog row crashed the whole app (DAT-1).** A numeric
   field arriving as a string (or a `null` name) threw an uncaught JSON
   type exception. **Fix:** type-checked field access that falls back to
   defaults.
3. **Jupiter was classified as a mini-Neptune (CLS-2).** Its bulk density
   (1.33 g/cm³) overlaps icy planets. **Fix:** radius takes precedence
   at ≥ 6 Earth radii, following NASA's size categories (Jupiter-size 6–15
   R⊕). Nothing rocky or icy is that large.
4. **Starfield tilted 23.4° from the planetary orbits** (found while
   documenting frames, verified with a separate check). Stars were placed
   in the equatorial frame while the planets were ecliptic. **Fix:** an
   equatorial→ecliptic rotation by the J2000 obliquity. The north
   ecliptic pole (RA 270°, Dec +66.56°) now maps exactly to world "up".

## Other verification

- **Every source file** compiles with `-Wall -Wextra` and zero warnings,
  and the full application links (GCC 13, GLFW 3.3, Dear ImGui, a
  glad-generated GL 3.3 loader, libcurl).
- **All 13 GLSL shaders** pass the Khronos reference validator
  (`glslangValidator`). This caught a function named `noise3` that
  collided with a GLSL built-in and would have failed at runtime on every
  GPU.

- **Packaging:** the configure → build → install → CPack → extract →
  `--selftest` chain was run end-to-end with real CMake on Linux. Because
  Ubuntu provides no CMake packages for ImGui and glad, stand-in package
  configs were used for those two only. The workflow's pass/fail logic
  (release-notes extraction, tag/version check, DLL detector) was tested
  against realistic inputs.

## What is NOT validated

Stated plainly, so nothing here is over-read:

- **No comparison against JPL Horizons yet.** The ephemeris checks use
  published event *times*, not full state vectors. A Horizons comparison
  of heliocentric positions over 1950–2050, with error envelopes, is the
  most valuable next validation step.
- **Error growth away from J2000 is not characterized.** The secular rates
  in Standish's table are not applied, so accuracy degrades for dates far
  from 2000. It is excellent for 2024–2026 per the results above; other
  eras are unmeasured.
- **Rendering is not verified on a GPU in this environment.** It compiles,
  links and passes shader validation; visual correctness is checked by eye
  on the Windows build.
- **Live data-source responses** (VizieR Hipparcos/WDS columns in
  particular) are parsed defensively but were not exercised from this
  environment. On a column mismatch, the fetch prints the actual header.
- **Mission planner accuracy vs. real missions** is only a plausibility
  check (MIS-7): Hohmann transfers are an idealization (see
  `docs/ARCHITECTURE.md` → Assumptions).
- **The MSVC/Windows test run** and the Windows CI job have not run yet.
  Their first run happens on the first push to GitHub.
