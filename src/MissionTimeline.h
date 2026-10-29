#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "MissionPlanner.h"
#include "LandingSimulator.h"

// Mission sequence ("timeline") built from a MissionPlan: the ordered list
// of time-tagged events a flight team would see in a sequence of events
// (SOE) - launch, injection burn, trajectory-correction slots, approach,
// arrival burn or atmospheric entry, and the landing sequence from
// LandingSimulator - plus the mission phase at any time.
//
// Also: orbit-insertion (capture) delta-v, mission elapsed time formatting,
// one-way light time, and cruise state-vector export.
enum class MissionPhase { Prelaunch, Launch, Cruise, Approach, Arrival, Descent, Landed, InOrbit, Lost };
const char* MissionPhaseName(MissionPhase p);

struct TimelineEvent {
    double day = 0.0;        // days since J2000
    std::string code;        // short tag: LAUNCH, TMI, TCM-1, APPR, MOI, EDL, ...
    std::string text;
    bool isLanding = false;  // part of the descent sequence (played back in the Landing view)
};

struct MissionTimelineData {
    bool valid = false;
    std::string landingBody;           // body the lander targets (planet or moon)
    std::vector<TimelineEvent> events; // sorted by day

    // Arrival
    double captureDvKms = 0.0;         // orbit-insertion burn; 0 for direct entry
    bool captureModeled = true;        // false: moon-of-another-planet legs aren't modeled
    std::string arrivalNote;

    // Landing
    bool hasLanding = false;
    double landingStartDay = 0.0;      // entry interface / powered-descent start
    LandingResult landing;
    std::string landingNote;           // why there is no landing, or what's skipped
};

class MissionTimeline {
public:
    // landingBody: the destination body itself (e.g. "Europa", "Mars").
    // arrivalIsParentSystem: true when the target is a moon of another planet.
    static MissionTimelineData Build(const MissionPlan& plan, const std::string& landingBody,
                                     bool arrivalIsParentSystem, const std::string& vehicleName);

    static MissionPhase PhaseAt(const MissionTimelineData& tl, const MissionPlan& plan, double day);

    // First event strictly after `day` (nullptr if none).
    static const TimelineEvent* NextEvent(const MissionTimelineData& tl, double day);

    // Capture burn from a hyperbolic approach with excess speed vInf into an
    // orbit with the given periapsis/apoapsis radii (km), applied at periapsis.
    static double CaptureDvKms(double gmKm3s2, double vInfKms, double periRadiusKm, double apoRadiusKm);

    // "T+012d 04:05:06" / "T-001d 00:00:12"
    static std::string FormatMET(double secondsFromLaunch);

    // One-way light time between two heliocentric positions in AU, seconds.
    static double LightTimeSec(const glm::dvec3& aAU, const glm::dvec3& bAU);

    // Heliocentric ecliptic J2000 state vectors along the transfer arc,
    // every stepDays (position AU, velocity km/s). False on I/O failure.
    static bool WriteCruiseCsv(const MissionPlan& plan, const std::string& path, double stepDays = 1.0);
};
