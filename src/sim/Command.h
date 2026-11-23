#pragma once
#include <cstdint>
#include <string>

// Every change a user can make to the SIMULATION goes through a Command
// (camera and display settings don't - they're presentation). This is what
// makes sessions recordable and replayable: SimulationState::apply() is the
// only way UI code mutates simulation state, and a recording is the list of
// commands with the clock tick at which each was applied.
enum class CommandType : int {
    SetSpeed = 0, // value: simulation days per real second
    SetPaused, // index: 0/1
    SetDay, // value: days since J2000
    SelectSystem, // index: system index (also clears the planet selection)
    SelectPlanet, // index: planet index within the current system, -1 = none
    SetSkyEpoch, // value: years from now
    ChooseTarget, // index: mission target
    ChooseVehicle, // index: launch vehicle
    PlanMission, // index: mission target to plan for, from the current date
    ClearMission,
    SetWarpToEvents, // index: 0/1
    ChooseModel, // index: 0 = Lambert (optimal 3-D), 1 = Hohmann (coplanar idealization)
    PlanTransfer, // index: target, value: launch day, value2: time of flight (days) - e.g. a porkchop pick
    PlanFlyby, // index: flyby sequence (GravityAssist::Sequences), value: earliest launch day
    Count
};

struct Command {
    CommandType type = CommandType::SetSpeed;
    double value = 0.0;
    int index = 0;
    double value2 = 0.0;

    static Command Speed(double daysPerSec) { return {CommandType::SetSpeed, daysPerSec, 0}; }
    static Command Paused(bool p) { return {CommandType::SetPaused, 0.0, p ? 1 : 0}; }
    static Command Day(double d) { return {CommandType::SetDay, d, 0}; }
    static Command System(int i) { return {CommandType::SelectSystem, 0.0, i}; }
    static Command Planet(int i) { return {CommandType::SelectPlanet, 0.0, i}; }
    static Command SkyEpoch(double years) { return {CommandType::SetSkyEpoch, years, 0}; }
    static Command Target(int i) { return {CommandType::ChooseTarget, 0.0, i}; }
    static Command Vehicle(int i) { return {CommandType::ChooseVehicle, 0.0, i}; }
    static Command Plan(int target) { return {CommandType::PlanMission, 0.0, target}; }
    static Command Clear() { return {CommandType::ClearMission, 0.0, 0}; }
    static Command WarpToEvents(bool on) { return {CommandType::SetWarpToEvents, 0.0, on ? 1 : 0}; }
    static Command Model(int m) { return {CommandType::ChooseModel, 0.0, m}; }
    static Command Transfer(int target, double launchDay, double tofDays) {
        return {CommandType::PlanTransfer, launchDay, target, tofDays};
    }
    static Command Flyby(int sequence, double fromDay) { return {CommandType::PlanFlyby, fromDay, sequence}; }
};

const char* CommandName(CommandType t);
bool CommandFromName(const std::string& name, CommandType& out);

struct LoggedCommand {
    std::uint64_t tick = 0; // SimClock tick count (relative to the recording start) when applied
    Command command;
};
