// EPH-H group: Sol ephemeris vs JPL Horizons (DE441), 1950-2050.
//
// tests/data/horizons_planets_1950_2050.csv holds heliocentric geometric
// positions (ecliptic and mean equinox of J2000, km) every 730 days from
// Horizons (regenerate with tools/fetch_horizons.py). For each planet this
// measures the position error of
//   (a) the current model: Standish Table 1 elements WITH secular rates, and
//   (b) the v0.7.0 model: rounded J2000 elements, sidereal period, no rates,
// and reports the error envelope as distance and as the angle seen from the
// Sun. Requirement ORB-3b: the current model stays within Standish's
// published accuracy class for 1800-2050.
#include "Harness.h"
#include "KeplerOrbit.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#ifndef EXOSIM_TEST_DATA_DIR
#define EXOSIM_TEST_DATA_DIR "tests/data"
#endif

namespace {
constexpr double kAuKm = 1.495978707e8;
constexpr double kJ2000 = 2451545.0;
constexpr double kRadToArcsec = 206264.80624709636;

struct Sample {
    double jd;
    glm::dvec3 km;
};

std::map<std::string, std::vector<Sample>> loadHorizons() {
    std::map<std::string, std::vector<Sample>> out;
    std::ifstream in(std::string(EXOSIM_TEST_DATA_DIR) + "/horizons_planets_1950_2050.csv");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("body,", 0) == 0) continue;
        std::stringstream ss(line);
        std::string body, f[4];
        std::getline(ss, body, ',');
        for (auto& x : f) std::getline(ss, x, ',');
        out[body].push_back({std::stod(f[0]), {std::stod(f[1]), std::stod(f[2]), std::stod(f[3])}});
    }
    return out;
}

// The v0.7.0 elements, verbatim, for the before/after comparison.
OrbitalElements legacyElements(const std::string& name) {
    struct L {
        const char* n;
        double a, e, i, per, w, node, M0;
    };
    static const L t[] = {
        {"Mercury", 0.38709927, 0.20563593, 7.005, 87.969, 29.127, 48.331, 174.793},
        {"Venus", 0.72333566, 0.00677672, 3.395, 224.701, 54.923, 76.680, 50.377},
        {"Earth", 1.00000261, 0.01671123, 0.000, 365.256, 102.938, 0.000, 357.527},
        {"Mars", 1.52371034, 0.09339410, 1.850, 686.980, 286.497, 49.560, 19.390},
        {"Jupiter", 5.20288700, 0.04838624, 1.304, 4332.59, 274.255, 100.474, 19.668},
        {"Saturn", 9.53667594, 0.05386179, 2.486, 10759.22, 338.937, 113.662, 317.355},
        {"Uranus", 19.18916464, 0.04725744, 0.773, 30688.5, 96.937, 74.017, 142.284},
        {"Neptune", 30.06992276, 0.00859048, 1.770, 60182.0, 273.181, 131.784, 259.915},
    };
    OrbitalElements e;
    for (const auto& x : t)
        if (name == x.n) {
            e.semiMajorAxisAU = x.a;
            e.eccentricity = x.e;
            e.inclinationDeg = x.i;
            e.periodDays = x.per;
            e.argPeriapsisDeg = x.w;
            e.longAscNodeDeg = x.node;
            e.meanAnomalyAtEpochDeg = x.M0;
        }
    return e;
}

struct Envelope {
    double maxKm = 0, rmsKm = 0, maxArcsec = 0;
    int n = 0;
};

Envelope measure(const KeplerOrbit& orbit, const std::vector<Sample>& ref) {
    Envelope env;
    double sumSq = 0.0;
    for (const auto& s : ref) {
        glm::dvec3 model = orbit.positionAtTime(s.jd - kJ2000) * kAuKm;
        double errKm = glm::length(model - s.km);
        double angle = std::acos(std::clamp(glm::dot(glm::normalize(model), glm::normalize(s.km)), -1.0, 1.0)) * kRadToArcsec;
        env.maxKm = std::max(env.maxKm, errKm);
        env.maxArcsec = std::max(env.maxArcsec, angle);
        sumSq += errKm * errKm;
        ++env.n;
    }
    env.rmsKm = env.n ? std::sqrt(sumSq / env.n) : 0.0;
    return env;
}
} // namespace

