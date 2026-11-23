// GAL group: the Milky Way map's coordinates, host loading and the
// illustrative Galaxy model.
#include "Harness.h"

#include "galaxy/GalaxyModel.h"
#include "sky/SkyModel.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

#ifndef EXOSIM_TEST_DATA_DIR
#define EXOSIM_TEST_DATA_DIR "tests/data"
#endif

namespace {
constexpr double kPi = 3.14159265358979323846;

double angDiffDeg(double a, double b) { return std::fabs(std::fmod(a - b + 540.0, 360.0) - 180.0); }

void testCoordinates() {
    // Sgr A* (ICRS 17h45m40.04s, -29d00m28.2s) sits at l = 359.944, b = -0.046.
    glm::dvec2 gc = Galaxy::EquatorialToGalactic(266.41683, -29.00781);
    check("GAL-1", "Sgr A* Galactic longitude", 359.944, gc.x, 0.002, "deg", "Reid & Brunthaler 2004");
    check("GAL-2", "Sgr A* Galactic latitude", -0.046, gc.y, 0.002, "deg", "Reid & Brunthaler 2004");
    glm::dvec2 ngp = Galaxy::EquatorialToGalactic(192.85948, 27.12825);
    check("GAL-3", "North Galactic Pole latitude", 90.0, ngp.y, 1e-6, "deg", "IAU 1958 / J2000 definition");

    // Every row of the Archive sample: our conversion vs the Archive's own glon/glat.
    std::ifstream in(std::string(EXOSIM_TEST_DATA_DIR) + "/hosts_sample.json");
    nlohmann::json rows = nlohmann::json::parse(in, nullptr, false);
    double worst = rows.is_array() && !rows.empty() ? 0.0 : NAN;
    if (rows.is_array())
        for (const auto& r : rows) {
            if (!r["ra"].is_number() || !r["glon"].is_number()) continue;
            glm::dvec2 lb = Galaxy::EquatorialToGalactic(r["ra"].get<double>(), r["dec"].get<double>());
            worst = std::max({worst, angDiffDeg(lb.x, r["glon"].get<double>()) * std::cos(lb.y * kPi / 180.0),
                              std::fabs(lb.y - r["glat"].get<double>())});
        }
    check("GAL-4", "RA/Dec -> (l, b) vs the Archive's glon/glat, worst row", 0.0, worst, 1e-4, "deg",
          "NASA Exoplanet Archive (tests/data/hosts_sample.json)");

    glm::dvec3 toGc = Galaxy::GalacticToCartesian(0.0, 0.0, 100.0), rot = Galaxy::GalacticToCartesian(90.0, 0.0, 100.0);
    check("GAL-5", "l = 0 points along +X (toward the Galactic Center)", 100.0, toGc.x, 1e-9, "pc", "frame definition");
    check("GAL-6", "l = 90 points along +Y (direction of rotation)", 100.0, rot.y, 1e-9, "pc", "frame definition");
    check("GAL-7", "Galactocentric radius of the Sun", Galaxy::kSunToCenterPc, Galaxy::GalactocentricRadiusPc(glm::dvec3(0.0)),
          1e-9, "pc", "GRAVITY Collaboration 2019 (R0 = 8.178 kpc)");
}

void testHosts() {
    const std::string dir = EXOSIM_TEST_DATA_DIR;
    // Sample (real Archive rows): TRAPPIST-1 (7 rows, no sy_dist in the `ps`
    // default set), 55 Cnc (5), Proxima Cen (2), Kepler-1813 (no distance anywhere).
    std::vector<Galaxy::Host> psOnly = Galaxy::LoadHosts(dir + "/hosts_sample.json");
    check("GAL-8", "Hosts from `ps` alone (one per hostname; no-distance hosts skipped)", 2.0, static_cast<double>(psOnly.size()),
          0.0, "hosts", "tests/data/hosts_sample.json");
    Galaxy::HostLoadStats st;
    std::vector<Galaxy::Host> hosts = Galaxy::LoadHosts(dir + "/hosts_sample.json", dir + "/stellar_hosts_sample.json", &st);
    check("GAL-8b", "stellarhosts fallback adds TRAPPIST-1; Kepler-1813 still unplotted", 3.0 + 10.0 * 1.0,
          static_cast<double>(hosts.size()) + 10.0 * st.withoutDistance, 0.0, "hosts", "Archive stellarhosts sy_dist");
    const Galaxy::Host* trappist = nullptr;
    for (const auto& h : hosts)
        if (h.name == "TRAPPIST-1") trappist = &h;
    check("GAL-9", "TRAPPIST-1 planet count (rows grouped by host)", 7.0, trappist ? trappist->planets : NAN, 0.0, "planets",
          "Gillon et al. 2017");
    check("GAL-10", "TRAPPIST-1 position magnitude equals its stellarhosts distance", 12.4299,
          trappist ? glm::length(trappist->posPc) : NAN, 1e-9, "pc", "Gaia DR3 via Archive stellarhosts");
    check("GAL-11", "Missing host file -> empty list (no crash)", 0.0,
          static_cast<double>(Galaxy::LoadHosts("no/such/file.json").size()), 0.0, "hosts", "robustness");
}

void testModel() {
    // Arm crossings of the Sun-center line (Sun's side, x < R0): sign changes of y.
    const double tanP = std::tan(Galaxy::kPitchDeg * kPi / 180.0);
    const auto& arms = Galaxy::Arms();
    double worstCross = 0.0;
    for (const auto& arm : arms) {
        if (arm.spur) continue;
        auto line = Galaxy::ArmCenterline(arm, 4000);
        double bestErr = 1.0;
        for (size_t k = 1; k < line.size(); ++k)
            if (line[k - 1].x < Galaxy::kSunToCenterPc && (line[k - 1].y > 0.0) != (line[k].y > 0.0))
                bestErr = std::min(bestErr, std::fabs(Galaxy::GalactocentricRadiusPc(line[k]) / arm.radiusAtSunAzimuthPc - 1.0));
        worstCross = std::max(worstCross, bestErr);
    }
    check("GAL-12", "Arm centerlines cross the Sun-center line at their measured radii (worst arm)", 0.0, worstCross, 0.005,
          "fraction", "Reid et al. 2019 arm crossings");
    // A 4-arm, 12.5 deg log spiral predicts crossings exp(pi/2 tan p) apart; the measured radii should agree.
    double worstRatio = 0.0;
    for (size_t i = 1; i < 4; ++i)
        worstRatio = std::max(
            worstRatio,
            std::fabs(arms[i].radiusAtSunAzimuthPc / arms[i - 1].radiusAtSunAzimuthPc / std::exp(kPi / 2.0 * tanP) - 1.0));
    check("GAL-13", "Measured crossings consistent with one 4-arm log spiral (worst ratio error)", 0.0, worstRatio, 0.02,
          "fraction", "pitch 12.5 deg (Reid et al. 2019)");
    // Trailing arms: the Galaxy turns clockwise seen from the NGP, so
    // galactocentric azimuth must increase (counter-clockwise) outward.
    auto line = Galaxy::ArmCenterline(arms[1], 200);
    auto az = [](const glm::dvec3& p) { return std::atan2(p.y, p.x - Galaxy::kSunToCenterPc); };
    double dphi = std::remainder(az(line[101]) - az(line[100]), 2.0 * kPi);
    check("GAL-14", "Arms trail the clockwise rotation (azimuth grows outward)", 1.0, dphi > 0.0 ? 1.0 : 0.0, 0.0, "bool",
          "spiral-arm winding sense");

    auto a = Galaxy::GenerateModel(20000, 7), b = Galaxy::GenerateModel(20000, 7), c = Galaxy::GenerateModel(20000, 8);
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); ++i) same = a[i].posPc == b[i].posPc && a[i].rgba == b[i].rgba;
    check("GAL-15", "Model generation is deterministic for a seed", 1.0, same ? 1.0 : 0.0, 0.0, "bool", "reproducible frames");
    check("GAL-16", "A different seed gives a different field", 1.0, a[0].posPc != c[0].posPc ? 1.0 : 0.0, 0.0, "bool",
          "seeded RNG");
    // Surface density falls outward (exponential disk): 3-5 kpc vs 11-13 kpc annuli, same area.
    int inner = 0, outer = 0;
    double sumZ = 0.0;
    int nZ = 0;
    std::vector<double> barY;
    for (const auto& p : a) {
        double r = Galaxy::GalactocentricRadiusPc(glm::dvec3(p.posPc));
        if (r > 3000.0 && r < 5000.0) ++inner;
        if (r > 11000.0 && r < 13000.0) ++outer;
        if (r > 6000.0 && r < 10000.0) {
            sumZ += std::fabs(p.posPc.z + Galaxy::kSunHeightPc);
            ++nZ;
        }
        // Bar points carry alpha 0.45; on the Sun's side of the center their y gives the near end's side.
        if (std::fabs(p.rgba.a - 0.45f) < 1e-6f && p.posPc.x < Galaxy::kSunToCenterPc - 1500.0) barY.push_back(p.posPc.y);
    }
    const double densityRatio = static_cast<double>(inner) * (13.0 * 13.0 - 11.0 * 11.0) / (outer * (5.0 * 5.0 - 3.0 * 3.0));
    check("GAL-17", "Surface density 3-5 kpc / 11-13 kpc (exponential disk, h = 2.6 kpc: > 5)", 1.0,
          densityRatio > 5.0 ? 1.0 : 0.0, 0.0, "bool", "Bland-Hawthorn & Gerhard 2016");
    check("GAL-18", "Mean |z| of disk stars near the solar circle (thin disk)", 250.0, sumZ / std::max(nZ, 1), 150.0, "pc",
          "scale height ~300 pc");
    double meanBarY = 0.0;
    for (double y : barY) meanBarY += y / static_cast<double>(std::max<size_t>(barY.size(), 1));
    check("GAL-19", "Bar's near end lies at positive longitude (+Y)", 1.0, meanBarY > 0.0 ? 1.0 : 0.0, 0.0, "bool",
          "bar angle ~27 deg, near end at l > 0");
}

