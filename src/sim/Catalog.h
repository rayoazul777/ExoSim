#pragma once
#include <string>
#include <vector>
#include "Dataset.h"
#include "KeplerOrbit.h"

// Everything loaded from data/ at startup: planets (Archive + Sol + dwarf
// planets), one KeplerOrbit per planet, star systems (Sol first), and the
// Hipparcos star catalog. Immutable after loading.
struct Catalog {
    std::vector<PlanetRecord> planets;
    std::vector<KeplerOrbit> orbits; // orbits[i] propagates planets[i]
    std::vector<StarSystem> systems; // systems[0] is Sol when present
    std::vector<StarCatalogEntry> stars; // Hipparcos naked-eye stars
    bool realArchiveData = false;

    // Loads from <dataDir>/exoplanets.json etc. Missing files degrade
    // gracefully (Sol only, no starfield, ...), as before.
    static Catalog Load(const std::string& dataDir = "data");

    // From an explicit planet list (tests, headless tools): orbits, grouping,
    // Sol first. No stellar-host or star-catalog files are read.
    static Catalog FromPlanets(std::vector<PlanetRecord> planets);

    // Index of the system whose host is `hostName`, or -1.
    int findSystem(const std::string& hostName) const;
};
