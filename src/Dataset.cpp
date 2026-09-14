#include "Dataset.h"
#include <fstream>
#include <iostream>
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
        p.elements.argPeriapsisDeg = numOr(row, "pl_orblper", 0.0);
        p.massJupiter = numOr(row, "pl_bmassj", 0.0);
        p.radiusJupiter = numOr(row, "pl_radj", 0.0);

        planets.push_back(std::move(p));
    }

    outPlanets = std::move(planets);
    std::cout << "[Dataset] Loaded " << outPlanets.size() << " planets with usable orbital elements.\n";
    return true;
}

std::vector<PlanetRecord> Dataset::DemoSolarSystem() {
    // Rough real values, just so the app has something to render
    // out of the box. Inclinations/eccentricities simplified.
    struct Seed { const char* name; double a, e, i, per; };
    static const Seed seeds[] = {
        {"Mercury",  0.387, 0.206, 7.00,   88.0},
        {"Venus",    0.723, 0.007, 3.39,  224.7},
        {"Earth",    1.000, 0.017, 0.00,  365.25},
        {"Mars",     1.524, 0.093, 1.85,  687.0},
        {"Jupiter",  5.203, 0.049, 1.30, 4331.0},
        {"Saturn",   9.537, 0.056, 2.49,10747.0},
        {"Uranus",  19.191, 0.046, 0.77,30589.0},
        {"Neptune", 30.07,  0.010, 1.77,59800.0},
    };

    std::vector<PlanetRecord> out;
    for (const auto& s : seeds) {
        PlanetRecord p;
        p.name = s.name;
        p.hostStar = "Sun";
        p.elements.semiMajorAxisAU = s.a;
        p.elements.eccentricity = s.e;
        p.elements.inclinationDeg = s.i;
        p.elements.periodDays = s.per;
        out.push_back(std::move(p));
    }
    return out;
}
