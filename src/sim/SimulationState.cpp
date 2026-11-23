#include "sim/SimulationState.h"
#include "PlanetClassifier.h"
#include "sim/Formatting.h"
#include "sim/SpeculativeMoons.h"
#include "GravityAssist.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
const char* kVehicles[SimulationState::kVehicleCount] = {"Falcon Heavy (SpaceX)", "SLS Block 1 (Artemis)"};
const std::vector<MoonRecord> kNoMoons;

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    template <typename T> void add(const T& v) { bytes(&v, sizeof(T)); }
};
} // namespace

const char* SimulationState::VehicleName(int i) { return (i >= 0 && i < kVehicleCount) ? kVehicles[i] : "?"; }

SimulationState::SimulationState(Catalog catalog, double startDay) : m_catalog(std::move(catalog)) {
    // Day 0 = J2000 (2000-01-01 12:00). Sol's planets use real J2000-epoch
    // elements, so today's date puts them where they really are.
    m_clock.timeDays = startDay;

    // Mission targets: every Sol planet (except Earth) and every real moon.
    for (const auto& sys : m_catalog.systems) {
        if (!sys.isHome) continue;
        for (size_t pIdx : sys.planetIndices) {
            const PlanetRecord& p = m_catalog.planets[pIdx];
            if (p.name == "Earth") m_earthIdx = pIdx;
            if (p.name != "Earth") m_targets.push_back({p.name, pIdx, false, p.name, false});
            for (const auto& m : p.moons) {
                bool earthMoon = p.name == "Earth";
                m_targets.push_back({m.name + " (moon of " + p.name + ")", pIdx, earthMoon, m.name, !earthMoon});
            }
        }
    }
}

SimulationState::StepResult SimulationState::advance(double realDtSec) {
    StepResult r;
    m_clock.warp = static_cast<double>(m_speedDaysPerSec) * 86400.0;
    double stopDay = std::numeric_limits<double>::infinity();
    double stopWarp = 1.0;
    bool stopIsLanding = false;
    if (m_missionActive && m_warpToEvents && !m_clock.paused) {
        if (m_clock.timeDays < m_mission.launchDay) {
            stopDay = m_mission.launchDay;
            stopWarp = 60.0; // ascent and the injection burn are watchable at 60x
        } else if (m_timeline.hasLanding && m_clock.timeDays < m_timeline.landingStartDay) {
            stopDay = m_timeline.landingStartDay;
            stopIsLanding = true;
        }
    }
    SimClock::Advance adv = m_clock.advance(realDtSec, stopDay);
    r.ticks = adv.ticks;
    if (adv.hitStop) {
        m_clock.warp = stopWarp;
        r.hitStop = true;
        r.landingStarted = stopIsLanding;
    }
    m_speedDaysPerSec = static_cast<float>(m_clock.warp / 86400.0);
    return r;
}

void SimulationState::apply(const Command& c) {
    if (m_recording) m_log.push_back({m_clock.ticks - m_recordTickBase, c});
    applyNoLog(c);
}

void SimulationState::applyNoLog(const Command& c) {
    const int nSys = static_cast<int>(m_catalog.systems.size());
    switch (c.type) {
        case CommandType::SetSpeed: m_speedDaysPerSec = static_cast<float>(c.value); break;
        case CommandType::SetPaused: m_clock.paused = c.index != 0; break;
        case CommandType::SetDay: m_clock.timeDays = c.value; break;
        case CommandType::SelectSystem:
            if (c.index >= 0 && c.index < nSys) {
                m_systemIndex = c.index;
                m_selectedPlanetLocal = -1;
            }
            break;
        case CommandType::SelectPlanet: {
            int n = static_cast<int>(currentSystem().planetIndices.size());
            m_selectedPlanetLocal = (c.index >= 0 && c.index < n) ? c.index : -1;
            break;
        }
        case CommandType::SetSkyEpoch: m_skyEpochOffsetYears = static_cast<float>(c.value); break;
        case CommandType::ChooseTarget:
            if (c.index >= 0 && c.index < static_cast<int>(m_targets.size())) m_targetChoice = c.index;
            break;
        case CommandType::ChooseVehicle:
            if (c.index >= 0 && c.index < kVehicleCount) m_vehicleChoice = c.index;
            break;
        case CommandType::PlanMission:
            if (c.index >= 0 && c.index < static_cast<int>(m_targets.size())) planMission(c.index);
            break;
        case CommandType::ClearMission: m_missionActive = false; break;
        case CommandType::SetWarpToEvents: m_warpToEvents = c.index != 0; break;
        case CommandType::ChooseModel: m_model = (c.index == 1) ? 1 : 0; break;
        case CommandType::PlanTransfer:
            if (c.index >= 0 && c.index < static_cast<int>(m_targets.size())) planTransfer(c.index, c.value, c.value2);
            break;
        case CommandType::PlanFlyby: planFlyby(c.index, c.value); break;
        case CommandType::Count: break;
    }
}

