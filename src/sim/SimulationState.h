#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "MissionPlanner.h"
#include "MissionTimeline.h"
#include "SimClock.h"
#include "sim/Catalog.h"
#include "sim/Command.h"

struct FlybyPlan;

// A mission destination: a Sol planet (other than Earth) or a real moon.
// body: what a lander would touch down on (the planet itself, or the moon).
struct MissionTarget {
    std::string label;
    size_t planetIdx = 0;
    bool isEarthMoon = false;
    std::string body;
    bool isMoon = false;
};

// All simulation state, with no OpenGL or ImGui dependency: the catalog,
// the clock, what's selected, and the active mission. UI code reads it
// through const accessors and changes it only with apply(Command) - which
// is what makes a session recordable and exactly replayable (see Replay.h).
class SimulationState {
public:
    static constexpr int kVehicleCount = 2;
    static const char* VehicleName(int i);

    SimulationState(Catalog catalog, double startDay);

    // ---- Time ----
    struct StepResult {
        int ticks = 0;
        bool hitStop = false;
        bool landingStarted = false; // the clock stopped at the start of the landing sequence
    };
    // Advance by real elapsed seconds, in fixed ticks. With "warp to events"
    // on, stops exactly at launch (dropping to 60x) and at the start of the
    // landing sequence (dropping to real time).
    StepResult advance(double realDtSec);

    double day() const { return m_clock.timeDays; }
    const SimClock& clock() const { return m_clock; }
    bool paused() const { return m_clock.paused; }
    float speedDaysPerSec() const { return m_speedDaysPerSec; }

    // ---- The one mutator ----
    void apply(const Command& c);

    // ---- Catalog & selection ----
    const Catalog& catalog() const { return m_catalog; }
    const std::vector<PlanetRecord>& planets() const { return m_catalog.planets; }
    const std::vector<KeplerOrbit>& orbits() const { return m_catalog.orbits; }
    const std::vector<StarSystem>& systems() const { return m_catalog.systems; }
    int systemIndex() const { return m_systemIndex; }
    const StarSystem& currentSystem() const { return m_catalog.systems[m_systemIndex]; }
    int selectedPlanetLocal() const { return m_selectedPlanetLocal; }
    float skyEpochOffsetYears() const { return m_skyEpochOffsetYears; }

    // Real moons for Sol bodies; deterministic illustrative candidates for
    // exoplanets (cached, so they stay stable for the session); none for
    // Sol bodies without moons (Mercury, Venus, ...).
    const std::vector<MoonRecord>& moonsFor(size_t planetIdx) const;

    // ---- Mission ----
    const std::vector<MissionTarget>& missionTargets() const { return m_targets; }
    int targetChoice() const { return m_targetChoice; }
    int vehicleChoice() const { return m_vehicleChoice; }
    bool missionActive() const { return m_missionActive; }
    const MissionPlan& mission() const { return m_mission; }
    const MissionTarget& activeTarget() const { return m_activeTarget; }
    const MissionTimelineData& timeline() const { return m_timeline; }
    size_t earthIndex() const { return m_earthIdx; }
    bool warpToEvents() const { return m_warpToEvents; }
    double missionPlannedDay() const { return m_missionPlannedDay; }
    int findTarget(const std::string& body) const;
    int trajectoryModel() const { return m_model; } // 0 Lambert, 1 Hohmann
    // Flyby chain of the active mission (empty for direct transfers).
    const FlybyPlan* flyby() const;

    // Spacecraft heliocentric ecliptic position (AU): on the transfer arc
    // in flight; at Earth before launch; at the target after arrival.
    glm::dvec3 craftPosition(double day) const;

    // ---- Recording ----
    // Starts a recording whose first entries recreate the current state, so
    // replay can begin from a fresh SimulationState.
    void startRecording();
    void stopRecording() { m_recording = false; }
    bool recording() const { return m_recording; }
    const std::vector<LoggedCommand>& recorded() const { return m_log; }
    std::uint64_t recordingTicks() const { return m_clock.ticks - m_recordTickBase; }

    // Fingerprint of all simulation state (excluding the absolute tick
    // count), for replay verification.
    std::uint64_t hash() const;

private:
    void applyNoLog(const Command& c);
    void planMission(int choice);
    void planTransfer(int choice, double launchDay, double tofDays);
    void planFlyby(int sequence, double fromDay);
    void finishPlan(const MissionTarget& mt);

    Catalog m_catalog;
    SimClock m_clock;
    float m_speedDaysPerSec = 5.0f;
    int m_systemIndex = 0;
    int m_selectedPlanetLocal = -1;
    float m_skyEpochOffsetYears = 0.0f;

    std::vector<MissionTarget> m_targets;
    int m_targetChoice = 0;
    int m_vehicleChoice = 0;
    bool m_missionActive = false;
    MissionPlan m_mission;
    MissionTarget m_activeTarget;
    MissionTimelineData m_timeline;
    double m_missionPlannedDay = 0.0;
    Command m_planCommand; // the command that produced the active mission (for recordings)
    int m_model = 0;
    int m_planModel = 0; // model in effect when the active mission was planned
    std::shared_ptr<FlybyPlan> m_flyby;
    size_t m_earthIdx = 0;
    bool m_warpToEvents = true;

    mutable std::unordered_map<size_t, std::vector<MoonRecord>> m_moonCache;

    bool m_recording = false;
    std::uint64_t m_recordTickBase = 0;
    std::vector<LoggedCommand> m_log;
};
