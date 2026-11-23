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
(the `ExoSimCore` library: no OpenGL, no ImGui).

**Environment for the results below:** Ubuntu 24.04, GCC 13.3, `-O1`, single
2.8 GHz Xeon core. The same suite was also run under AddressSanitizer +
UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) with 0
failures and no sanitizer reports, and clang-tidy-18 reports no findings
(see `.clang-tidy` for the enabled checks). It has not yet been run on MSVC; that
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
| **EPH-H** JPL Horizons | Heliocentric positions of all eight planets at 51 epochs, 1950–2050, vs. JPL Horizons (DE441) | The authoritative comparison: full position vectors over a century, not event times near one date. |
| **LAM** Lambert / porkchop | Curtis Example 5.2; analytic ellipses; state↔elements round trips; 2020 and 2026 Mars windows; propagated miss distance at arrival | Checks the solver against a textbook case, against itself (round trips), and against a real launch window (Perseverance). |
| **GA** Gravity assists | Laplace sphere-of-influence radii; flyby turn-angle identity; burn accounting; hyperbolic energy conservation; Voyager-era and New Horizons-era chains | Confirms the patched-conic flyby physics, then that the optimizer rediscovers real mission dates. |
| **ENG** Engineering | Recorded-session replay (hash, time and tick reproduced; a tampered recording detected); formatting; sky model; config validation; log output | The determinism and replay properties ENG-4 requires, tested end to end. |
| **GAL** Milky Way map | Sgr A* and the NGP in Galactic coordinates; RA/Dec→(l, b) vs. the Archive's own values; host loading and distance fallback; arm geometry vs. measured crossings; model determinism and shape | Real positions are checked against independent values; the illustrative model is checked for the properties it claims. |
| **FSW** Flight software | Entry speed and orbital speed vs. vis-viva; touchdown speed and duration vs. Perseverance, Apollo 11, Huygens and Venera; guidance accuracy; capture-Δv identity and Juno JOI; light time; MET format; clock independence of frame pacing and exact event stops; timeline ordering and phases; a bit-for-bit re-run | Checks the descent physics against real missions, and checks the properties flight software depends on: determinism, sequencing and timing. The published comparisons are plausibility-level, because the vehicle parameters are representative of a class, not exact. |

## Results

