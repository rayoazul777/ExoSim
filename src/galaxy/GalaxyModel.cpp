#include "galaxy/GalaxyModel.h"
#include "Log.h"
#include "sky/SkyModel.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
// J2000 North Galactic Pole and the Galactic longitude of the North Celestial Pole.
constexpr double kRaNgpDeg = 192.85948, kDecNgpDeg = 27.12825, kLNcpDeg = 122.93192;

double numOr(const nlohmann::json& r, const char* k, double d) {
    return r.contains(k) && r[k].is_number() ? r[k].get<double>() : d;
}
} // namespace

namespace Galaxy {

glm::dvec2 EquatorialToGalactic(double raDeg, double decDeg) {
    const double a = raDeg * kDeg, d = decDeg * kDeg, aG = kRaNgpDeg * kDeg, dG = kDecNgpDeg * kDeg;
    const double sinb = std::sin(d) * std::sin(dG) + std::cos(d) * std::cos(dG) * std::cos(a - aG);
    const double b = std::asin(std::clamp(sinb, -1.0, 1.0));
    const double y = std::cos(d) * std::sin(a - aG);
    const double x = std::sin(d) * std::cos(dG) - std::cos(d) * std::sin(dG) * std::cos(a - aG);
    double l = kLNcpDeg - std::atan2(y, x) / kDeg;
    l = std::fmod(l + 720.0, 360.0);
    return {l, b / kDeg};
}

glm::dvec3 GalacticToCartesian(double lDeg, double bDeg, double distPc) {
    const double l = lDeg * kDeg, b = bDeg * kDeg;
    return {distPc * std::cos(b) * std::cos(l), distPc * std::cos(b) * std::sin(l), distPc * std::sin(b)};
}

double GalactocentricRadiusPc(const glm::dvec3& p) { return std::hypot(p.x - kSunToCenterPc, p.y); }

const std::vector<Arm>& Arms() {
    // Each arm is anchored at its measured crossing of the Sun-center line
    // (Reid et al. 2019). With a common 12.5 deg pitch, four arms 90 deg
    // apart cross a factor exp(pi/2 tan p) = 1.417 apart in radius, and the
    // measured crossings follow that ratio: 4.66, 6.6, 9.35, 13.25 kpc
    // (Norma-Outer also crosses at 13.25 / exp(2 pi tan p) = 3.29 kpc, where
    // it is called the Norma arm).
    static const std::vector<Arm> arms = {
        {"Scutum-Centaurus", 4660.0}, {"Sagittarius-Carina", 6600.0},           {"Perseus", 9350.0},
        {"Norma-Outer", 13250.0},     {"Local Arm (Orion Spur)", 8300.0, true},
    };
    return arms;
}

namespace {
// Galactocentric frame: GC at origin, Sun at (-R0, 0); azimuth phi
// counter-clockwise from +x seen from the NGP (the Sun is at phi = pi). An
// arm is r = r_cross exp((phi - pi) tan p): radius grows counter-clockwise,
// against the clockwise rotation, so the arms trail.
glm::dvec3 toHelio(double r, double phi, double z) {
    return {kSunToCenterPc + r * std::cos(phi), r * std::sin(phi), z - kSunHeightPc};
}
// Azimuth at which an arm reaches radius r.
double armPhi(const Arm& arm, double r) { return kPi + std::log(r / arm.radiusAtSunAzimuthPc) / std::tan(kPitchDeg * kDeg); }
} // namespace

std::vector<glm::dvec3> ArmCenterline(const Arm& arm, int samples) {
    std::vector<glm::dvec3> out;
    double r0 = arm.spur ? 7800.0 : 2500.0, r1 = arm.spur ? 9000.0 : 17000.0;
    for (int i = 0; i < samples; ++i) {
        double r = r0 * std::pow(r1 / r0, static_cast<double>(i) / (samples - 1));
        double phi = arm.spur ? kPi + (r - 8300.0) / 8300.0 * 1.6 : armPhi(arm, r);
        out.push_back(toHelio(r, phi, 0.0));
    }
    return out;
}

std::vector<ModelPoint> GenerateModel(int count, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::normal_distribution<double> N(0.0, 1.0);
    std::vector<ModelPoint> pts;
    pts.reserve(static_cast<size_t>(count));
    const auto& arms = Arms();
    const glm::vec3 armBlue(0.62f, 0.72f, 1.0f), diskWhite(0.95f, 0.92f, 0.85f), bulgeGold(1.0f, 0.82f, 0.55f);
    const double barAngle = 27.0 * kDeg; // near end toward l > 0 (+Y side of the Sun-center line)
    for (int i = 0; i < count; ++i) {
        double u = U(rng);
        ModelPoint p{};
        if (u < 0.55) {
            // Spiral arm star: pick an arm (spur rarer), a radius, scatter across the arm.
            int k = U(rng) < 0.06 ? 4 : static_cast<int>(U(rng) * 4.0);
            const Arm& arm = arms[static_cast<size_t>(k)];
            double r = arm.spur ? 7800.0 + U(rng) * 1200.0 : 2500.0 * std::pow(17000.0 / 2500.0, std::pow(U(rng), 0.8));
            double phi = arm.spur ? kPi + (r - 8300.0) / 8300.0 * 1.6 : armPhi(arm, r);
            double width = 180.0 + 0.04 * r;
            glm::dvec3 g = toHelio(r, phi, N(rng) * 120.0);
            g.x += N(rng) * width;
            g.y += N(rng) * width;
            p.posPc = glm::vec3(g);
            float young = static_cast<float>(U(rng));
            p.rgba = glm::vec4(glm::mix(armBlue, diskWhite, young * 0.5f), 0.55f);
            p.sizePx = 1.4f + 1.6f * static_cast<float>(U(rng) * U(rng));
        } else if (u < 0.85) {
            // Exponential disk (scale length 2.6 kpc, height 300 pc).
            double r = -2600.0 * std::log(1.0 - U(rng) * 0.995);
            double phi = U(rng) * 2.0 * kPi;
            double z = -300.0 * std::log(1.0 - U(rng)) * (U(rng) < 0.5 ? -1.0 : 1.0);
            p.posPc = glm::vec3(toHelio(r, phi, z));
            p.rgba = glm::vec4(diskWhite, 0.30f);
            p.sizePx = 1.2f + static_cast<float>(U(rng));
        } else if (u < 0.95) {
            // Bar: elongated, half-length 4.4 kpc.
            double a = std::clamp(N(rng) * 1800.0, -4400.0, 4400.0), b = N(rng) * 600.0, z = N(rng) * 250.0;
            double x = a * std::cos(barAngle) - b * std::sin(barAngle), y = a * std::sin(barAngle) + b * std::cos(barAngle);
            p.posPc =
                glm::vec3(static_cast<float>(kSunToCenterPc - x), static_cast<float>(y), static_cast<float>(z - kSunHeightPc));
            p.rgba = glm::vec4(bulgeGold, 0.45f);
            p.sizePx = 1.5f + static_cast<float>(U(rng));
        } else {
            // Bulge.
            p.posPc = glm::vec3(static_cast<float>(kSunToCenterPc + N(rng) * 700.0), static_cast<float>(N(rng) * 700.0),
                                static_cast<float>(N(rng) * 500.0 - kSunHeightPc));
            p.rgba = glm::vec4(bulgeGold, 0.55f);
            p.sizePx = 1.6f + static_cast<float>(U(rng));
        }
        pts.push_back(p);
    }
    return pts;
}

std::vector<Host> LoadHosts(const std::string& path, const std::string& stellarHostsPath, HostLoadStats* stats) {
    std::vector<Host> hosts;
    HostLoadStats st;
    std::ifstream in(path);
    if (!in) return hosts;
    nlohmann::json rows = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (rows.is_discarded() || !rows.is_array()) {
        Log::Warn("galaxy", "Could not parse " + path);
        return hosts;
    }
    // Fallback distances from the stellarhosts table (first non-null per host).
    std::unordered_map<std::string, double> altDist;
    if (!stellarHostsPath.empty()) {
        std::ifstream sin(stellarHostsPath);
        nlohmann::json srows = sin ? nlohmann::json::parse(sin, nullptr, false) : nlohmann::json();
        if (srows.is_array())
            for (const auto& r : srows) {
                if (!r.is_object() || !r.contains("hostname") || !r["hostname"].is_string()) continue;
                double d = numOr(r, "sy_dist", -1.0);
                if (d > 0.0) altDist.emplace(r["hostname"].get<std::string>(), d);
            }
    }
    // Group rows by host first: a host is plotted if any row (or stellarhosts) has its distance.
    struct Acc {
        double dist = -1.0, ra = NAN, dec = NAN, teff = 0.0;
        int planets = 0;
    };
    std::vector<std::string> order;
    std::unordered_map<std::string, Acc> acc;
    for (const auto& r : rows) {
        if (!r.is_object() || !r.contains("hostname") || !r["hostname"].is_string()) continue;
        const std::string name = r["hostname"].get<std::string>();
        auto [it, inserted] = acc.try_emplace(name);
        if (inserted) order.push_back(name);
        Acc& a = it->second;
        ++a.planets;
        if (!(a.dist > 0.0)) a.dist = numOr(r, "sy_dist", -1.0);
        if (!std::isfinite(a.ra)) a.ra = numOr(r, "ra", NAN);
        if (!std::isfinite(a.dec)) a.dec = numOr(r, "dec", NAN);
        if (!(a.teff > 0.0)) a.teff = numOr(r, "st_teff", 0.0);
    }
    for (const std::string& name : order) {
        const Acc& a = acc[name];
        double dist = a.dist;
        if (!(dist > 0.0)) {
            auto alt = altDist.find(name);
            if (alt != altDist.end()) {
                dist = alt->second;
                ++st.fromStellarHosts;
            }
        }
        if (!(dist > 0.0) || !std::isfinite(a.ra) || !std::isfinite(a.dec)) {
            ++st.withoutDistance;
            continue;
        }
        Host h;
        h.name = name;
        h.raDeg = a.ra;
        h.decDeg = a.dec;
        h.distPc = dist;
        glm::dvec2 lb = EquatorialToGalactic(a.ra, a.dec);
        h.lDeg = lb.x;
        h.bDeg = lb.y;
        h.teffK = a.teff;
        h.planets = a.planets;
        h.posPc = GalacticToCartesian(h.lDeg, h.bDeg, h.distPc);
        hosts.push_back(std::move(h));
    }
    Log::Info("galaxy",
              Log::Cat("Plotted ", hosts.size(), " exoplanet host stars (", st.fromStellarHosts,
                       " distances from stellar_hosts.json; ", st.withoutDistance, " hosts have no distance)."),
              {{"hosts", static_cast<double>(hosts.size())},
               {"fromStellarHosts", st.fromStellarHosts},
               {"withoutDistance", st.withoutDistance}});
    if (stats) *stats = st;
    return hosts;
}

glm::vec3 TeffToColor(double teffK) {
    if (!(teffK > 0.0)) return Sky::BvToColor(0.65);
    // Invert Ballesteros (2012): T = 4600 (1/(0.92 BV + 1.7) + 1/(0.92 BV + 0.62)).
    // With x = 0.92 BV and k = T/4600: k x^2 + (2.32k - 2) x + (1.054k - 2.32) = 0.
    const double k = std::clamp(teffK, 2000.0, 40000.0) / 4600.0;
    const double B = 2.32 * k - 2.0, C = 1.054 * k - 2.32;
    const double x = (-B + std::sqrt(std::max(0.0, B * B - 4.0 * k * C))) / (2.0 * k);
    return Sky::BvToColor(x / 0.92);
}

std::vector<LocalStar> LocalStars(const std::vector<StarCatalogEntry>& stars) {
    std::vector<LocalStar> out;
    for (const auto& s : stars) {
        if (!(s.parallaxMas > 1.0)) continue; // within 1 kpc and a usable parallax
        glm::dvec2 lb = EquatorialToGalactic(s.raDeg, s.decDeg);
        LocalStar ls;
        ls.posPc = GalacticToCartesian(lb.x, lb.y, 1000.0 / s.parallaxMas);
        ls.color = Sky::BvToColor(s.hasBv ? s.bv : 0.6);
        ls.vmag = s.vmag;
        out.push_back(ls);
    }
    return out;
}

} // namespace Galaxy
