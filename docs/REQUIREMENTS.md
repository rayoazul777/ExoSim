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
| ORB-3 | Place Sol's planets from J2000 elements so published events for 2024–2026 are reproduced within 2 days | T: EPH-1…6 | Met (oppositions within 12–70 min) |
| ORB-4 | Converge Kepler's equation for every mean anomaly, including near-parabolic orbits | T: KEP (e = 0.99); I: bisection fallback | Met |

## Mission planning

| ID | Requirement | Verif. | Status |
|---|---|---|---|
| MIS-R1 | Compute Hohmann transfer time and Δv from 200 km LEO, matching closed-form values | T: MIS-1, MIS-2 | Met |
| MIS-R2 | Find the next launch window after a given date such that the trajectory reaches the target | T: MIS-4 (miss < 1e-4 AU) | Met |
| MIS-R3 | Report windows as calendar dates | T: TIM-1…4 | Met |
| MIS-R4 | State the model's idealizations (no gravity assists, ecliptic-plane transfer, no capture burn) in the UI | I, D | Met |
| MIS-R5 | Support gravity-assist trajectories | — | **Not met** (roadmap) |

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
| ENG-3 | All shaders compile under the GLSL reference compiler | T: `glslangValidator` | Met (13/13) |
| ENG-4 | Deterministic, replayable simulation | — | **Not met**: the clock advances by wall-clock time |
| ENG-5 | Continuous integration on Windows and Linux | I: workflow file | Implemented. Linted (actionlint) and its pass/fail logic tested locally; **not yet run on GitHub** |
| ENG-8 | A downloadable build runs on a clean Windows machine with no installs | T: CI dependency check + packaged `--selftest` | Implemented; verified end-to-end on Linux, Windows CI pending first run |
| ENG-6 | Runtime configuration from a file rather than constants | — | **Not met** (roadmap) |
| ENG-7 | Structured logging | — | **Not met**: plain console output today |

## Out of scope

ExoSim is **not flight software** and is not intended for operational
mission design. Moving it toward that would require:
- formal requirements with review and configuration control;
- independent verification and validation against authoritative
  ephemerides (e.g. JPL DE series / Horizons) with quantified error budgets;
- fault management;
- deterministic execution;
- full traceability from requirements to tests.
