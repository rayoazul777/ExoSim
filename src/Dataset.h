#pragma once
#include <string>
#include <vector>
#include "KeplerOrbit.h"

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
};

