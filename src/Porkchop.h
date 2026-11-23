#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "KeplerOrbit.h"

// Porkchop plots: Lambert transfers over a grid of launch dates and times of
// flight between two bodies on their real (3-D, eccentric, secularly
// drifting) orbits. Each cell holds the departure energy C3 = v_inf^2, the
// arrival excess speed, and the burn from a 200 km low Earth orbit - the
// quantities mission designers read off real porkchop plots.
struct TransferPoint {
    bool ok = false;
    double launchDay = 0.0, tofDays = 0.0;
    double c3 = 0.0; // km^2/s^2
    double vInfDepartureKms = 0.0;
    double vInfArrivalKms = 0.0;
    double dvFromLeoKms = 0.0; // patched conic, 200 km circular LEO
    double transferAngleDeg = 0.0;
    glm::dvec3 r1{0.0}, v1{0.0}; // heliocentric departure state (AU, AU/day)
    // Default optimization objective: departure burn + arrival v_inf (the
    // usual "total" porkchop; arrival v_inf drives capture or entry cost).
    double total() const { return dvFromLeoKms + vInfArrivalKms; }
};

enum class PorkchopMetric { C3, DepartureDv, ArrivalVinf, Total };

struct PorkchopGrid {
    double launch0 = 0.0, launchStep = 1.0;
    double tof0 = 0.0, tofStep = 1.0;
    int nLaunch = 0, nTof = 0;
    std::vector<TransferPoint> cells; // [iTof * nLaunch + iLaunch]
    int bestIndex = -1; // lowest total()

    const TransferPoint& at(int iLaunch, int iTof) const { return cells[static_cast<size_t>(iTof) * nLaunch + iLaunch]; }
    static double Value(const TransferPoint& p, PorkchopMetric m);
};

namespace Porkchop {

constexpr double kLeoRadiusKm = 6378.137 + 200.0;
constexpr double kMuEarthKm3s2 = 398600.4418;

double DvFromLeo(double vInfKms);

TransferPoint Evaluate(const KeplerOrbit& from, const KeplerOrbit& to, double launchDay, double tofDays);

PorkchopGrid Compute(const KeplerOrbit& from, const KeplerOrbit& to, double launchStart, double launchEnd, int nLaunch,
                     double tofMin, double tofMax, int nTof);

// Local refinement of a (launch, tof) seed by pattern search on total(),
// with launch >= earliestLaunch.
TransferPoint Refine(const KeplerOrbit& from, const KeplerOrbit& to, const TransferPoint& seed, double earliestLaunch);

} // namespace Porkchop
