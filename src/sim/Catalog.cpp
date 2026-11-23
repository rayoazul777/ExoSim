#include "sim/Catalog.h"
#include "Log.h"

#include <algorithm>

namespace {
void buildOrbitsAndSystems(Catalog& c) {
    c.orbits.clear();
    c.orbits.reserve(c.planets.size());
    for (auto& p : c.planets) c.orbits.emplace_back(p.elements);

    c.systems = Dataset::GroupByHost(c.planets);
    for (auto& sys : c.systems)
        if (sys.hostName == "Sun") sys.isHome = true;
    // Home system leads the list, ahead of GroupByHost's most-planets-first order.
    std::stable_sort(c.systems.begin(), c.systems.end(),
                     [](const StarSystem& a, const StarSystem& b) { return a.isHome && !b.isHome; });
}
} // namespace

Catalog Catalog::Load(const std::string& dataDir) {
    Catalog c;
    // Sol is always included alongside the Archive data, as a real-data
    // reference point (see StarSystem::isHome).
    c.realArchiveData = Dataset::LoadFromFile(dataDir + "/exoplanets.json", c.planets) && !c.planets.empty();
    if (!c.realArchiveData) {
        Log::Info("catalog", "No archive data found - using the built-in solar system only.");
        c.planets = Dataset::DemoSolarSystem();
    } else {
        std::vector<PlanetRecord> sol = Dataset::DemoSolarSystem();
        c.planets.insert(c.planets.end(), sol.begin(), sol.end());
    }
    // NASA/JPL SBDB dwarf planets join the "Sun" system via GroupByHost.
    Dataset::LoadDwarfPlanets(dataDir + "/dwarf_planets.json", c.planets);

    buildOrbitsAndSystems(c);
    Dataset::LoadStellarHosts(dataDir + "/stellar_hosts.json", c.systems);
    Log::Info("catalog",
              "Grouped " + std::to_string(c.planets.size()) + " planets into " + std::to_string(c.systems.size()) +
                  " star systems.",
              {{"planets", static_cast<double>(c.planets.size())}, {"systems", static_cast<double>(c.systems.size())}});

    c.stars = Dataset::LoadStarCatalog(dataDir + "/star_catalog.json");
    return c;
}

Catalog Catalog::FromPlanets(std::vector<PlanetRecord> planets) {
    Catalog c;
    c.planets = std::move(planets);
    buildOrbitsAndSystems(c);
    return c;
}

int Catalog::findSystem(const std::string& hostName) const {
    for (int i = 0; i < static_cast<int>(systems.size()); ++i)
        if (systems[i].hostName == hostName) return i;
    return -1;
}