| ID | Check | Expected | Computed | Tolerance | Unit | Reference | Result |
|---|---|---|---|---|---|---|---|
| KEP-1 e=0.00 | Periapsis distance a(1-e) at t=0 | 2 | 2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.00 | Apoapsis distance a(1+e) at t=P/2 | 2 | 2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.00 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.00 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 8.8965885e-10 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.00 | Worst vis-viva energy error, relative | 0 | 9.7524949e-10 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.10 | Periapsis distance a(1-e) at t=0 | 1.8 | 1.8 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.10 | Apoapsis distance a(1+e) at t=P/2 | 2.2 | 2.2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.10 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.10 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 1.2533989e-09 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.10 | Worst vis-viva energy error, relative | 0 | 1.4361606e-09 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.50 | Periapsis distance a(1-e) at t=0 | 1 | 1 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.50 | Apoapsis distance a(1+e) at t=P/2 | 3 | 3 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.50 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.50 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 1.4471523e-09 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.50 | Worst vis-viva energy error, relative | 0 | 1.8651768e-09 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.90 | Periapsis distance a(1-e) at t=0 | 0.2 | 0.2 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.90 | Apoapsis distance a(1+e) at t=P/2 | 3.8 | 3.8 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.90 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.90 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 2.2041186e-09 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.90 | Worst vis-viva energy error, relative | 0 | 2.6203658e-09 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-1 e=0.99 | Periapsis distance a(1-e) at t=0 | 0.02 | 0.02 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-2 e=0.99 | Apoapsis distance a(1+e) at t=P/2 | 3.98 | 3.98 | 1e-09 | AU | Analytic (conic geometry) | PASS |
| KEP-3 e=0.99 | Period closure |r(P) - r(0)| | 0 | 0 | 1e-09 | AU | Analytic (periodicity) | PASS |
| KEP-4 e=0.99 | Angular momentum spread (Kepler's 2nd law), relative | 0 | 6.6620526e-08 | 1e-05 | - | Analytic (conservation law) | PASS |
| KEP-5 e=0.99 | Worst vis-viva energy error, relative | 0 | 1.3159472e-07 | 1e-05 | - | Analytic (vis-viva equation) | PASS |
| KEP-6 i=30 | Max |z|/r equals sin(i) for an inclined orbit | 0.5 | 0.5 | 0.0001 | - | Analytic (rotation geometry) | PASS |
| EPH-1 | 2026 March equinox (Earth helio. longitude = 180 deg) | 9575.115 | 9575.476 | 0.5 | days since J2000 | Published: 2026-03-20 14:46 UTC | PASS |
| EPH-1b | 2026 March equinox, corrected for precession of the equinoxes | 9575.4866 | 9575.476 | 0.1 | days since J2000 | Published time + 50.29"/yr general precession | PASS |
| EPH-2 | 2026 perihelion date | 9499.219 | 9499.26 | 2 | days since J2000 | Published: 2026-01-03 17:16 UTC (Earth centre; model is EM barycentre) | PASS |
| EPH-3 | Perihelion distance a(1-e) | 0.98329 | 0.9833042 | 0.0001 | AU | Published: 0.98329 AU (Standish a, e) | PASS |
| EPH-4 | Mars opposition (Jan 2025) | 9146.606 | 9146.6093 | 2 | days since J2000 | Published: 2025-01-16 02:32 UTC | PASS |
| EPH-5 | Jupiter opposition (Dec 2024) | 9107.375 | 9107.3274 | 2 | days since J2000 | Published: 2024-12-07 ~21:00 UTC | PASS |
| EPH-6 | Saturn opposition (Sep 2025) | 9394.708 | 9394.814 | 2 | days since J2000 | Published: 2025-09-21 ~05:00 UTC | PASS |
| MIS-1 Mars | Transfer time, circular orbits | 258.91515 | 258.91515 | 0.001 | days | Closed-form Hohmann: pi*sqrt(a^3/mu) | PASS |
| MIS-2 Mars | Departure dv from 200 km LEO | 3.6117333 | 3.6117333 | 0.0001 | km/s | Closed-form patched conic | PASS |
| MIS-3 Mars | Synodic period | 779.90689 | 779.90689 | 1e-06 | days | Closed-form 1/|1/P1-1/P2| | PASS |
| MIS-4 Mars | Arrival miss distance (trajectory hits target) | 0 | 3.0168004e-12 | 0.0001 | AU | Consistency (window solver) | PASS |
| MIS-1 Venus | Transfer time, circular orbits | 146.03312 | 146.03312 | 0.001 | days | Closed-form Hohmann: pi*sqrt(a^3/mu) | PASS |
| MIS-2 Venus | Departure dv from 200 km LEO | 3.5044305 | 3.5044305 | 0.0001 | km/s | Closed-form patched conic | PASS |
| MIS-3 Venus | Synodic period | 583.93223 | 583.93223 | 1e-06 | days | Closed-form 1/|1/P1-1/P2| | PASS |
| MIS-4 Venus | Arrival miss distance (trajectory hits target) | 0 | 9.9920558e-13 | 0.0001 | AU | Consistency (window solver) | PASS |
| MIS-1 Jupiter | Transfer time, circular orbits | 997.53033 | 997.53033 | 0.001 | days | Closed-form Hohmann: pi*sqrt(a^3/mu) | PASS |
| MIS-2 Jupiter | Departure dv from 200 km LEO | 6.3048481 | 6.3048481 | 0.0001 | km/s | Closed-form patched conic | PASS |
| MIS-3 Jupiter | Synodic period | 398.87645 | 398.87645 | 1e-06 | days | Closed-form 1/|1/P1-1/P2| | PASS |
| MIS-4 Jupiter | Arrival miss distance (trajectory hits target) | 0 | 1.5925532e-11 | 0.0001 | AU | Consistency (window solver) | PASS |
| MIS-5 | Earth->Moon Hohmann time | 4.98 | 4.9779549 | 0.01 | days | Closed-form; cf. Artemis I ~5-day outbound coast | PASS |
| MIS-6 | Trans-lunar injection dv from 200 km LEO | 3.13 | 3.131345 | 0.01 | km/s | Closed-form; typical TLI ~3.1 km/s | PASS |
| MIS-7 | First Mars window after 2026-01-01 (plausibility) | 9800 | 9804.2251 | 60 | days since J2000 | Real-world 2026 Mars window: late 2026 (approx. Nov) | PASS |
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
| FSW-1 | Mars entry speed = sqrt(vInf^2 + 2mu/r) at entry interface | 5596.6393 | 5596.6393 | 1e-06 | m/s | Analytic (vis-viva, hyperbolic approach) | PASS |
| FSW-2 | Mars EDL lands (powered descent, ZEM/ZEV guidance) | 1 | 1 | 0 | bool | Expected behaviour | PASS |
| FSW-3 | Mars touchdown speed | 0.75 | 0.74998769 | 0.1 | m/s | Perseverance sky-crane touchdown ~0.75 m/s (NASA/JPL) | PASS |
| FSW-4 | Mars parachute deploy altitude (plausibility) | 11 | 9.5 | 4 | km | Perseverance: ~11 km (NASA/JPL); simplified lift-up entry | PASS |
| FSW-5 | Mars landing error vs. guidance target | 0 | 0.051524733 | 10 | m | Closed-loop guidance accuracy | PASS |
| FSW-6 | Lunar descent starts at circular orbital speed | 1672.6515 | 1672.6515 | 1e-06 | m/s | Analytic sqrt(mu/r) | PASS |
| FSW-7 | Lunar powered descent duration (PDI to touchdown) | 756 | 720.03 | 120 | s | Apollo 11: PDI to landing 12 min 36 s | PASS |
| FSW-8 | Lunar touchdown speed | 0.9 | 0.90000003 | 0.2 | m/s | Apollo LM: ~0.9 m/s contact | PASS |
| FSW-9 | Lunar landing error vs. guidance target | 0 | 1.248933 | 10 | m | Closed-loop guidance accuracy | PASS |
| FSW-10 | Titan touchdown speed under drogue chute | 4.5 | 4.4498975 | 0.7 | m/s | Huygens 2005: ~4.5 m/s (ESA) | PASS |
| FSW-11 | Titan descent duration (entry to touchdown) | 8820 | 10028.1 | 2.16e+03 | s | Huygens: ~2 h 27 min under parachutes (ESA) | PASS |
| FSW-12 | Venus probe lands below 8 m/s on its drag disk | 1 | 1 | 0 | bool | Venera landers: ~7-8 m/s | PASS |
| FSW-13 | No landing on a body without a solid surface | 0 | 0 | 0 | bool | Expected behaviour | PASS |
| FSW-14 | Landing simulation is deterministic (re-run, bitwise) | 0 | 0 | 0 | differing samples | ENG-4 | PASS |
| FSW-15 | Capture dv identity (vInf=0, circular): (sqrt2-1) v_circ | 0.67661956 | 0.67661956 | 1e-12 | km/s | Analytic | PASS |
| FSW-16 | Jupiter capture into 1.05 x 100 R orbit (vInf 5.6 km/s) | 0.54 | 0.5723202 | 0.15 | km/s | Juno JOI 2016: 0.54 km/s (similar orbit) | PASS |
| FSW-17 | One-way light time over 1 AU | 499.00478 | 499.00478 | 1e-05 | s | IAU: 1 AU / c = 499.004784 s | PASS |
| FSW-18 | MET formatting T+003d 04:05:06 (got T+003d 04:05:06) | 1 | 1 | 0 | bool | Format spec | PASS |
| FSW-19 | Clock: sim time independent of frame pacing | 1 | 1 | 1e-12 | days | Fixed-tick design | PASS |
| FSW-20 | Clock: lands exactly on the next event and drops to real time | 0.123456 | 0.123456 | 0 | days | Warp-to-event behaviour | PASS |
| FSW-21 | Mission timeline events are in time order | 1 | 1 | 0 | bool | Expected behaviour | PASS |
| FSW-22 | Phase at mid-transfer is CRUISE | 2 | 2 | 0 | enum | Expected behaviour | PASS |
| FSW-23 | Phase after the landing sequence is SURFACE OPS | 6 | 6 | 0 | enum | Expected behaviour | PASS |
| FSW-24 | Mars direct entry needs no capture burn | 0 | 0 | 0 | km/s | Perseverance: direct entry | PASS |
| ENG-1 | Scripted session reaches the landing event stop | 1 | 1 | 0 | bool | Expected behaviour (exercises warp-to-event) | PASS |
| ENG-2 | Recording round-trips through its JSON-lines file | 1 | 1 | 0 | bool | Format spec (Replay.h) | PASS |
| ENG-3 | Replay reproduces the final state hash | 1 | 1 | 0 | bool | ENG-4 (deterministic replay) | PASS |
| ENG-4 | Replay reproduces the final simulation time bit-for-bit | 0 | 0 | 0 | days | ENG-4 | PASS |
| ENG-5 | Replay ends on the recorded tick | 6684 | 6684 | 0 | ticks | ENG-4 | PASS |
| ENG-6 | Mid-session recording (active mission) replays to the same hash | 1 | 1 | 0 | bool | ENG-4 | PASS |
| ENG-7 | A changed command changes the replayed hash | 1 | 1 | 0 | bool | Expected behaviour (hash covers the state) | PASS |
| ENG-8 | Unknown command in a recording is rejected with an error | 0 | 0 | 0 | bool | Failure handling | PASS |
| ENG-9 | Read-only access does not perturb the simulation | 1 | 1 | 0 | bool | Design (SimulationState API) | PASS |
| ENG-10 | UTC formatting at J2000 epoch (got 2000-01-01 12:00:00) | 1 | 1 | 0 | bool | J2000 = 2000-01-01 12:00 TT (approx. UTC) | PASS |
| ENG-11 | Duration picks units: 7.9 days | 1 | 1 | 0 | bool | Format spec | PASS |
| ENG-12 | Warp label for real time | 1 | 1 | 0 | bool | Format spec | PASS |
| ENG-13 | Case-insensitive search | 1 | 1 | 0 | bool | Expected behaviour | PASS |
| ENG-14 | Sun's apparent magnitude from 10 pc | 4.83 | 4.83 | 1e-06 | mag | Definition of absolute magnitude (float direction vector) | PASS |
| ENG-15 | Parallax re-projection dims a star by the inverse-square law | 4.5953646 | 4.5953646 | 1e-06 | px | m = M + 5 log10(d/10pc) | PASS |
| ENG-16 | Empty config keeps every default | 1 | 1 | 0 | bool | Config.h defaults = built-in behaviour | PASS |
| ENG-17 | Config: valid key applied, bad type/range/unknown key each warned | 1 | 1 | 0 | bool | Config.h | PASS |
| ENG-18 | Malformed config is rejected and defaults kept | 0 | 0 | 0 | bool | Failure handling | PASS |
| ENG-19 | Structured log line is valid, escaped JSON | 1 | 1 | 0 | bool | JSON (RFC 8259) escaping | PASS |
| EPH-H0 | Horizons reference file loaded (8 planets x 51 epochs) | 408 | 408 | 0 | samples | tests/data/horizons_planets_1950_2050.csv | PASS |
| EPH-H1 | Mercury: max heliocentric angle error 1950-2050 vs Horizons | 0 | 19.077536 | 60 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H2 | Mercury: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H3 | Venus: max heliocentric angle error 1950-2050 vs Horizons | 0 | 17.89838 | 60 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H4 | Venus: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H5 | Earth: max heliocentric angle error 1950-2050 vs Horizons | 0 | 21.805589 | 60 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H6 | Earth: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H7 | Mars: max heliocentric angle error 1950-2050 vs Horizons | 0 | 77.32124 | 120 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H8 | Mars: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H9 | Jupiter: max heliocentric angle error 1950-2050 vs Horizons | 0 | 508.92589 | 700 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H10 | Jupiter: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H11 | Saturn: max heliocentric angle error 1950-2050 vs Horizons | 0 | 553.27406 | 1.5e+03 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H12 | Saturn: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H13 | Uranus: max heliocentric angle error 1950-2050 vs Horizons | 0 | 110.40816 | 200 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H14 | Uranus: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H15 | Neptune: max heliocentric angle error 1950-2050 vs Horizons | 0 | 58.039245 | 100 | arcsec | JPL Horizons DE441; limit = Standish accuracy class | PASS |
| EPH-H16 | Neptune: max error with rates <= 1.1 x v0.7 model | 1 | 1 | 0 | bool | Before/after comparison | PASS |
| EPH-H17 | Fixture sanity: all reference distances within perihelion-aphelion | 0 | 0 | 0 | samples | Conic geometry | PASS |
| LAM-1 | Curtis Example 5.2: departure velocity v1 | 0 | 5.197601e-05 | 0.001 | km/s | Curtis (2014) Ex. 5.2 | PASS |
| LAM-2 | Curtis Example 5.2: arrival velocity v2 | 0 | 4.3364319e-05 | 0.001 | km/s | Curtis (2014) Ex. 5.2 | PASS |
| LAM-3 | Analytic ellipse: perihelion speed recovered | 32.730747 | 32.730747 | 1e-06 | km/s | Vis-viva | PASS |
| LAM-4 | Analytic ellipse: departure is tangential (radial component) | 0 | 1.4828389e-11 | 1e-06 | km/s | Periapsis geometry | PASS |
| LAM-5 | Stumpff C(z) continuous through z=0 | 0.5 | 0.5 | 1e-08 |  | C(0) = 1/2 | PASS |
| LAM-6 | Stumpff S(z) continuous through z=0 | 0.16666667 | 0.16666667 | 1e-08 |  | S(0) = 1/6 | PASS |
| LAM-7 | Analytic planet velocity vs numerical derivative | 0 | 7.3996715e-05 | 0.0001 | km/s | Finite difference | PASS |
| LAM-8 | State->elements round trip (7-day propagation vs ephemeris) | 0 | 59.590856 | 2e+03 | km | Two-body consistency (residual = secular drift + GM rounding) | PASS |
| LAM-9 | State->elements: eccentricity recovered (two-body orbit) | 0.23 | 0.23 | 1e-10 |  | Exact inverse | PASS |
| LAM-10 | State->elements: semi-major axis, inclination, node, perihelion recovered | 0 | 9.015011e-14 | 1e-08 | sum |error| | Exact inverse | PASS |
| LAM-11 | 2020 Mars window: optimal launch date (got 2020-08-01) | 7515.5 | 7517.5167 | 45 | days since J2000 | Perseverance launch 2020-07-30 (NASA) | PASS |
| LAM-12 | 2020 Mars window: time of flight | 203 | 208.80305 | 60 | days | Perseverance cruise 203 days (NASA) | PASS |
| LAM-13 | 2020 Mars window: launch energy C3 (plausibility) | 14 | 14.99373 | 6 | km^2/s^2 | Mars 2020 launch period C3 ~ 8-20 | PASS |
| LAM-14 | Lambert transfer is 3-D (inclined to the ecliptic) | 1 | 1 | 0 | bool | Mars orbit inclination 1.85 deg | PASS |
| LAM-15 | Planned transfer arrives at Mars (propagated miss distance) | 0 | 0.00018901473 | 1 | km | Lambert boundary condition | PASS |
| LAM-16 | 2026 window: Lambert launch near the Hohmann window | 9804.2251 | 9800.538 | 45 | days | Same synodic window | PASS |
| LAM-17 | 2026 window: departure burn within 0.6 km/s of Hohmann | 3.6818581 | 3.6368163 | 0.6 | km/s | Hohmann is the coplanar circular limit | PASS |
| LAM-18 | Jupiter Lambert transfer arrives (propagated miss distance) | 0 | 0.00022747829 | 10 | km | Lambert boundary condition | PASS |
| LAM-19 | Jupiter direct transfer C3 (plausibility) | 80 | 82.851958 | 15 | km^2/s^2 | Direct Earth-Jupiter C3 ~ 75-90 (Juno used an Earth flyby instead) | PASS |
| GA-1 | Earth's sphere of influence (Laplace) | 925000 | 924649.21 | 5e+03 | km | Textbook value ~925,000 km (Curtis Table 8.1) | PASS |
| GA-2 | Jupiter's sphere of influence | 48200000 | 48205582 | 5e+05 | km | Textbook value 48.2 million km (Curtis Table 8.1) | PASS |
| GA-3 | Ballistic flyby (|v_in| = |v_out|, turn < max) needs no burn | 0 | 0 | 1e-06 | km/s | Patched conic: a flyby conserves |v_inf| | PASS |
| GA-4 | Flyby periapsis gives exactly the required turn: 2 asin(1/e) | 0.5 | 0.5 | 1e-09 | rad | Hyperbola asymptote geometry | PASS |
| GA-5 | Turn beyond what Mars can provide is charged as a burn | 1 | 1 | 0 | bool | Turn-deficit model | PASS |
| GA-6 | Universal-variable propagation vs Kepler (ellipse, 400 days) | 0 | 1.9969329e-07 | 0.001 | km | Two-body identity | PASS |
| GA-7 | Hyperbolic propagation conserves energy (relative) | 0 | 2.220446e-16 | 1e-09 |  | Two-body invariant | PASS |
| GA-8 | Voyager-era EJS: launch date (got 1977-09-05) | -8161.5 | -8153 | 75 | days since J2000 | Voyager 2 / 1 launched 1977-08-20 / 09-05 (NASA) | PASS |
| GA-9 | Voyager-era EJS: Jupiter flyby date | -7544.5 | -7412.6368 | 180 | days since J2000 | Voyager 1 / 2 Jupiter flybys 1979-03-05 / 07-09 | PASS |
| GA-10 | Gravity assist reaches Saturn faster than a direct Hohmann transfer | 1 | 1 | 0 | bool | Voyager: ~3.2-4 yr vs ~6 yr Hohmann | PASS |
| GA-11 | Voyager-era EJS: Jupiter flyby is (nearly) ballistic | 0 | 0.12509479 | 0.5 | km/s | Voyager made no large burn at Jupiter | PASS |
| GA-12 | Voyager-era EJS: launch energy C3 (plausibility) | 105 | 89.226308 | 25 | km^2/s^2 | Voyager C3 ~100-110 (Titan IIIE-Centaur) | PASS |
| GA-13 | New Horizons-era EJP: Jupiter flyby date | 2614.5 | 2621.7757 | 200 | days since J2000 | New Horizons at Jupiter 2007-02-28 | PASS |
| GA-14 | New Horizons-era EJP: launch date | 2209.5 | 2209.0625 | 150 | days since J2000 | New Horizons launched 2006-01-19 | PASS |
| GA-15 | New Horizons-era EJP: launch energy C3 (plausibility) | 158 | 153.51563 | 35 | km^2/s^2 | New Horizons C3 157.7 (record at the time) | PASS |
| GAL-1 | Sgr A* Galactic longitude | 359.944 | 359.94425 | 0.002 | deg | Reid & Brunthaler 2004 | PASS |
| GAL-2 | Sgr A* Galactic latitude | -0.046 | -0.04615932 | 0.002 | deg | Reid & Brunthaler 2004 | PASS |
| GAL-3 | North Galactic Pole latitude | 90 | 90 | 1e-06 | deg | IAU 1958 / J2000 definition | PASS |
| GAL-4 | RA/Dec -> (l, b) vs the Archive's glon/glat, worst row | 0 | 7.7658407e-06 | 0.0001 | deg | NASA Exoplanet Archive (tests/data/hosts_sample.json) | PASS |
| GAL-5 | l = 0 points along +X (toward the Galactic Center) | 100 | 100 | 1e-09 | pc | frame definition | PASS |
| GAL-6 | l = 90 points along +Y (direction of rotation) | 100 | 100 | 1e-09 | pc | frame definition | PASS |
| GAL-7 | Galactocentric radius of the Sun | 8178 | 8178 | 1e-09 | pc | GRAVITY Collaboration 2019 (R0 = 8.178 kpc) | PASS |
| GAL-8 | Hosts from `ps` alone (one per hostname; no-distance hosts skipped) | 2 | 2 | 0 | hosts | tests/data/hosts_sample.json | PASS |
| GAL-8b | stellarhosts fallback adds TRAPPIST-1; Kepler-1813 still unplotted | 13 | 13 | 0 | hosts | Archive stellarhosts sy_dist | PASS |
| GAL-9 | TRAPPIST-1 planet count (rows grouped by host) | 7 | 7 | 0 | planets | Gillon et al. 2017 | PASS |
| GAL-10 | TRAPPIST-1 position magnitude equals its stellarhosts distance | 12.4299 | 12.4299 | 1e-09 | pc | Gaia DR3 via Archive stellarhosts | PASS |
| GAL-11 | Missing host file -> empty list (no crash) | 0 | 0 | 0 | hosts | robustness | PASS |
| GAL-12 | Arm centerlines cross the Sun-center line at their measured radii (worst arm) | 0 | 0.00043298068 | 0.005 | fraction | Reid et al. 2019 arm crossings | PASS |
| GAL-13 | Measured crossings consistent with one 4-arm log spiral (worst ratio error) | 0 | 0.00038412114 | 0.02 | fraction | pitch 12.5 deg (Reid et al. 2019) | PASS |
| GAL-14 | Arms trail the clockwise rotation (azimuth grows outward) | 1 | 1 | 0 | bool | spiral-arm winding sense | PASS |
| GAL-15 | Model generation is deterministic for a seed | 1 | 1 | 0 | bool | reproducible frames | PASS |
| GAL-16 | A different seed gives a different field | 1 | 1 | 0 | bool | seeded RNG | PASS |
| GAL-17 | Surface density 3-5 kpc / 11-13 kpc (exponential disk, h = 2.6 kpc: > 5) | 1 | 1 | 0 | bool | Bland-Hawthorn & Gerhard 2016 | PASS |
| GAL-18 | Mean |z| of disk stars near the solar circle (thin disk) | 250 | 118.901 | 150 | pc | scale height ~300 pc | PASS |
| GAL-19 | Bar's near end lies at positive longitude (+Y) | 1 | 1 | 0 | bool | bar angle ~27 deg, near end at l > 0 | PASS |
| GAL-20 | Teff 5772 K maps to the Sun's B-V color | 0 | 0.00080831774 | 0.03 | rgb | Ballesteros 2012 (B-V 0.653) | PASS |
| GAL-21 | Hot stars bluer, cool stars redder | 1 | 1 | 0 | bool | stellar color sequence | PASS |
| GAL-22 | Hipparcos star at 100 mas -> 10 pc toward l = 0; 0.5 mas excluded | 10 | 9.999992 | 0.01 | pc | d = 1000 / parallax | PASS |

**181 checks, 0 failed.**

## Reading the ephemeris results

Sol's planets are propagated as two-body orbits from JPL's approximate
elements (Standish Table 1, valid 1800–2050). Since 0.8 the elements'
secular rates are applied, so the orbits drift and precess as the real
ones do. Against published event times:

| Event | Error, 0.8 (rates) | Error, 0.7 (fixed J2000 elements) |
|---|---|---|
| Mars opposition, Jan 2025 | 5 min | 70 min |
| Jupiter opposition, Dec 2024 | 69 min | 12 min |
| Saturn opposition, Sep 2025 | 153 min | 31 min |
| 2026 equinox (precession-corrected) | 15 min | 33 min |

The rates help most where it matters most: over a century they cut the
inner planets' error 2–63× (see the Horizons section below). Near 2025,
though, Jupiter and Saturn got worse. Table 1 is a least-squares fit over
1800–2050, and for those two it trades a little accuracy at 2025 for
much better accuracy across the whole span, because it can't follow
their mutual great-inequality perturbation. Every event stays well
inside the ±2-day tolerance set before the change.

- **The 2026 March equinox** crossing lands 0.35 days after the published
  time (EPH-1). This is expected, not an error. The model's frame is the
  *fixed* J2000 equinox, while published equinoxes use the equinox *of
  date*, which precesses at about 50.29″/yr. That is 0.366° over 26.2
  years, which Earth covers in 0.37 days. EPH-1b applies that correction.
- **Perihelion 2026** is within 0.05 days of the published time. The
  tolerance is looser (±2 days) for a physical reason: Standish's Earth
  elements describe the Earth–Moon *barycentre*, while published
  perihelion times refer to Earth's centre, and the Moon can shift Earth's
  perihelion by a day or more.

Tolerances were chosen from these physical arguments *before* looking at
the results, not tuned to make checks pass.

## JPL Horizons comparison, 1950–2050

`tests/data/horizons_planets_1950_2050.csv` holds heliocentric
ecliptic-J2000 positions from JPL Horizons (DE441) for the eight planets on
1 January of every second year, 1950–2050 (regenerate with
`tools/fetch_horizons.py`). EPH-H compares ExoSim's positions with them.
"v0.7" is the same comparison with the elements held fixed at J2000, as
0.7 did. Output of `ExoSimTests --horizons-table`:

| Planet | Max error, v0.8 (km) | RMS, v0.8 (km) | Max angle, v0.8 (arcsec) | Max error, v0.7 (km) | Max angle, v0.7 (arcsec) | Improvement |
|---|---|---|---|---|---|---|
| Mercury | 4274 | 2170 | 19.1 | 270826 | 1087 | 63x |
| Venus | 9419 | 6182 | 17.9 | 55931 | 107 | 6x |
| Earth | 15814 | 7254 | 21.8 | 64430 | 90 | 4x |
| Mars | 77639 | 35697 | 77.3 | 180562 | 180 | 2x |
| Jupiter | 1847362 | 815046 | 508.9 | 3100551 | 854 | 2x |
| Saturn | 3957912 | 2654218 | 553.3 | 7350884 | 1005 | 2x |
| Uranus | 1662817 | 993560 | 110.4 | 1642492 | 94 | 1x |
| Neptune | 1597869 | 933013 | 58.0 | 2121188 | 96 | 1x |

How to read this:

- **Applying Standish's secular rates** (0.8) is what made the
  difference: the inner planets improved 2–63×. Mercury's perihelion
  alone precesses ~5.7″/yr, which fixed elements can't follow.
- **Jupiter and Saturn stay at ~500″.** That is the limit of the
  two-body model, not a bug. Their mutual perturbation (the "great
  inequality", period ~900 yr) moves each by up to ~0.3° in longitude,
  and Standish's Table 1 doesn't include the extra terms that correct
  it. Standish quotes similar errors for this table.
- **Uranus and Neptune** were already good with fixed elements; the
  rates change them by less than 1.1×.
- For mission planning, 500″ at Jupiter (~1.8 million km) is about 4%
  of its sphere of influence: fine for porkchop plots and flyby dates,
  not for targeting a flyby altitude.

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
- **All 15 GLSL shaders** pass the Khronos reference validator
  (`glslangValidator`). This caught a function named `noise3` that
  collided with a GLSL built-in and would have failed at runtime on every
  GPU.

- **Refactor regression check:** splitting `main.cpp` (0.8) was checked
  by rendering 12 reference views (system, log scale, exoplanet system,
  planet and moon detail, cruise, three landings, Titan, Jupiter mission)
  with the deterministic `--screenshot` mode before and after. All 12
  were pixel-identical.
- **Static analysis:** clang-format and clang-tidy (bugprone, performance,
  clang-analyzer and others; warnings as errors) run in CI. The first run
  found four floating-point loop counters (converted to integers), an
  incorrect rounding (`(int)(x + 0.5)` → `lround`) and an empty `catch`.
- **Packaging:** the configure → build → install → CPack → extract →
  `--selftest` chain was run end-to-end with real CMake on Linux. Because
  Ubuntu provides no CMake packages for ImGui and glad, stand-in package
  configs were used for those two only. The workflow's pass/fail logic
  (release-notes extraction, tag/version check, DLL detector) was tested
  against realistic inputs.

## What is NOT validated

Stated plainly, so nothing here is over-read:

- **Ephemeris outside 1950–2050 is not characterized.** Standish's Table 1
  is valid for 1800–2050; EPH-H covers 1950–2050. Earlier eras, and the
  outer planets' great-inequality terms, are not modeled.
- **Rendering is not verified on a GPU in this environment.** It compiles,
  links and passes shader validation; visual correctness is checked by eye
  on the Windows build.
- **Live data-source responses** (VizieR Hipparcos/WDS columns in
  particular) are parsed defensively but were not exercised from this
  environment. On a column mismatch, the fetch prints the actual header.
- **Landing fidelity.** FSW checks compare outcomes (touchdown speed,
  duration, parachute altitude) with real missions at plausibility
  tolerances. The 2-D model has no winds, terrain, planet rotation or
  guided lifting entry, and the vehicles are class-representative. It is
  a teaching-grade EDL simulation, not a mission-design tool.
- **Mission planner accuracy vs. real missions** is plausibility-level.
  Lambert windows land within days of real launches (Perseverance:
  2020-08-01 vs. 2020-07-30), and the flyby optimizer rediscovers Voyager
  and New Horizons launch dates. But the planets are two-body, burns are
  impulsive, and flybys are patched conics, so C3 values match real
  missions only to ~15–30%.
- **The Milky Way model** (disk, bar, bulge, arms) is illustrative. GAL
  checks that it has the properties it claims (arm crossings, trailing
  winding, exponential disk), not that it matches the real Galaxy's star
  counts. Host *positions* are real, to within the Archive's distance
  errors (microlensing hosts can be uncertain by ~1 kpc).
- **The MSVC/Windows test run** and the Windows CI job have not run yet.
  Their first run happens on the first push to GitHub.
