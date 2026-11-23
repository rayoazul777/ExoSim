#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "sim/Command.h"

class SimulationState;

// Recorded-input replay.
//
// A recording is a JSON-lines file:
//   {"type":"header","format":1,"exosim":"0.8.0","startDay":9800.5,"planets":3598,"systems":2472}
//   {"tick":0,"cmd":"SetSpeed","value":5,"index":0}
//   ...
//   {"type":"end","ticks":123456,"hash":"9f3c..."}
// Ticks are SimClock ticks since the recording started. Because the clock
// only moves in fixed ticks, the simulation is a pure function of
// (catalog, start day, commands-at-ticks); replaying the commands at the
// same ticks reproduces the final state hash bit-for-bit.
struct ReplayFile {
    int format = 1;
    std::string exosimVersion;
    double startDay = 0.0;
    std::uint64_t planets = 0, systems = 0; // catalog fingerprint
    std::vector<LoggedCommand> commands;
    bool hasEnd = false;
    std::uint64_t endTicks = 0;
    std::uint64_t endHash = 0;

    bool save(const std::string& path) const;
    static bool load(const std::string& path, ReplayFile& out, std::string& error);

    // Snapshot a live recording (header from the state; end = current ticks/hash).
    static ReplayFile FromRecording(const SimulationState& state, double recordingStartDay, const std::string& version);
};

// Drives a SimulationState from a ReplayFile, one fixed tick at a time.
class ReplayPlayer {
public:
    explicit ReplayPlayer(const ReplayFile& file) : m_file(file) {}

    // Windowed playback: consumes real time like the live clock would.
    void advance(SimulationState& state, double realDtSec);
    // Headless: runs to the end tick. Returns false if the log is inconsistent.
    bool runToEnd(SimulationState& state, std::string& error);

    bool finished(const SimulationState& state) const;
    double progress(const SimulationState& state) const;

private:
    // Applies due commands, then advances one tick. False when nothing more can happen.
    bool stepOnce(SimulationState& state);

    const ReplayFile& m_file;
    size_t m_next = 0;
    double m_accum = 0.0;
};
