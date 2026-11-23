#include "GravityAssist.h"
#include "BodyConstants.h"
#include "Lambert.h"
#include "Porkchop.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMuSunKm = 1.32712440018e11;
const double kInf = std::numeric_limits<double>::infinity();

// Closest safe flyby altitude: above the atmosphere / ring plane, roughly as
// real missions planned them (Galileo, Cassini, Juno, New Horizons).
double minAltitudeKm(const std::string& body) {
    if (body == "Jupiter") return 0.1 * 69911.0; // radiation belts (Juno dips lower; Pioneer 11 went to 0.6 R_J)
    if (body == "Saturn") return 0.3 * 58232.0; // outside the main rings' plane crossing
    if (body == "Uranus" || body == "Neptune") return 0.1 * (body == "Uranus" ? 25362.0 : 24622.0);
    return 300.0; // terrestrial planets
}

double hohmannDays(double a1, double a2) {
    const double a = 0.5 * (a1 + a2);
    return kPi * std::sqrt(a * a * a / Orbit::kMuSunAU3Day2);
}

// Hyperbolic time from periapsis to radius r (seconds).
double timeFromPeriapsisSec(double r, double rp, double vInf, double mu) {
    const double a = -mu / (vInf * vInf); // negative
    const double e = 1.0 - rp / a; // = 1 + rp vInf^2/mu
    const double coshH = (1.0 - r / a) / e;
    if (coshH <= 1.0) return 0.0;
    const double H = std::acosh(coshH);
    return std::sqrt(-a * a * a / mu) * (e * std::sinh(H) - H);
}
} // namespace

MissionPlan FlybyPlan::AsMissionPlan(const std::string& targetLabel) const {
    MissionPlan m;
    m.valid = valid;
    m.method = "Gravity assist (" + sequenceName + ")";
    m.targetName = targetLabel;
    m.arrivalBodyName = bodies.empty() ? "" : bodies.back();
    m.launchDay = encounterDays.empty() ? 0.0 : encounterDays.front();
    m.arrivalDay = encounterDays.empty() ? 0.0 : encounterDays.back();
    m.transferDays = m.arrivalDay - m.launchDay;
    m.vInfDepartureKms = vInfDepartureKms;
    m.vInfArrivalKms = vInfArrivalKms;
    m.dvFromLeoKms = dvFromLeoKms;
    m.c3 = c3;
    m.legs = legs;
    if (!legs.empty()) {
        m.r1AU = glm::length(legs.front().r0);
        OrbitalElements el = Orbit::ElementsFromState(legs.front().r0, legs.front().v0, Orbit::kMuSunAU3Day2);
        if (el.semiMajorAxisAU > 0.0) m.transferElements = el;
        m.inclinationDeg = el.inclinationDeg;
    }
    return m;
}

