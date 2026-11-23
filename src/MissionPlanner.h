#pragma once
#include <string>
#include <vector>
#include "KeplerOrbit.h"

// Earth-departure mission planning with real two-body orbital mechanics:
//   - Lambert (default): the optimal 3-D transfer between the planets' real
//     positions, found on a porkchop grid around the window and refined.
//   - Hohmann: the textbook coplanar minimum-energy transfer, used to find
//     the window and kept as a reference model.
// Both report patched-conic delta-v from low Earth orbit.
//
// What this IS: a textbook minimum-energy trajectory computed from the real
// J2000 orbital elements of the planets (so launch windows land on real
// calendar dates - e.g. it finds the real late-2026 Mars window).
//
// What this is NOT: a flown-mission trajectory. Real missions to the outer
// planets use gravity assists and higher launch energies to cut decades
// down to years (Voyager 2 reached Neptune in ~12 years, not the ~30 a
// Hohmann transfer takes; New Horizons reached Pluto in ~9.5) - see
// GravityAssist.h for flyby chains. The Hohmann model ignores inclination
// (it's computed in the ecliptic plane); Lambert transfers don't. Targets
// are treated as points - arriving at a moon
// of another planet means arriving in that planet's system, with the final
// capture and moon transfer not modeled.
// One coast arc of a heliocentric trajectory: the state at startDay,
// propagated two-body (any conic - flyby legs can be hyperbolic).
struct TrajectoryLeg {
    double startDay = 0.0, durationDays = 0.0;
    glm::dvec3 r0{0.0}, v0{0.0}; // heliocentric ecliptic J2000, AU and AU/day
};

struct MissionPlan {
    bool valid = false;
    std::string targetName;
    std::string arrivalBodyName; // parent planet when the target is another planet's moon
    bool earthMoonMission = false; // Earth -> Moon: geocentric transfer, no heliocentric window
    std::string method = "Hohmann"; // "Hohmann" (coplanar idealization) or "Lambert" (3-D, real positions)

    double launchDay = 0.0; // days since J2000
    double arrivalDay = 0.0;
    double transferDays = 0.0;
    double synodicPeriodDays = 0.0; // time between successive windows (0 for Earth-Moon)

    double r1AU = 0.0, r2AU = 0.0;
    bool outbound = true;
    double dvFromLeoKms = 0.0; // departure burn from a 200 km circular LEO (patched conic)
    double vInfDepartureKms = 0.0;
    double vInfArrivalKms = 0.0; // hyperbolic excess at arrival (capture burn: see MissionTimeline)
    double c3 = 0.0; // departure energy, km^2/s^2 (= vInfDeparture^2)
    double transferAngleDeg = 0.0;
    double inclinationDeg = 0.0; // of the heliocentric transfer orbit

    // Heliocentric transfer ellipse, propagated with time measured from
    // launchDay (i.e. positionAtTime(simDay - launchDay)). For multi-leg
    // (flyby) plans, this is the first leg; use positionAt().
    OrbitalElements transferElements;
    // Lambert and flyby plans: the coast arcs, in order. Empty for Hohmann
    // and Earth-Moon plans (which use transferElements).
    std::vector<TrajectoryLeg> legs;

    // Spacecraft heliocentric position on the trajectory at `day`, clamped
    // to [launchDay, arrivalDay].
    glm::dvec3 positionAt(double day) const;
};

class MissionPlanner {
public:
    // Finds the next Hohmann launch window at or after `fromDay` from Earth
    // to the body with elements `target`, and fills in the full plan.
    static MissionPlan PlanHohmann(const OrbitalElements& earth, const OrbitalElements& target, double fromDay,
                                   const std::string& targetName);

    // Earth -> Moon Hohmann transfer from a 200 km LEO (~5 days, matching
    // the real ~5-day outbound coast of Apollo/Artemis I). Launch is
    // immediate: real lunar windows are driven by landing-site lighting and
    // plane geometry (roughly monthly/daily), which isn't modeled here.
    static MissionPlan PlanEarthMoon(double fromDay);

    // Lambert transfer for a chosen launch day and time of flight.
    static MissionPlan PlanLambert(const OrbitalElements& earth, const OrbitalElements& target, double launchDay, double tofDays,
                                   const std::string& targetName);

    // The next window's optimal Lambert transfer (minimum departure burn +
    // arrival v_inf): porkchop grid around the Hohmann window, refined.
    // Falls back to the Hohmann plan if no Lambert solution is found.
    static MissionPlan PlanOptimal(const OrbitalElements& earth, const OrbitalElements& target, double fromDay,
                                   const std::string& targetName);

    // Calendar date (UTC, "YYYY-MM-DD") for a day count since J2000.
    static std::string DayToCalendar(double daysSinceJ2000);

    // Current real-world date as days since J2000, from the system clock.
    static double NowDaysSinceJ2000();
};
