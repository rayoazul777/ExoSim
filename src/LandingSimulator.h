#pragma once
#include <string>
#include <vector>

// Entry, descent and landing (EDL) simulator.
//
// A planar (2-D, great-circle) point-mass simulation around a spherical,
// non-rotating body: inverse-square gravity, an exponential atmosphere
// (BodyConstants), drag, parachutes, jettison events, and a throttleable
// descent engine flown by a closed-loop guidance law. It is run ONCE when a
// mission is planned and the result is played back, so playback is exactly
// reproducible and independent of frame rate.
//
// Flight-software structure (the same split real GNC software uses):
//   - Physics ("truth") integrated with RK4 at a fixed 100 Hz.
//   - Guidance + control evaluated at a fixed 20 Hz, command held between
//     cycles (zero-order hold) - the way a flight computer's control cycle
//     is slower than the physical world.
//   - A sequencer that moves between phases on measured conditions (speed,
//     altitude, elapsed time), and logs each transition as an event.
//
// Powered-descent guidance: the ZEM/ZEV feedback law (Ebrahimi, Bahrami &
// Roshanian 2008; Guo, Hawkins & Wie 2013) - the optimal-control-derived
// family that also underlies Apollo's quadratic descent guidance:
//     a_cmd = 6 * ZEM / tgo^2  -  2 * ZEV / tgo
// where ZEM / ZEV are the zero-effort miss / velocity at time-to-go tgo.
//
// What is simplified (also stated in the UI):
//   - Ballistic (zero-lift) entry; real Mars landers fly a guided lifting
//     entry. No winds, no planet rotation, no terrain relief.
//   - Vehicle parameters are representative of a class (Perseverance-,
//     Venera-, Huygens-, Apollo LM-like), not exact flight values.
//   - Moon / airless landings start from a low circular orbit; the days of
//     orbit operations before a real descent are skipped.
enum class DescentPhase { Entry, Parachute, PoweredDescent, TerminalDescent, Landed, Crashed, Deorbit };

const char* DescentPhaseName(DescentPhase p);

struct LandingSample {
    double t = 0.0; // seconds since entry interface / descent start
    double altitudeM = 0.0;
    double downrangeM = 0.0; // surface distance travelled from the start point
    double angleRad = 0.0; // the same, as an angle around the body
    double speedMs = 0.0;
    double verticalMs = 0.0; // + up
    double horizontalMs = 0.0;
    double accelG = 0.0; // sensed (non-gravitational) acceleration, Earth g
    double dynPressurePa = 0.0;
    double heatFluxWcm2 = 0.0; // Sutton-Graves stagnation-point estimate
    double massKg = 0.0;
    double propellantKg = 0.0;
    double throttle = 0.0; // 0..1 of max thrust
    double thrustDirX = 0.0, thrustDirY = 0.0; // thrust direction in the local (horizontal, up) frame
    double rangeToTargetM = 0.0; // powered phases only
    DescentPhase phase = DescentPhase::Entry;
    bool chuteDeployed = false;
    bool heatShieldAttached = false;
};

struct LandingEvent {
    double t;
    std::string text;
};

enum class LimitStatus { Green, Yellow, Red, NotApplicable };
struct LimitCheck {
    std::string name, unit;
    double value = 0.0, yellow = 0.0, red = 0.0;
    bool higherIsWorse = true;
    LimitStatus status = LimitStatus::NotApplicable;
};

struct LandingConfig {
    std::string bodyName;
    double approachVinfKms = 0.0; // hyperbolic excess speed on arrival (direct entry)
};

struct LandingResult {
    bool valid = false;
    std::string bodyName;
    std::string profileName; // e.g. "Mars EDL (Perseverance-class)"
    std::string message; // why invalid, or notes
    double bodyRadiusM = 0.0;
    bool atmosphericEntry = false;
    bool powered = false;
    double entrySpeedMs = 0.0;
    double entryAltitudeM = 0.0;
    double flightPathAngleDeg = 0.0;
    double targetAngleRad = 0.0; // guidance target, as an angle from the start
    double durationS = 0.0;
    double touchdownSpeedMs = 0.0;
    double missDistanceM = 0.0;
    double propellantInitialKg = 0.0;
    double propellantUsedKg = 0.0;
    double peakG = 0.0, peakDynPressurePa = 0.0, peakHeatFluxWcm2 = 0.0;
    bool landed = false;
    std::vector<LandingSample> samples; // every 0.25 s
    std::vector<LandingEvent> events;
    std::vector<LimitCheck> limits;
};

class LandingSimulator {
public:
    static constexpr double kPhysicsHz = 100.0;
    static constexpr double kGuidanceHz = 20.0;
    static constexpr double kSampleInterval = 0.25;

    static LandingResult Run(const LandingConfig& cfg);

    // State at time t (linear interpolation between samples; clamps).
    static LandingSample At(const LandingResult& r, double t);

    // Telemetry export: one row per sample. Returns false on I/O failure.
    static bool WriteCsv(const LandingResult& r, const std::string& path);
};
