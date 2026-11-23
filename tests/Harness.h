#pragma once
// Shared check() harness for ExoSimTests. Every check records what was
// tested, the expected value and its source, the computed value, and the
// tolerance; main() prints them (or a Markdown table with --markdown).
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include "Dataset.h"

struct Result {
    std::string id, what, source;
    double expected, actual, tolerance;
    std::string unit;
    bool pass;
};

inline std::vector<Result> g_results;

inline void check(const std::string& id, const std::string& what, double expected, double actual, double tolerance,
                  const std::string& unit, const std::string& source) {
    bool pass = std::isfinite(actual) && std::fabs(actual - expected) <= tolerance;
    g_results.push_back({id, what, source, expected, actual, tolerance, unit, pass});
}

inline const PlanetRecord& solPlanet(const std::vector<PlanetRecord>& sol, const std::string& name) {
    for (const auto& p : sol)
        if (p.name == name) return p;
    throw std::runtime_error("missing planet " + name);
}

// Test groups defined in other translation units.
void testEngineering(); // test_engineering.cpp: replay, formatting, sky, config, logging
void testHorizons(); // test_horizons.cpp: ephemeris vs JPL Horizons DE441
void testLambert(); // test_lambert.cpp: Lambert solver, porkchop, gravity assists
void testGalaxy(); // test_galaxy.cpp: Milky Way map coordinates, hosts, model