void SimulationState::finishPlan(const MissionTarget& mt) {
    m_missionActive = m_mission.valid;
    if (m_missionActive) {
        m_activeTarget = mt;
        m_missionPlannedDay = m_clock.timeDays;
        m_timeline = MissionTimeline::Build(m_mission, mt.isEarthMoon ? std::string("Moon") : mt.body, mt.isMoon,
                                            kVehicles[m_vehicleChoice]);
        if (m_flyby) MissionTimeline::AddFlybyEvents(m_timeline, *m_flyby);
    }
}

void SimulationState::planMission(int choice) {
    const MissionTarget& mt = m_targets[choice];
    const auto& planets = m_catalog.planets;
    m_flyby.reset();
    if (mt.isEarthMoon) {
        m_mission = MissionPlanner::PlanEarthMoon(m_clock.timeDays);
    } else if (m_model == 1) {
        m_mission = MissionPlanner::PlanHohmann(planets[m_earthIdx].elements, planets[mt.planetIdx].elements, m_clock.timeDays,
                                                planets[mt.planetIdx].name);
        m_mission.targetName = mt.label;
    } else {
        m_mission = MissionPlanner::PlanOptimal(planets[m_earthIdx].elements, planets[mt.planetIdx].elements, m_clock.timeDays,
                                                planets[mt.planetIdx].name);
        m_mission.targetName = mt.label;
    }
    m_planCommand = Command::Plan(choice);
    m_planModel = m_model;
    finishPlan(mt);
}

void SimulationState::planTransfer(int choice, double launchDay, double tofDays) {
    const MissionTarget& mt = m_targets[choice];
    if (mt.isEarthMoon || tofDays <= 0.0) return;
    const auto& planets = m_catalog.planets;
    m_flyby.reset();
    m_mission = MissionPlanner::PlanLambert(planets[m_earthIdx].elements, planets[mt.planetIdx].elements, launchDay, tofDays,
                                            planets[mt.planetIdx].name);
    m_mission.targetName = mt.label;
    m_planCommand = Command::Transfer(choice, launchDay, tofDays);
    finishPlan(mt);
}

void SimulationState::planFlyby(int sequence, double fromDay) {
    const auto& seqs = GravityAssist::Sequences();
    if (sequence < 0 || sequence >= static_cast<int>(seqs.size())) return;
    const GravityAssist::Sequence& seq = seqs[sequence];
    std::vector<OrbitalElements> bodies;
    for (const auto& name : seq.bodies) {
        int idx = -1;
        for (size_t p : currentSystem().planetIndices)
            if (m_catalog.planets[p].name == name) idx = static_cast<int>(p);
        if (idx < 0)
            for (size_t p = 0; p < m_catalog.planets.size(); ++p)
                if (m_catalog.planets[p].name == name && m_catalog.planets[p].hostStar == "Sun") idx = static_cast<int>(p);
        if (idx < 0) return;
        bodies.push_back(m_catalog.planets[idx].elements);
    }
    FlybyPlan fp = GravityAssist::Optimize(seq, bodies, fromDay);
    if (!fp.valid) return;
    int targetChoice = findTarget(seq.bodies.back());
    if (targetChoice < 0) return;
    m_flyby = std::make_shared<FlybyPlan>(fp);
    m_mission = fp.AsMissionPlan(m_targets[targetChoice].label);
    m_planCommand = Command::Flyby(sequence, fromDay);
    finishPlan(m_targets[targetChoice]);
}

