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
};

// Loads data/exoplanets.json (produced by data/fetch_exoplanets.py)
// into a flat list of PlanetRecord. Skips rows missing the orbital
// elements needed to propagate a position (semi-major axis + period).
class Dataset {
public:
    // Returns false (and leaves outPlanets untouched) if the file
    // can't be opened or parsed - caller should fall back to a
    // built-in demo system (see Dataset::DemoSolarSystem()).
    static bool LoadFromFile(const std::string& path, std::vector<PlanetRecord>& outPlanets);

    // A hand-coded fallback (Mercury-Neptune) so the app still runs
    // and looks reasonable before you've run the fetch script.
    static std::vector<PlanetRecord> DemoSolarSystem();
};