// Exposed for docs: tools print this table (ExoSimTests --horizons-table).
std::vector<std::string> g_horizonsTable;

void testHorizons() {
    auto ref = loadHorizons();
    check(
        "EPH-H0", "Horizons reference file loaded (8 planets x 51 epochs)", 408.0,
        [&] {
            double n = 0;
            for (auto& kv : ref) n += kv.second.size();
            return n;
        }(),
        0.0, "samples", "tests/data/horizons_planets_1950_2050.csv");
    if (ref.empty()) return;

    auto sol = Dataset::DemoSolarSystem();
    // Heliocentric-angle error limits (max over 1950-2050). Standish's
    // published accuracy for this table (1800-2050) is tens of arcseconds for
    // the terrestrial planets and Neptune/Uranus, and several hundred for
    // Jupiter and Saturn (their mutual perturbations are the largest
    // unmodeled effect). Limits are set at that class, rounded up.
    struct Limit {
        const char* name;
        double arcsec;
    };
    const Limit limits[] = {{"Mercury", 60},  {"Venus", 60},    {"Earth", 60},   {"Mars", 120},
                            {"Jupiter", 700}, {"Saturn", 1500}, {"Uranus", 200}, {"Neptune", 100}};
    char row[256];
    g_horizonsTable.push_back("| Planet | Max error, v0.8 (km) | RMS, v0.8 (km) | Max angle, v0.8 (arcsec) | Max error, v0.7 "
                              "(km) | Max angle, v0.7 (arcsec) | Improvement |");
    g_horizonsTable.push_back("|---|---|---|---|---|---|---|");
    int k = 1;
    for (const auto& lim : limits) {
        const auto& samples = ref[lim.name];
        KeplerOrbit now(solPlanet(sol, lim.name).elements);
        KeplerOrbit old(legacyElements(lim.name));
        Envelope a = measure(now, samples), b = measure(old, samples);
        check("EPH-H" + std::to_string(k++), std::string(lim.name) + ": max heliocentric angle error 1950-2050 vs Horizons", 0.0,
              a.maxArcsec, lim.arcsec, "arcsec", "JPL Horizons DE441; limit = Standish accuracy class");
        std::snprintf(row, sizeof(row), "| %s | %.0f | %.0f | %.1f | %.0f | %.0f | %.0fx |", lim.name, a.maxKm, a.rmsKm,
                      a.maxArcsec, b.maxKm, b.maxArcsec, b.maxKm / std::max(a.maxKm, 1.0));
        g_horizonsTable.push_back(row);
        // Rates help most planets a lot; over this window Uranus is ~1x (within
        // noise), so the requirement is "never materially worse".
        check("EPH-H" + std::to_string(k++), std::string(lim.name) + ": max error with rates <= 1.1 x v0.7 model", 1.0,
              a.maxKm <= 1.1 * b.maxKm ? 1.0 : 0.0, 0.0, "bool", "Before/after comparison");
    }

    // The fixture itself: every Horizons distance must lie within the
    // planet's perihelion-aphelion range (guards against a corrupted file).
    int bad = 0;
    for (const auto& p : sol) {
        auto it = ref.find(p.name);
        if (it == ref.end()) continue;
        const double q = p.elements.semiMajorAxisAU * (1 - p.elements.eccentricity) * kAuKm * 0.995;
        const double Q = p.elements.semiMajorAxisAU * (1 + p.elements.eccentricity) * kAuKm * 1.005;
        for (const auto& s : it->second) {
            double r = glm::length(s.km);
            if (r < q || r > Q) ++bad;
        }
    }
    check("EPH-H17", "Fixture sanity: all reference distances within perihelion-aphelion", 0.0, bad, 0.0, "samples",
          "Conic geometry");
}