const FlybyPlan* SimulationState::flyby() const { return m_missionActive ? m_flyby.get() : nullptr; }

int SimulationState::findTarget(const std::string& body) const {
    for (int i = 0; i < static_cast<int>(m_targets.size()); ++i)
        if (m_targets[i].body == body) return i;
    return -1;
}

const std::vector<MoonRecord>& SimulationState::moonsFor(size_t pIdx) const {
    const PlanetRecord& p = m_catalog.planets[pIdx];
    if (!p.moons.empty()) return p.moons;
    // Never invent moons in our own Solar System: Sol's moons are known.
    if (p.hostStar == "Sun") return kNoMoons;
    auto it = m_moonCache.find(pIdx);
    if (it != m_moonCache.end()) return it->second;
    PlanetClassification cls = PlanetClassifier::Classify(p);
    return m_moonCache.emplace(pIdx, GenerateSpeculativeMoons(cls.type, Format::HashSeed(p.name))).first->second;
}

glm::dvec3 SimulationState::craftPosition(double day) const {
    const MissionPlan& m = m_mission;
    glm::dvec3 earthPos = m_catalog.orbits[m_earthIdx].positionAtTime(day);
    if (day < m.launchDay) return earthPos;
    if (day >= m.arrivalDay) return m_catalog.orbits[m_activeTarget.planetIdx].positionAtTime(day);
    return m.positionAt(day);
}

void SimulationState::startRecording() {
    m_log.clear();
    m_recordTickBase = m_clock.ticks;
    m_recording = true;
    // Recreate the current state from a fresh SimulationState(catalog, day):
    // settings first, then the mission re-planned on the day it was planned
    // (planning is deterministic), then back to the current date.
    const double now = m_clock.timeDays;
    const int target = m_targetChoice;
    apply(Command::Speed(m_speedDaysPerSec));
    apply(Command::Paused(m_clock.paused));
    apply(Command::WarpToEvents(m_warpToEvents));
    apply(Command::Vehicle(m_vehicleChoice));
    int planet = m_selectedPlanetLocal;
    apply(Command::System(m_systemIndex));
    apply(Command::Planet(planet));
    apply(Command::SkyEpoch(m_skyEpochOffsetYears));
    const int model = m_model;
    if (m_missionActive) {
        // Re-create the mission with the command that made it, on the day it
        // was planned, under the model then in effect.
        apply(Command::Model(m_planModel));
        apply(Command::Day(m_missionPlannedDay));
        apply(m_planCommand);
    }
    apply(Command::Model(model));
    apply(Command::Target(target));
    apply(Command::Day(now));
}

std::uint64_t SimulationState::hash() const {
    Fnv f;
    f.add(m_clock.timeDays);
    f.add(m_clock.warp);
    f.add(m_clock.paused);
    f.add(m_speedDaysPerSec);
    f.add(m_systemIndex);
    f.add(m_selectedPlanetLocal);
    f.add(m_skyEpochOffsetYears);
    f.add(m_targetChoice);
    f.add(m_vehicleChoice);
    f.add(m_warpToEvents);
    f.add(m_model);
    f.add(m_missionActive);
    if (m_missionActive) {
        f.add(m_mission.launchDay);
        f.add(m_mission.arrivalDay);
        f.add(m_mission.dvFromLeoKms);
        f.add(m_missionPlannedDay);
        f.bytes(m_activeTarget.body.data(), m_activeTarget.body.size());
        size_t n = m_timeline.events.size();
        f.add(n);
        if (m_timeline.hasLanding) f.add(m_timeline.landing.durationS);
    }
    return f.h;
}