namespace GravityAssist {

const std::vector<Sequence>& Sequences() {
    static const std::vector<Sequence> s = {
        {"Earth-Jupiter-Saturn",
         {"Earth", "Jupiter", "Saturn"},
         4.5,
         25.0,
         "Voyager 1 & 2 (1977): Saturn in ~3.2-4 years instead of ~6"},
        {"Earth-Jupiter-Uranus", {"Earth", "Jupiter", "Uranus"}, 9.0, 15.0, "Voyager 2 reached Uranus in 8.4 years (1986)"},
        {"Earth-Jupiter-Neptune", {"Earth", "Jupiter", "Neptune"}, 12.0, 15.0, "Voyager 2 reached Neptune in 12 years (1989)"},
        {"Earth-Jupiter-Pluto", {"Earth", "Jupiter", "Pluto"}, 10.0, 15.0, "New Horizons: Jupiter 2007, Pluto 2015 (9.5 years)"},
        {"Earth-Venus-Earth-Jupiter",
         {"Earth", "Venus", "Earth", "Jupiter"},
         6.5,
         3.0,
         "Galileo used Venus-Earth-Earth assists (VEEGA) to reach Jupiter with a small launch"},
    };
    return s;
}

double SoiRadiusKm(double gm, double aAU) { return aAU * Orbit::kAuKm * std::pow(gm / kMuSunKm, 0.4); }

double MaxTurnRad(double vInf, double rp, double gm) {
    const double e = 1.0 + rp * vInf * vInf / gm;
    return 2.0 * std::asin(1.0 / e);
}

FlybyEncounter EvaluateFlyby(const std::string& body, const glm::dvec3& vIn, const glm::dvec3& vOut) {
    FlybyEncounter f;
    f.body = body;
    const BodyConstants* b = Bodies::Lookup(body);
    const double gm = b ? b->gmKm3s2 : 1.0, R = b ? b->radiusKm : 1.0;
    const double vi = glm::length(vIn), vo = glm::length(vOut);
    f.vInfInKms = vi;
    f.vInfOutKms = vo;
    const double cosd = std::clamp(glm::dot(vIn, vOut) / (vi * vo), -1.0, 1.0);
    const double delta = std::acos(cosd);
    f.turnAngleDeg = delta * 180.0 / kPi;
    const double rpMin = R + minAltitudeKm(body);

    // Turn achieved by a powered flyby with periapsis rp: half from each branch.
    auto turnAt = [&](double rp) {
        return std::asin(1.0 / (1.0 + rp * vi * vi / gm)) + std::asin(1.0 / (1.0 + rp * vo * vo / gm));
    };
    const double maxTurn = turnAt(rpMin);
    f.maxTurnDeg = maxTurn * 180.0 / kPi;
    double rp = rpMin, deficitDv = 0.0;
    if (delta <= maxTurn) {
        // Bisect for the periapsis that gives exactly the required turn.
        double lo = rpMin, hi = rpMin;
        while (turnAt(hi) > delta && hi < 1e12) hi *= 2.0;
        for (int i = 0; i < 80; ++i) {
            double mid = 0.5 * (lo + hi);
            if (turnAt(mid) > delta)
                lo = mid;
            else
                hi = mid;
        }
        rp = 0.5 * (lo + hi);
    } else {
        deficitDv = 2.0 * std::min(vi, vo) * std::sin(0.5 * (delta - maxTurn)); // deep-space turn burn
    }
    f.periapsisAltitudeKm = rp - R;
    f.poweredDvKms = std::fabs(std::sqrt(vo * vo + 2.0 * gm / rp) - std::sqrt(vi * vi + 2.0 * gm / rp)) + deficitDv;
    return f;
}

FlybyPlan Evaluate(const Sequence& seq, const std::vector<OrbitalElements>& bodies, double launchDay,
                   const std::vector<double>& tofs) {
    FlybyPlan p;
    p.sequenceName = seq.name;
    p.bodies = seq.bodies;
    const size_t n = bodies.size();
    if (n < 2 || tofs.size() != n - 1) return p;
    std::vector<KeplerOrbit> orbits(bodies.begin(), bodies.end());
    double day = launchDay;
    p.encounterDays.push_back(day);
    glm::dvec3 prevArrivalV(0.0);
    for (size_t k = 0; k + 1 < n; ++k) {
        const double tof = tofs[k];
        if (tof <= 0.0) return p;
        const glm::dvec3 r1 = orbits[k].positionAtTime(day), r2 = orbits[k + 1].positionAtTime(day + tof);
        Lambert::Solution s = Lambert::Solve(r1, r2, tof, Orbit::kMuSunAU3Day2, true);
        if (!s.ok) return p;
        const glm::dvec3 vBody1 = orbits[k].velocityAtTime(day);
        if (k == 0) {
            p.vInfDepartureKms = glm::length(s.v1 - vBody1) * Orbit::kAuPerDayToKms;
            p.c3 = p.vInfDepartureKms * p.vInfDepartureKms;
            p.dvFromLeoKms = Porkchop::DvFromLeo(p.vInfDepartureKms);
        } else {
            FlybyEncounter f = EvaluateFlyby(seq.bodies[k], (prevArrivalV - vBody1) * Orbit::kAuPerDayToKms,
                                             (s.v1 - vBody1) * Orbit::kAuPerDayToKms);
            f.day = day;
            p.flybys.push_back(f);
            p.totalPoweredDvKms += f.poweredDvKms;
        }
        p.legs.push_back({day, tof, r1, s.v1});
        prevArrivalV = s.v2;
        day += tof;
        p.encounterDays.push_back(day);
    }
    p.vInfArrivalKms = glm::length(prevArrivalV - orbits[n - 1].velocityAtTime(day)) * Orbit::kAuPerDayToKms;
    p.totalDays = day - launchDay;
    p.directHohmannDays = hohmannDays(bodies.front().semiMajorAxisAU, bodies.back().semiMajorAxisAU);

    // Sphere-of-influence crossing times for each flyby.
    for (size_t i = 0; i < p.flybys.size(); ++i) {
        FlybyEncounter& f = p.flybys[i];
        const BodyConstants* b = Bodies::Lookup(f.body);
        if (!b) continue;
        f.soiRadiusKm = SoiRadiusKm(b->gmKm3s2, bodies[i + 1].semiMajorAxisAU);
        const double rp = b->radiusKm + f.periapsisAltitudeKm;
        f.soiEntryDay = f.day - timeFromPeriapsisSec(f.soiRadiusKm, rp, f.vInfInKms, b->gmKm3s2) / 86400.0;
        f.soiExitDay = f.day + timeFromPeriapsisSec(f.soiRadiusKm, rp, f.vInfOutKms, b->gmKm3s2) / 86400.0;
    }
    p.valid = std::isfinite(p.cost());
    return p;
}

FlybyPlan Optimize(const Sequence& seq, const std::vector<OrbitalElements>& bodies, double fromDay) {
    const size_t legs = bodies.size() - 1;
    const double cap = seq.maxYears * 365.25;
    // Per-leg time-of-flight ranges around each leg's Hohmann time.
    std::vector<double> tMin(legs), tMax(legs);
    for (size_t k = 0; k < legs; ++k) {
        double th = hohmannDays(bodies[k].semiMajorAxisAU, bodies[k + 1].semiMajorAxisAU);
        if (seq.bodies[k] == seq.bodies[k + 1]) th = 365.25; // resonant return (not used by the built-in sequences)
        tMax[k] = std::min(cap, 1.3 * th);
        tMin[k] = std::max(30.0, std::min(0.2 * th, 0.5 * tMax[k]));
    }
    const int nT = legs >= 3 ? 9 : 14;
    const double launchStep = legs >= 3 ? 8.0 : 20.0;
    const double searchEnd = fromDay + seq.searchYears * 365.25;

    FlybyPlan best;
    double bestCost = kInf;
    std::vector<double> tofs(legs);
    // Depth-first grid with pruning on the partial cost.
    std::function<void(size_t, double, double)> search;
    std::vector<KeplerOrbit> orbits(bodies.begin(), bodies.end());
    const int nLaunch = static_cast<int>((searchEnd - fromDay) / launchStep);
    for (int li = 0; li <= nLaunch; ++li) {
        const double L = fromDay + li * launchStep;
        search = [&](size_t k, double elapsed, double partial) {
            if (partial >= bestCost) return;
            if (k == legs) {
                FlybyPlan p = Evaluate(seq, bodies, L, tofs);
                if (p.valid && p.cost() < bestCost) {
                    bestCost = p.cost();
                    best = p;
                }
                return;
            }
            for (int i = 0; i < nT; ++i) {
                double t = tMin[k] + (tMax[k] - tMin[k]) * i / (nT - 1);
                if (elapsed + t > cap) break;
                tofs[k] = t;
                double lower = partial;
                if (k == 0) {
                    // Cheap lower bound: the departure burn alone.
                    TransferPoint d = Porkchop::Evaluate(orbits[0], orbits[1], L, t);
                    // ~10 km/s from LEO is C3 ~ 190: beyond New Horizons' record ~158.
                    if (!d.ok || d.dvFromLeoKms > 10.0) continue;
                    lower = d.dvFromLeoKms;
                }
                search(k + 1, elapsed + t, lower);
            }
        };
        search(0, 0.0, 0.0);
    }
    if (!best.valid) return best;

    // Pattern-search refinement over (launch, tof_1..tof_n).
    std::vector<double> x(legs + 1);
    x[0] = best.encounterDays.front();
    for (size_t k = 0; k < legs; ++k) x[k + 1] = best.encounterDays[k + 1] - best.encounterDays[k];
    std::vector<double> step(legs + 1, 8.0);
    for (int iter = 0; iter < 400; ++iter) {
        bool improved = false;
        for (size_t d = 0; d < x.size(); ++d) {
            for (double sgn : {1.0, -1.0}) {
                std::vector<double> y = x;
                y[d] += sgn * step[d];
                if (y[0] < fromDay) continue;
                double total = 0.0;
                bool okT = true;
                for (size_t k = 1; k < y.size(); ++k) {
                    if (y[k] < 10.0) okT = false;
                    total += y[k];
                }
                if (!okT || total > cap) continue;
                FlybyPlan p = Evaluate(seq, bodies, y[0], std::vector<double>(y.begin() + 1, y.end()));
                if (p.valid && p.cost() < best.cost()) {
                    best = p;
                    x = y;
                    improved = true;
                }
            }
        }
        if (!improved) {
            bool done = true;
            for (double& s : step) {
                s *= 0.5;
                if (s > 0.05) done = false;
            }
            if (done) break;
        }
    }
    return best;
}

} // namespace GravityAssist
