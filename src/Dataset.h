#pragma once
#include <string>
#include <vector>
#include "KeplerOrbit.h"

// A moon of a planet. Real values are only populated for the built-in
// demo solar system, where orbital data is genuinely well-known. NASA's
// Exoplanet Archive does not include exomoon detections (only a
// small handful of disputed, unconfirmed candidates exist in the
// literature system-wide) - moons for Archive-loaded planets are
// generated speculatively at the UI layer for illustration only, never
// stored here as if they were real, and isReal distinguishes the two.
struct MoonRecord {
    std::string name;
    double orbitRadiusPlanetRadii = 0.0; // distance from planet, in units of the planet's own radius
    double orbitPeriodDays = 0.0;
    double inclinationDeg = 0.0;         // real for demo moons (approximate), illustrative otherwise
    double eccentricity = 0.0;           // real for demo moons (approximate), illustrative otherwise
    double argPeriapsisDeg = 0.0;        // real (approximate) where confidently known, else 0
    double longAscNodeDeg = 0.0;         // real (approximate) where confidently known, else 0
    bool isReal = false;
};

// A companion star in a multi-star system. The Archive's `stellarhosts`
// table gives real per-star temperature/radius/mass where a companion has
// been independently characterized, but has NO bulk-queryable physical or
// angular separation field - that lives in individual discovery papers,
// not a simple column. So `hasData` real properties are trustworthy, but
// any on-screen POSITION for a companion is always schematic, never a
// real separation - the UI must say so wherever a companion is shown.
struct StarComponent {
    double teffK = 0.0;
    double radiusSolar = 0.0;
    double massSolar = 0.0;
    std::string spectralType;
    bool hasData = false; // false = a known-to-exist companion (from sy_snum) with no resolved properties
};

struct PlanetRecord {
    std::string name;
    std::string hostStar;
    OrbitalElements elements;
    double massJupiter = 0.0;   // pl_bmassj, for point-size / info panel
    double radiusJupiter = 0.0; // pl_radj

    // Fields used to sharpen composition/atmosphere estimates - real
    // Archive measurements, not fabricated. Density in particular is far
    // more diagnostic of rocky-vs-gas than radius or mass alone.
    double densityGcm3 = 0.0;     // pl_dens (0 = not measured)
    double eqTempK = 0.0;         // pl_eqt (0 = not measured)
    double insolationEarth = 0.0; // pl_insol, relative to Earth's (0 = not measured)

    // Host star's physical properties, repeated on every planet row by
    // the Archive since it's a per-system property - GroupByHost copies
    // these onto the StarSystem once so they aren't duplicated in the UI.
    double starTeffK = 0.0;         // st_teff, Kelvin (0 = not measured)
    double starRadiusSolar = 0.0;   // st_rad, solar radii (0 = not measured)
    double starMassSolar = 0.0;     // st_mass, solar masses (0 = not measured)
    std::string starSpectralType;   // st_spectype, e.g. "G2 V" (often empty)
    int numStarsInSystem = 1;       // sy_snum (>1 = binary/multiple star system)

    // Real galactic coordinates of this system, used only to orient the
    // background skybox so it's genuinely related to where the system
    // sits in the sky, not arbitrary decoration. 0/0 (their default) means
    // "not measured" for a real exoplanet, or "not physically meaningful"
    // for the Sun itself (a star's galactic coordinates describe its
    // direction as seen from the Sun, which doesn't apply to the Sun).
    double galacticLatDeg = 0.0;    // glat
    double galacticLonDeg = 0.0;    // glon

    // Only populated for the built-in demo solar system - see MoonRecord's
    // comment above for why Archive-loaded planets don't carry real data here.
    std::vector<MoonRecord> moons;
};

// A single real star system: one host star plus the indices (into the
// flat PlanetRecord list) of its known planets. Different systems are
// light-years apart in reality, so we never render two systems in the
// same scene - the app shows one system at a time and swaps on selection.
struct StarSystem {
    std::string hostName;
    std::vector<size_t> planetIndices;

    // Copied from the first planet record with this host - see note above.
    double starTeffK = 0.0;
    double starRadiusSolar = 0.0;
    double starMassSolar = 0.0;
    std::string starSpectralType;
    int numStars = 1;
    double galacticLatDeg = 0.0;
    double galacticLonDeg = 0.0;

    // Populated by LoadStellarHosts, only for systems with numStars > 1.
    // Real per-star data where the Archive has it distinguished; padded
    // with hasData=false placeholders up to numStars-1 when it doesn't -
    // either way, position is always schematic (see StarComponent).
    std::vector<StarComponent> companions;

    bool isHome = false; // true only for the built-in Sol reference system
};

class Dataset {
public:
    // Returns false (and leaves outPlanets untouched) if the file
    // can't be opened or parsed - caller should fall back to a
    // built-in demo system (see Dataset::DemoSolarSystem()).
    static bool LoadFromFile(const std::string& path, std::vector<PlanetRecord>& outPlanets);

    // A hand-coded fallback (Mercury-Neptune) so the app still runs
    // and looks reasonable before you've run the fetch script.
    static std::vector<PlanetRecord> DemoSolarSystem();

    // Groups a flat planet list by hostStar, sorted with the
    // most-populated systems first (multi-planet systems are more
    // interesting to land on by default than lone single-planet ones).
    static std::vector<StarSystem> GroupByHost(const std::vector<PlanetRecord>& planets);

    // Enriches multi-star systems with real companion-star data from
    // data/stellar_hosts.json (produced by fetch_exoplanets.py), when
    // present. Safe to call even if the file doesn't exist - companions
    // are simply left empty (schematic rendering is skipped) in that case.
    static void LoadStellarHosts(const std::string& path, std::vector<StarSystem>& systems);

    // Appends the 5 IAU-recognized dwarf planets (Ceres, Pluto, Eris,
    // Haumea, Makemake) to `planets`, from data/dwarf_planets.json -
    // real orbital/physical data pulled from NASA/JPL's SBDB API, a
    // DIFFERENT official NASA source from the Exoplanet Archive used
    // everywhere else in Dataset. Call this before GroupByHost so they
    // get bucketed into the "Sun" system like the 8 main planets. Safe
    // to call even if the file doesn't exist - it just adds nothing.
    static void LoadDwarfPlanets(const std::string& path, std::vector<PlanetRecord>& planets);
};

