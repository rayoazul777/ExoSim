#include "Porkchop.h"
#include "Lambert.h"

#include <cmath>
#include <limits>

double PorkchopGrid::Value(const TransferPoint& p, PorkchopMetric m) {
    if (!p.ok) return std::numeric_limits<double>::quiet_NaN();
    switch (m) {
        case PorkchopMetric::C3: return p.c3;
        case PorkchopMetric::DepartureDv: return p.dvFromLeoKms;
        case PorkchopMetric::ArrivalVinf: return p.vInfArrivalKms;
        case PorkchopMetric::Total: return p.total();
    }
    return p.total();
}

namespace Porkchop {

double DvFromLeo(double vInf) {
    return std::sqrt(vInf * vInf + 2.0 * kMuEarthKm3s2 / kLeoRadiusKm) - std::sqrt(kMuEarthKm3s2 / kLeoRadiusKm);
}

TransferPoint Evaluate(const KeplerOrbit& from, const KeplerOrbit& to, double launchDay, double tofDays) {
    TransferPoint p;
    p.launchDay = launchDay;
    p.tofDays = tofDays;
    const glm::dvec3 r1 = from.positionAtTime(launchDay);
    const glm::dvec3 r2 = to.positionAtTime(launchDay + tofDays);
    Lambert::Solution s = Lambert::Solve(r1, r2, tofDays, Orbit::kMuSunAU3Day2, true);
    if (!s.ok) return p;
    const glm::dvec3 vDep = (s.v1 - from.velocityAtTime(launchDay)) * Orbit::kAuPerDayToKms;
    const glm::dvec3 vArr = (s.v2 - to.velocityAtTime(launchDay + tofDays)) * Orbit::kAuPerDayToKms;
    p.vInfDepartureKms = glm::length(vDep);
    p.vInfArrivalKms = glm::length(vArr);
    p.c3 = p.vInfDepartureKms * p.vInfDepartureKms;
    p.dvFromLeoKms = DvFromLeo(p.vInfDepartureKms);
    p.transferAngleDeg = s.transferAngleRad * 180.0 / 3.14159265358979323846;
    p.r1 = r1;
    p.v1 = s.v1;
    p.ok = std::isfinite(p.c3) && std::isfinite(p.vInfArrivalKms);
    return p;
}

PorkchopGrid Compute(const KeplerOrbit& from, const KeplerOrbit& to, double launchStart, double launchEnd, int nLaunch,
                     double tofMin, double tofMax, int nTof) {
    PorkchopGrid g;
    g.nLaunch = std::max(nLaunch, 2);
    g.nTof = std::max(nTof, 2);
    g.launch0 = launchStart;
    g.launchStep = (launchEnd - launchStart) / (g.nLaunch - 1);
    g.tof0 = tofMin;
    g.tofStep = (tofMax - tofMin) / (g.nTof - 1);
    g.cells.resize(static_cast<size_t>(g.nLaunch) * g.nTof);
    double best = std::numeric_limits<double>::infinity();
    for (int j = 0; j < g.nTof; ++j) {
        for (int i = 0; i < g.nLaunch; ++i) {
            TransferPoint p = Evaluate(from, to, g.launch0 + i * g.launchStep, g.tof0 + j * g.tofStep);
            size_t idx = static_cast<size_t>(j) * g.nLaunch + i;
            g.cells[idx] = p;
            // Transfers within ~2 deg of 180 deg are near-singular (the plane
            // is ill-defined and C3 spikes); they can't be the optimum anyway.
            if (p.ok && p.total() < best && std::fabs(p.transferAngleDeg - 180.0) > 2.0) {
                best = p.total();
                g.bestIndex = static_cast<int>(idx);
            }
        }
    }
    return g;
}

TransferPoint Refine(const KeplerOrbit& from, const KeplerOrbit& to, const TransferPoint& seed, double earliestLaunch) {
    TransferPoint best = seed;
    if (!best.ok) return best;
    double stepL = std::max(1.0, seed.tofDays * 0.02), stepT = std::max(1.0, seed.tofDays * 0.02);
    for (int iter = 0; iter < 200 && (stepL > 0.01 || stepT > 0.01); ++iter) {
        bool improved = false;
        const double dl[4] = {stepL, -stepL, 0.0, 0.0};
        const double dt[4] = {0.0, 0.0, stepT, -stepT};
        for (int k = 0; k < 4; ++k) {
            double L = best.launchDay + dl[k], T = best.tofDays + dt[k];
            if (L < earliestLaunch || T < 1.0) continue;
            TransferPoint p = Evaluate(from, to, L, T);
            if (p.ok && p.total() < best.total() && std::fabs(p.transferAngleDeg - 180.0) > 2.0) {
                best = p;
                improved = true;
            }
        }
        if (!improved) {
            stepL *= 0.5;
            stepT *= 0.5;
        }
    }
    return best;
}

} // namespace Porkchop
