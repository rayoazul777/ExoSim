#include "Dataset.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
    // Pulls a numeric field out of a JSON row, returning defaultVal
    // if the field is missing or null (NASA archive data has a LOT
    // of nulls - not every planet has every parameter measured).
    double numOr(const json& row, const char* key, double defaultVal) {
        if (row.contains(key) && !row[key].is_null()) {
            return row[key].get<double>();
        }
        return defaultVal;
    }

    std::string strOr(const json& row, const char* key, const std::string& defaultVal) {
        if (row.contains(key) && !row[key].is_null()) {
            return row[key].get<std::string>();
        }
        return defaultVal;
    }
}

bool Dataset::LoadFromFile(const std::string& path, std::vector<PlanetRecord>& outPlanets) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Dataset] Could not open " << path
                  << " - run data/fetch_exoplanets.py first, or fall back to DemoSolarSystem().\n";
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[Dataset] JSON parse error: " << e.what() << "\n";
        return false;
    }

    std::vector<PlanetRecord> planets;
    planets.reserve(root.size());

    for (const auto& row : root) {
        double semiMajorAxis = numOr(row, "pl_orbsmax", -1.0);
        double period = numOr(row, "pl_orbper", -1.0);

        // Both are required to propagate a position - skip if missing.
        if (semiMajorAxis <= 0.0 || period <= 0.0) continue;

        PlanetRecord p;
        p.name = row.value("pl_name", "Unnamed");
        p.hostStar = row.value("hostname", "Unknown");
        p.elements.semiMajorAxisAU = semiMajorAxis;
        p.elements.periodDays = period;
        p.elements.eccentricity = numOr(row, "pl_orbeccen", 0.0);
        p.elements.inclinationDeg = numOr(row, "pl_orbincl", 0.0);
        p.elements.inclinationMeasured = row.contains("pl_orbincl") && !row["pl_orbincl"].is_null();
        p.elements.argPeriapsisDeg = numOr(row, "pl_orblper", 0.0);
        p.massJupiter = numOr(row, "pl_bmassj", 0.0);
        p.radiusJupiter = numOr(row, "pl_radj", 0.0);
        p.densityGcm3 = numOr(row, "pl_dens", 0.0);
        p.eqTempK = numOr(row, "pl_eqt", 0.0);
        p.insolationEarth = numOr(row, "pl_insol", 0.0);

        p.starTeffK = numOr(row, "st_teff", 0.0);
        p.starRadiusSolar = numOr(row, "st_rad", 0.0);
        p.starMassSolar = numOr(row, "st_mass", 0.0);
        p.starSpectralType = strOr(row, "st_spectype", "");

        planets.push_back(std::move(p));
    }

    outPlanets = std::move(planets);
    std::cout << "[Dataset] Loaded " << outPlanets.size() << " planets with usable orbital elements.\n";
    return true;
}

std::vector<PlanetRecord> Dataset::DemoSolarSystem() {
    // Rough real values, just so the app has something to render
    // out of the box. Inclinations/eccentricities simplified.
    // Densities are real (g/cm^3) - a nice demonstration that the
    // density-based classifier correctly separates rocky planets
    // (Mercury-Mars, ~3.9-5.5) from gas/ice giants (Jupiter-Neptune, ~0.7-1.6).
    struct Seed { const char* name; double a, e, i, per, density; };
    static const Seed seeds[] = {
        {"Mercury",  0.387, 0.206, 7.00,   88.0,  5.43},
        {"Venus",    0.723, 0.007, 3.39,  224.7,  5.24},
        {"Earth",    1.000, 0.017, 0.00,  365.25, 5.51},
        {"Mars",     1.524, 0.093, 1.85,  687.0,  3.93},
        {"Jupiter",  5.203, 0.049, 1.30, 4331.0,  1.33},
        {"Saturn",   9.537, 0.056, 2.49,10747.0,  0.69},
        {"Uranus",  19.191, 0.046, 0.77,30589.0,  1.27},
        {"Neptune", 30.07,  0.010, 1.77,59800.0,  1.64},
    };

    std::vector<PlanetRecord> out;
    for (const auto& s : seeds) {
        PlanetRecord p;
        p.name = s.name;
        p.hostStar = "Sun";
        p.elements.semiMajorAxisAU = s.a;
        p.elements.eccentricity = s.e;
        p.elements.inclinationDeg = s.i;
        p.elements.inclinationMeasured = true; // hand-coded real values, not archive fallbacks
        p.elements.periodDays = s.per;
        p.densityGcm3 = s.density;
        p.starTeffK = 5778.0;
        p.starRadiusSolar = 1.0;
        p.starMassSolar = 1.0;
        p.starSpectralType = "G2 V";
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<StarSystem> Dataset::GroupByHost(const std::vector<PlanetRecord>& planets) {
    std::vector<StarSystem> systems;

    // Preserve first-seen order while grouping, using a simple linear
    // lookup into `systems` by name (fine at this data scale - a few
    // thousand rows, not tens of millions).
    for (size_t i = 0; i < planets.size(); ++i) {
        const std::string& host = planets[i].hostStar;
        auto it = std::find_if(systems.begin(), systems.end(),
            [&host](const StarSystem& s) { return s.hostName == host; });
        if (it == systems.end()) {
            StarSystem sys;
            sys.hostName = host;
            sys.planetIndices = {i};
            sys.starTeffK = planets[i].starTeffK;
            sys.starRadiusSolar = planets[i].starRadiusSolar;
            sys.starMassSolar = planets[i].starMassSolar;
            sys.starSpectralType = planets[i].starSpectralType;
            systems.push_back(std::move(sys));
        } else {
            it->planetIndices.push_back(i);
        }
    }

    // Multi-planet systems are more interesting to land on by default,
    // so sort those first.
    std::sort(systems.begin(), systems.end(),
        [](const StarSystem& a, const StarSystem& b) {
            return a.planetIndices.size() > b.planetIndices.size();
        });

    return systems;
}
