#pragma once
#include <string>
#include <vector>
#include "KeplerOrbit.h"
#include "MissionPlanner.h"

// Gravity-assist (flyby) trajectories in the patched-conic approximation:
// heliocentric Lambert legs joined at each flyby body, where the spacecraft
// swings through the body's sphere of influence (SOI) on a hyperbola.
//
// At each flyby the planet-relative excess velocity must turn from v_inf_in
// to v_inf_out. A flyby can only turn it by delta_max, set by the closest
// safe periapsis (sin(delta/2) = 1/e, e = 1 + rp v_inf^2 / mu), and can't
// change its magnitude - so the chain is "powered": a burn at periapsis
// makes up any speed mismatch (Delta-v = |v_p,out - v_p,in|), and turning
// beyond delta_max is charged as a deep-space burn of 2 v sin(deficit/2).
// A ballistic gravity assist is one with ~0 powered Delta-v.
//
// Sphere of influence: r_SOI = a (m/M_sun)^(2/5) (Laplace). Entry/exit
// times come from the flyby hyperbola's time from SOI radius to periapsis.
struct FlybyEncounter {
    std::string body;
    double day = 0.0; // closest approach
    double vInfInKms = 0.0, vInfOutKms = 0.0;
    double turnAngleDeg = 0.0; // required
    double maxTurnDeg = 0.0; // at the minimum safe periapsis
    double periapsisAltitudeKm = 0.0; // of the flyby hyperbola
    double poweredDvKms = 0.0; // periapsis burn + any turn deficit
    double soiRadiusKm = 0.0;
    double soiEntryDay = 0.0, soiExitDay = 0.0;
};

struct FlybyPlan {
    bool valid = false;
    std::string sequenceName;
    std::vector<std::string> bodies; // departure, flybys..., destination
    std::vector<double> encounterDays; // launch, flybys..., arrival
    std::vector<TrajectoryLeg> legs;
    std::vector<FlybyEncounter> flybys;
    double c3 = 0.0, vInfDepartureKms = 0.0, dvFromLeoKms = 0.0, vInfArrivalKms = 0.0;
    double totalPoweredDvKms = 0.0;
    double totalDays = 0.0;
    double directHohmannDays = 0.0; // for comparison: a direct Hohmann transfer to the destination

    double cost() const { return dvFromLeoKms + totalPoweredDvKms + vInfArrivalKms; }
    MissionPlan AsMissionPlan(const std::string& targetLabel) const;
};

namespace GravityAssist {

struct Sequence {
    std::string name;
    std::vector<std::string> bodies; // first is Earth
    double maxYears; // trip-time cap used by the optimizer
    double searchYears; // how far ahead to look for a launch
    std::string note; // the real mission it echoes
};

// The flyby chains offered in the app.
const std::vector<Sequence>& Sequences();

// Laplace sphere-of-influence radius (km) of a planet with gravitational
// parameter gm (km^3/s^2) on an orbit of semi-major axis aAU.
double SoiRadiusKm(double gmKm3s2, double aAU);

// Maximum turn of a flyby with excess speed vInf at periapsis radius rp.
double MaxTurnRad(double vInfKms, double rpKm, double gmKm3s2);

// Powered flyby: fills the turn angle, periapsis and Delta-v for given
// planet-relative incoming/outgoing excess velocities (km/s).
FlybyEncounter EvaluateFlyby(const std::string& body, const glm::dvec3& vInfIn, const glm::dvec3& vInfOut);

// Evaluates a chain for fixed dates (launch, then each leg's time of flight).
FlybyPlan Evaluate(const Sequence& seq, const std::vector<OrbitalElements>& bodies, double launchDay,
                   const std::vector<double>& tofDays);

// Best chain launching at or after fromDay (grid search with pruning, then
// pattern-search refinement), minimizing cost() within seq.maxYears.
FlybyPlan Optimize(const Sequence& seq, const std::vector<OrbitalElements>& bodies, double fromDay);

} // namespace GravityAssist
