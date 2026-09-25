#pragma once
#include <string>
#include "KeplerOrbit.h"

// Idealized Earth-departure mission planning using real two-body orbital
// mechanics: Hohmann transfers, launch-window (phase angle) search, and
// patched-conic delta-v from low Earth orbit.
//
// What this IS: a textbook minimum-energy trajectory computed from the real
// J2000 orbital elements of the planets (so launch windows land on real
// calendar dates - e.g. it finds the real late-2026 Mars window).
//
// What this is NOT: a flown-mission trajectory. Real missions to the outer
// planets use gravity assists and higher launch energies to cut decades
// down to years (Voyager 2 reached Neptune in ~12 years, not the ~30 a
// Hohmann transfer takes; New Horizons reached Pluto in ~9.5). Planet
// inclinations are ignored in the transfer itself (it's computed in the
// ecliptic plane), and targets are treated as points - arriving at a moon
// of another planet means arriving in that planet's system, with the final
// capture and moon transfer not modeled.
struct MissionPlan {
    bool valid = false;
    std::string targetName;
    std::string arrivalBodyName;  // parent planet when the target is another planet's moon
    bool earthMoonMission = false; // Earth -> Moon: geocentric transfer, no heliocentric window

    double launchDay = 0.0;       // days since J2000
    double arrivalDay = 0.0;
    double transferDays = 0.0;
    double synodicPeriodDays = 0.0; // time between successive windows (0 for Earth-Moon)

    double r1AU = 0.0, r2AU = 0.0;
    bool outbound = true;
    double dvFromLeoKms = 0.0;    // departure burn from a 200 km circular LEO (patched conic)
    double vInfDepartureKms = 0.0;
    double vInfArrivalKms = 0.0;  // hyperbolic excess at arrival (capture burn not included)

    // Heliocentric transfer ellipse, propagated with time measured from
    // launchDay (i.e. positionAtTime(simDay - launchDay)).
    OrbitalElements transferElements;
};

class MissionPlanner {
public:
    // Finds the next Hohmann launch window at or after `fromDay` from Earth
    // to the body with elements `target`, and fills in the full plan.
    static MissionPlan PlanHohmann(const OrbitalElements& earth, const OrbitalElements& target,
                                   double fromDay, const std::string& targetName);

    // Earth -> Moon Hohmann transfer from a 200 km LEO (~5 days, matching
    // the real ~5-day outbound coast of Apollo/Artemis I). Launch is
    // immediate: real lunar windows are driven by landing-site lighting and
    // plane geometry (roughly monthly/daily), which isn't modeled here.
    static MissionPlan PlanEarthMoon(double fromDay);

    // Calendar date (UTC, "YYYY-MM-DD") for a day count since J2000.
    static std::string DayToCalendar(double daysSinceJ2000);

    // Current real-world date as days since J2000, from the system clock.
    static double NowDaysSinceJ2000();
};