void testColorsAndLocalStars() {
    glm::vec3 sun = Galaxy::TeffToColor(5772.0), ref = Sky::BvToColor(0.653);
    check("GAL-20", "Teff 5772 K maps to the Sun's B-V color", 0.0, glm::length(sun - ref), 0.03, "rgb",
          "Ballesteros 2012 (B-V 0.653)");
    glm::vec3 hot = Galaxy::TeffToColor(30000.0), cool = Galaxy::TeffToColor(3000.0);
    check("GAL-21", "Hot stars bluer, cool stars redder", 1.0, (hot.b > hot.r && cool.r > cool.b) ? 1.0 : 0.0, 0.0, "bool",
          "stellar color sequence");
    StarCatalogEntry near, far;
    near.raDeg = 266.41683;
    near.decDeg = -29.00781;
    near.parallaxMas = 100.0;
    far.parallaxMas = 0.5;
    auto local = Galaxy::LocalStars({near, far});
    check("GAL-22", "Hipparcos star at 100 mas -> 10 pc toward l = 0; 0.5 mas excluded", 10.0,
          local.size() == 1 ? local[0].posPc.x : NAN, 0.01, "pc", "d = 1000 / parallax");
}
} // namespace

void testGalaxy() {
    testCoordinates();
    testHosts();
    testModel();
    testColorsAndLocalStars();
}
