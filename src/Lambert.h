#pragma once
#include <glm/glm.hpp>
#include "KeplerOrbit.h"

// Lambert's problem: the conic that connects position r1 to position r2 in
// a given time of flight - the core of real interplanetary trajectory
// design (porkchop plots, flyby legs). Unlike the Hohmann planner it uses
// the actual 3-D positions of both bodies on the chosen dates, so it
// handles inclined and eccentric orbits and arbitrary dates.
//
// Method: universal variables with Stumpff functions (Bate, Mueller & White
// 1971, sec. 5.3; Vallado, "Fundamentals of Astrodynamics", Algorithm 58),
// solved by bisection on psi = (change in eccentric anomaly)^2, which is
// monotonic in time of flight for single-revolution transfers and therefore
// always converges. Single revolution only. The transfer angle is taken in
// the prograde sense (counter-clockwise seen from +z) unless prograde=false.
namespace Lambert {

struct Solution {
    bool ok = false;
    glm::dvec3 v1{0.0}, v2{0.0};
    double transferAngleRad = 0.0;
    int iterations = 0;
};

// Units are consistent with mu: e.g. AU, days and mu in AU^3/day^2.
Solution Solve(const glm::dvec3& r1, const glm::dvec3& r2, double tof, double mu, bool prograde = true);

// Stumpff functions C(z), S(z) (exposed for tests).
double StumpffC(double z);
double StumpffS(double z);

} // namespace Lambert

namespace Orbit {

constexpr double kMuSunAU3Day2 = 2.9591220828559115e-4; // GM_sun in AU^3/day^2 (= 1.32712440018e11 km^3/s^2)
constexpr double kAuKm = 1.495978707e8;
constexpr double kAuPerDayToKms = kAuKm / 86400.0;

// Classical elements of the (elliptic) orbit through state (r, v), with
// the epoch at that state: meanAnomalyAtEpochDeg is M at the state and
// periodDays = 2 pi sqrt(a^3/mu) in mu's time unit. Equatorial and circular
// orbits use the usual conventions (node 0; periapsis at the node/x-axis).
// Returns elements with semiMajorAxisAU <= 0 for unbound (e >= 1) states.
OrbitalElements ElementsFromState(const glm::dvec3& r, const glm::dvec3& v, double mu);

// Two-body propagation of a state by dt (any conic: elliptic, parabolic or
// hyperbolic) with universal variables (Curtis, Algorithm 3.4). Returns the
// position; velocity too if `vOut` is given.
glm::dvec3 Propagate(const glm::dvec3& r0, const glm::dvec3& v0, double dt, double mu, glm::dvec3* vOut = nullptr);

} // namespace Orbit
