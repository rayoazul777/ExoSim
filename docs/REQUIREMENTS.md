# Requirements (lightweight)

A deliberately small specification, written so each requirement can be
traced to how it is verified. Verification methods:
- **T** (Test): an automated check in `tests/test_physics.cpp`; IDs refer to `VALIDATION.md`.
- **I** (Inspection): code review.
- **D** (Demonstration): manual run of the Windows build.

Status is honest. Requirements that are not yet met are listed and
marked, not omitted.

## Orbital mechanics

| ID | Requirement | Verif. | Status |
|---|---|---|---|
| ORB-1 | Propagate two-body orbits for 0 ≤ e < 1, conserving angular momentum and vis-viva energy to a relative error below 1e-5 | T: KEP-1…5 | Met (worst case ~1e-7 at e = 0.99) |
| ORB-2 | Handle inclined orbits with correct 3-D orientation | T: KEP-6 | Met |
| ORB-3 | Place Sol's planets from J2000 elements so published events for 2024–2026 are reproduced within 2 days | T: EPH-1…6 | Met (oppositions within 5–153 min) |
| ORB-4 | Converge Kepler's equation for every mean anomaly, including near-parabolic orbits | T: KEP (e = 0.99); I: bisection fallback | Met |

## Mission planning

| ID | Requirement | Verif. | Status |
|---|---|---|---|
| MIS-R1 | Compute Hohmann transfer time and Δv from 200 km LEO, matching closed-form values | T: MIS-1, MIS-2 | Met |
| MIS-R2 | Find the next launch window after a given date such that the trajectory reaches the target | T: MIS-4 (miss < 1e-4 AU) | Met |
| MIS-R3 | Report windows as calendar dates | T: TIM-1…4 | Met |
| MIS-R4 | State the model's idealizations (patched conics, impulsive burns, two-body planets) in the UI | I, D | Met |
| MIS-R5 | Support gravity-assist trajectories | T: GA-1…15 | Met (patched conics, powered flybys; Voyager- and New Horizons-era chains reproduced) |
| MIS-R6 | Plan transfers between arbitrary dates (Lambert), 3-D, and show launch/arrival cost maps | T: LAM-1…19 | Met |
| MIS-R7 | Quantify ephemeris error against an authoritative ephemeris over 1950–2050 | T: EPH-H0…17 (JPL Horizons DE441) | Met; envelopes in `VALIDATION.md` |

## Mission operations and landing

| ID | Requirement | Verif. | Status |
|---|---|---|---|
| FSW-R1 | Compute arrival orbit-insertion Δv from the approach v∞ | T: FSW-15, FSW-16 | Met (not for moons of other planets: tour not modeled) |
| FSW-R2 | Simulate entry, descent and landing at fixed physics (100 Hz) and guidance (20 Hz) rates, independent of frame rate | T: FSW-1, FSW-6, FSW-14 | Met |
| FSW-R3 | Land within 10 m of the guidance target at ≤ 1 m/s for powered landers | T: FSW-3, FSW-5, FSW-8, FSW-9 | Met (Mars 0.05 m, Moon 1.2 m) |
| FSW-R4 | Reproduce published descent outcomes within stated tolerances | T: FSW-3, FSW-4, FSW-7, FSW-10, FSW-11, FSW-12 | Met (plausibility level; vehicle parameters are representative) |
| FSW-R5 | Never simulate a landing on a body with no solid surface | T: FSW-13 | Met |
| FSW-R6 | Order mission events in time, and report the correct phase at any time | T: FSW-21…23 | Met |
| FSW-R7 | Show signal delay (one-way light time) to Earth | T: FSW-17 | Met |
| FSW-R8 | Export telemetry in an open format | I: CSV export | Met (cruise state vectors and landing telemetry) |
| FSW-R9 | Label simplifications in the UI | I, D | Met |

## Data handling

| ID | Requirement | Verif. | Status |
|---|---|---|---|
| DAT-R1 | Malformed, missing or wrongly-typed input must not crash the application | T: DAT-1, DAT-3, DAT-4 | Met (previously failed; fixed) |
| DAT-R2 | Skip records lacking the fields needed to propagate an orbit | T: DAT-5 | Met |
| DAT-R3 | Label every displayed value as measured, derived, estimated or illustrative | I, D | Met; taxonomy in `DATA_SOURCES.md` |
| DAT-R4 | Never present a speculative moon or an estimated composition as measured | I, D | Met |
| DAT-R5 | Record the provenance of every dataset | I: `DATA_SOURCES.md` | Met (paper citations for the atmosphere table still to add) |

## Robustness and engineering

| ID | Requirement | Verif. | Status |
|---|---|---|---|
| ENG-1 | Core modules build and test without a GPU | T: `ExoSimTests` | Met |
| ENG-2 | No memory errors or undefined behavior in the core | T: suite under ASan + UBSan | Met (GCC) |
| ENG-3 | All shaders compile under the GLSL reference compiler | T: `glslangValidator` | Met (15/15) |
| ENG-4 | Deterministic, replayable simulation | T: FSW-14, FSW-19, FSW-20, ENG-1…8 | Met: fixed-tick clock; bit-for-bit landing; whole sessions recorded (`--record`) and replayed (`--replay`), verified by state hash (`--verify-replay`) |
| ENG-5 | Continuous integration on Windows and Linux | I: workflow file | Implemented. Linted (actionlint) and its pass/fail logic tested locally; **not yet run on GitHub** |
| ENG-8 | A downloadable build runs on a clean Windows machine with no installs | T: CI dependency check + packaged `--selftest` | Implemented; verified end-to-end on Linux, Windows CI pending first run |
| ENG-6 | Runtime configuration from a file rather than constants | T: ENG-16…18 | Met: `exosim.json` (unknown keys and bad values warn and fall back) |
| ENG-7 | Structured logging | T: ENG-19 | Met: levels, components, typed fields; console + JSON-lines file |
| ENG-9 | Static analysis and formatting enforced in CI | I: workflow `static-analysis` job | Met: clang-format-18 check and clang-tidy-18 (warnings as errors) over core and tests |
| ENG-10 | Separate simulation state from UI so it can be tested | T: ENG group; I: `sim/` has no GL/ImGui includes | Met |

## Milky Way map

| ID | Requirement | Verif. | Status |
|---|---|---|---|
| GAL-R1 | Plot every exoplanet host with a measured distance at its real 3-D position | T: GAL-4, GAL-8…10 | Met (to within the Archive's distance errors) |
| GAL-R2 | Convert equatorial to Galactic coordinates per the IAU J2000 definition | T: GAL-1…4 | Met (matches the Archive's glon/glat to ~1e-5°) |
| GAL-R3 | Label the disk/arm/bar model as illustrative and keep it from standing in for real stars at close range | I, D; T: GAL-12…19 | Met (model fades out below ~2 kpc) |

## Out of scope

ExoSim is **not flight software** and is not intended for operational
mission design. Moving it toward that would require:
- formal requirements with review and configuration control;
- independent verification and validation against authoritative
  ephemerides (e.g. JPL DE series / Horizons) with quantified error budgets;
- fault management;
- deterministic execution;
- full traceability from requirements to tests.
