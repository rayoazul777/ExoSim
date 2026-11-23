#include "sim/Replay.h"
#include "sim/SimulationState.h"
#include "SimClock.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
const char* kNames[] = {"SetSpeed",        "SetPaused",    "SetDay",        "SelectSystem", "SelectPlanet",
                        "SetSkyEpoch",     "ChooseTarget", "ChooseVehicle", "PlanMission",  "ClearMission",
                        "SetWarpToEvents", "ChooseModel",  "PlanTransfer",  "PlanFlyby"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(CommandType::Count), "command names");

std::string hex64(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v));
    return b;
}
} // namespace

const char* CommandName(CommandType t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < static_cast<int>(CommandType::Count)) ? kNames[i] : "?";
}

bool CommandFromName(const std::string& name, CommandType& out) {
    for (int i = 0; i < static_cast<int>(CommandType::Count); ++i)
        if (name == kNames[i]) {
            out = static_cast<CommandType>(i);
            return true;
        }
    return false;
}

bool ReplayFile::save(const std::string& path) const {
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path);
    if (!out) return false;
    nlohmann::json h = {{"type", "header"},     {"format", format},   {"exosim", exosimVersion},
                        {"startDay", startDay}, {"planets", planets}, {"systems", systems}};
    out << h.dump() << '\n';
    for (const auto& lc : commands) {
        nlohmann::json j = {
            {"tick", lc.tick}, {"cmd", CommandName(lc.command.type)}, {"value", lc.command.value}, {"index", lc.command.index}};
        if (lc.command.value2 != 0.0) j["value2"] = lc.command.value2;
        out << j.dump() << '\n';
    }
    if (hasEnd) out << nlohmann::json{{"type", "end"}, {"ticks", endTicks}, {"hash", hex64(endHash)}}.dump() << '\n';
    return static_cast<bool>(out);
}

bool ReplayFile::load(const std::string& path, ReplayFile& f, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    f = ReplayFile{};
    std::string line;
    int lineNo = 0;
    bool sawHeader = false;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty()) continue;
        nlohmann::json j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) {
            error = "line " + std::to_string(lineNo) + ": not JSON";
            return false;
        }
        std::string type = j.value("type", std::string());
        if (type == "header") {
            f.format = j.value("format", 1);
            f.exosimVersion = j.value("exosim", std::string());
            f.startDay = j.value("startDay", 0.0);
            f.planets = j.value("planets", std::uint64_t{0});
            f.systems = j.value("systems", std::uint64_t{0});
            sawHeader = true;
        } else if (type == "end") {
            f.hasEnd = true;
            f.endTicks = j.value("ticks", std::uint64_t{0});
            f.endHash = std::stoull(j.value("hash", std::string("0")), nullptr, 16);
        } else {
            LoggedCommand lc;
            CommandType t;
            if (!j.contains("tick") || !CommandFromName(j.value("cmd", std::string()), t)) {
                error = "line " + std::to_string(lineNo) + ": unknown command";
                return false;
            }
            lc.tick = j.value("tick", std::uint64_t{0});
            lc.command = {t, j.value("value", 0.0), j.value("index", 0), j.value("value2", 0.0)};
            if (!f.commands.empty() && lc.tick < f.commands.back().tick) {
                error = "line " + std::to_string(lineNo) + ": ticks go backwards";
                return false;
            }
            f.commands.push_back(lc);
        }
    }
    if (!sawHeader) {
        error = "missing header";
        return false;
    }
    return true;
}

ReplayFile ReplayFile::FromRecording(const SimulationState& s, double recordingStartDay, const std::string& version) {
    ReplayFile f;
    f.exosimVersion = version;
    f.startDay = recordingStartDay;
    f.planets = s.planets().size();
    f.systems = s.systems().size();
    f.commands = s.recorded();
    f.hasEnd = true;
    f.endTicks = s.recordingTicks();
    f.endHash = s.hash();
    return f;
}

bool ReplayPlayer::stepOnce(SimulationState& state) {
    const auto& cmds = m_file.commands;
    const std::uint64_t now = state.clock().ticks;
    while (m_next < cmds.size() && cmds[m_next].tick <= now) state.apply(cmds[m_next++].command);
    const bool more = m_next < cmds.size() || now < m_file.endTicks;
    if (!more) return false;
    if (state.paused() && (m_next >= cmds.size() || cmds[m_next].tick > now)) return false; // nothing can unpause it
    state.advance(SimClock::kTickRealSec);
    return true;
}

void ReplayPlayer::advance(SimulationState& state, double realDtSec) {
    m_accum += realDtSec;
    int budget = SimClock::kMaxTicksPerFrame;
    while (m_accum >= SimClock::kTickRealSec && budget-- > 0) {
        m_accum -= SimClock::kTickRealSec;
        if (!stepOnce(state)) {
            m_accum = 0.0;
            return;
        }
    }
    if (budget < 0) m_accum = 0.0;
}

bool ReplayPlayer::runToEnd(SimulationState& state, std::string& error) {
    const std::uint64_t guard = m_file.endTicks + m_file.commands.size() + 16;
    std::uint64_t iterations = 0;
    while (stepOnce(state)) {
        if (++iterations > guard * 4) {
            error = "replay did not terminate";
            return false;
        }
    }
    // Apply anything stamped at the final tick.
    const auto& cmds = m_file.commands;
    while (m_next < cmds.size() && cmds[m_next].tick <= state.clock().ticks) state.apply(cmds[m_next++].command);
    if (m_next < cmds.size()) {
        error = "commands left after the clock stopped (paused at the end?)";
        return false;
    }
    return true;
}

bool ReplayPlayer::finished(const SimulationState& state) const {
    return m_next >= m_file.commands.size() && state.clock().ticks >= m_file.endTicks;
}

double ReplayPlayer::progress(const SimulationState& state) const {
    return m_file.endTicks ? std::min(1.0, static_cast<double>(state.clock().ticks) / m_file.endTicks) : 1.0;
}
