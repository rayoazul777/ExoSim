# Data sources, provenance and licenses

## How to read ExoSim's data

Every value falls into one of four categories. The UI labels them, and
the code keeps them separate.

| Category | Meaning | Examples |
|---|---|---|
| **Measured** | Taken from a published catalog or measurement | Orbital period, semi-major axis, stellar temperature, star positions, WDS separations, ring radii |
| **Derived** | Computed from measured values with standard physics | Hohmann transfer times, Δv, projected separation in AU, Kepler's-3rd-law companion periods, sky seen from another star |
| **Estimated** | Statistical inference, not a measurement | Planet composition class and surface/atmosphere text |
| **Illustrative** | A visualization choice, not data | Procedural surfaces, speculative exomoons, nebula/galactic-band patterns, size exaggeration, exoplanet orbital phases |

## Catalogs and services

| Data | Source | Access | Reference |
|---|---|---|---|
| Exoplanets (`ps` table) and host stars (`stellarhosts` table) | NASA Exoplanet Archive, operated by Caltech/IPAC | TAP: `exoplanetarchive.ipac.caltech.edu/TAP/sync` | https://exoplanetarchive.ipac.caltech.edu (table DOIs on the archive's citation page) |
| Dwarf planets (Ceres, Pluto, Eris, Haumea, Makemake) | NASA/JPL Small-Body Database | `ssd-api.jpl.nasa.gov/sbdb.api` | https://ssd-api.jpl.nasa.gov/doc/sbdb.html |
| Star positions, magnitudes, colors, proper motions, parallaxes | *The Hipparcos and Tycho Catalogues*, ESA SP-1200 (1997); Perryman et al. 1997, A&A 323, L49. VizieR table `I/239/hip_main` | CDS VizieR TAP | https://tapvizier.cds.unistra.fr/adql/ |
| Double-star separations | Washington Double Star Catalog: Mason et al. 2001, AJ 122, 3466. VizieR table `B/wds/wds` | CDS VizieR TAP | as above |
| VizieR service | Ochsenbein, Bauer & Marcout 2000, A&AS 143, 23 | — | https://vizier.cds.unistra.fr |
| Sol planets' orbital elements (J2000) | E. M. Standish, *Keplerian Elements for Approximate Positions of the Major Planets*, JPL Solar System Dynamics | Hand-entered in `Dataset::DemoSolarSystem` | https://ssd.jpl.nasa.gov/planets/approx_pos.html |
| Planet densities, radii, masses; ring radii | NASA NSSDCA planetary and ring fact sheets | Hand-entered | https://nssdc.gsfc.nasa.gov/planetary/factsheet/ |
| Moon periods, inclinations, eccentricities, radii | Standard published values (JPL SSD satellite tables / NSSDCA fact sheets) | Hand-entered, rounded | https://ssd.jpl.nasa.gov/sats/ |

**Hand-entered values** live in `Dataset.cpp` and
`SolarSystemAppearance.cpp` rather than being fetched. They are
well-established figures, but they carry transcription risk, and the
EPH/CLS tests only cover part of them.

## Curated and reference values

- **Published atmosphere detections** (`PlanetClassifier.cpp`): short
  summaries of JWST/Hubble results for about ten well-studied planets
  (e.g. WASP-39 b, K2-18 b, TRAPPIST-1 b/c). **Per-entry paper citations
  have not been added yet.** Until they are, treat these as pointers to
  the literature, not authoritative summaries. Some results (e.g. the
  K2-18 b dimethyl-sulfide signal) are explicitly labeled as disputed.
- **Launch vehicle figures** (Mission Planner HUD): Falcon Heavy from
  SpaceX's published specifications; SLS Block 1 from NASA's published
  SLS reference material. Vendors revise these figures, so they are
  quoted, not modeled.
- **Planet size classes**: the radius gap follows Fulton et al. 2017,
  AJ 154, 109. The Earth/super-Earth/Neptune/Jupiter-size boundaries
  (1.25 / 2 / 6 / 15 R⊕) are the Kepler mission's size classes
  (e.g. Batalha et al. 2013, ApJS 204, 24).
- **Published event times used in validation** (2026 equinox and
  perihelion; the 2024–2025 oppositions of Mars, Jupiter and Saturn):
  standard almanac values. Cross-checking them against JPL Horizons is
  listed in `VALIDATION.md` as future work.

## Equations and constants

| Item | Value / method | Reference |
|---|---|---|
| Kepler's equation, vis-viva, Hohmann transfer, patched conics | Standard two-body formulas | Curtis, *Orbital Mechanics for Engineering Students*; Vallado, *Fundamentals of Astrodynamics and Applications*; Bate, Mueller & White, *Fundamentals of Astrodynamics* |
| Sun's gravitational parameter μ☉ | 1.32712440018 × 10¹¹ km³/s² | IAU / JPL |
| Earth's gravitational parameter μ⊕ | 398,600.4418 km³/s² | WGS 84 |
| Astronomical unit | 1.495978707 × 10⁸ km | IAU 2012 (exact) |
| J2000 obliquity of the ecliptic | 23.4392911° | IAU |
| General precession (used only in test EPH-1b) | 50.29″/yr | IAU |
| Calendar conversion | civil-from-days algorithm | Howard Hinnant, public domain: https://howardhinnant.github.io/date_algorithms.html |

## Acknowledgments requested by data providers

If ExoSim or its output is shared publicly, include:

> This research has made use of the NASA Exoplanet Archive, which is
> operated by the California Institute of Technology, under contract with
> the National Aeronautics and Space Administration under the Exoplanet
> Exploration Program.

> This research has made use of the VizieR catalogue access tool, CDS,
> Strasbourg, France (DOI: 10.26093/cds/vizier).

Please also cite the Hipparcos catalogue (ESA 1997) and the Washington
Double Star Catalog (Mason et al. 2001) as listed above. Check each
provider's current terms before any commercial use.

## Software dependencies

| Dependency | Use | License |
|---|---|---|
| GLFW | Window and input | zlib/libpng |
| GLM | Vector/matrix math | MIT |
| Dear ImGui | UI | MIT |
| nlohmann/json | JSON parsing | MIT |
| glad | OpenGL function loader | Generator is MIT; check upstream for the generated code's terms |
| libcurl | HTTP in `FetchData` | curl license (MIT-style) |

All dependencies are installed through vcpkg (`vcpkg.json`).

## Project license

**ExoSim does not have a license yet.** Without one, others have no
legal right to reuse the code, even if the repository is public. Choose
one (for example MIT or Apache-2.0) and add a `LICENSE` file before
publishing.
