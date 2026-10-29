#include "BodyConstants.h"
#include <algorithm>
#include <cmath>

namespace {
    // Venus and Titan depart strongly from a single exponential (the scale
    // height shrinks with altitude), so they use tabulated profiles instead:
    // {altitude km, density kg/m^3}, log-linearly interpolated. Approximate
    // values computed from published pressure/temperature profiles (VIRA
    // reference atmosphere for Venus; Huygens HASI descent data for Titan)
    // via the ideal-gas law, rounded - representative, not authoritative.
    const double kVenusProfile[][2] = {
        {0, 65.0}, {10, 35.0}, {20, 18.0}, {30, 7.9}, {40, 3.9}, {50, 1.5}, {60, 0.47},
        {70, 0.08}, {80, 0.012}, {90, 1.1e-3}, {100, 9e-5}, {120, 2e-6}, {140, 3e-8}, {200, 1e-11}};
    const double kTitanProfile[][2] = {
        {0, 5.3}, {10, 3.8}, {20, 2.6}, {30, 1.75}, {40, 1.04}, {50, 0.56}, {60, 0.28}, {80, 0.061},
        {100, 0.016}, {150, 2.1e-3}, {200, 5e-4}, {300, 3.8e-5}, {500, 3.5e-7}, {800, 2e-9}, {1300, 1e-12}};

    template <size_t N>
    double tableDensity(const double (&tab)[N][2], double altKm) {
        if (altKm <= tab[0][0]) return tab[0][1];
        for (size_t i = 1; i < N; ++i) {
            if (altKm <= tab[i][0]) {
                double u = (altKm - tab[i - 1][0]) / (tab[i][0] - tab[i - 1][0]);
                return std::exp(std::log(tab[i - 1][1]) + u * (std::log(tab[i][1]) - std::log(tab[i - 1][1])));
            }
        }
        return 0.0; // above the table: vacuum
    }
}

double BodyConstants::densityAt(double altitudeM) const {
    if (!hasAtmosphere()) return 0.0;
    double altKm = std::max(altitudeM, 0.0) / 1000.0;
    if (std::string(name) == "Venus") return tableDensity(kVenusProfile, altKm);
    if (std::string(name) == "Titan") return tableDensity(kTitanProfile, altKm);
    return surfaceDensityKgM3 * std::exp(-altKm / scaleHeightKm);
}

namespace {
    // name, GM [km^3/s^2], mean radius [km], solid surface, rho0 [kg/m^3], H [km]
    const BodyConstants kBodies[] = {
        {"Mercury",  22031.86,      2439.4, true,  0.0,   0.0},
        {"Venus",    324858.59,     6051.8, true,  65.0,  15.9},
        {"Earth",    398600.4418,   6371.0, true,  1.217, 8.5},
        {"Moon",     4902.80,       1737.4, true,  0.0,   0.0},
        {"Mars",     42828.37,      3389.5, true,  0.020, 11.1},
        {"Phobos",   0.0007087,     11.08,  true,  0.0,   0.0},
        {"Deimos",   0.0000962,     6.2,    true,  0.0,   0.0},
        {"Jupiter",  126686531.9,   69911.0, false, 0.0,  0.0},
        {"Io",       5959.92,       1821.6, true,  0.0,   0.0},
        {"Europa",   3202.74,       1560.8, true,  0.0,   0.0},
        {"Ganymede", 9887.83,       2631.2, true,  0.0,   0.0},
        {"Callisto", 7179.29,       2410.3, true,  0.0,   0.0},
        {"Saturn",   37931206.2,    58232.0, false, 0.0,  0.0},
        {"Titan",    8978.14,       2574.7, true,  5.43,  40.0},
        {"Rhea",     153.94,        763.8,  true,  0.0,   0.0},
        {"Uranus",   5793951.3,     25362.0, false, 0.0,  0.0},
        {"Titania",  228.2,         788.9,  true,  0.0,   0.0},
        {"Oberon",   192.4,         761.4,  true,  0.0,   0.0},
        {"Neptune",  6835099.5,     24622.0, false, 0.0,  0.0},
        {"Triton",   1428.5,        1353.4, true,  0.0,   0.0}, // N2 atmosphere ~1.4 Pa: negligible for descent
        {"Ceres",    62.63,         469.7,  true,  0.0,   0.0},
        {"Pluto",    869.6,         1188.3, true,  0.0,   0.0}, // ~1 Pa N2 atmosphere: negligible for descent
        {"Eris",     1108.0,        1163.0, true,  0.0,   0.0},
    };
}

const BodyConstants* Bodies::Lookup(const std::string& name) {
    for (const auto& b : kBodies) if (name == b.name) return &b;
    return nullptr;
}
