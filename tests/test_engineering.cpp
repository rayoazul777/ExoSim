// ENG group: engineering properties of the simulation layer - recorded
// input replay, the decoupled SimulationState, formatting, the sky model,
// configuration and structured logging.
#include "Harness.h"

#include "Config.h"
#include "Log.h"
#include "sim/Catalog.h"
#include "sim/Formatting.h"
#include "sim/Replay.h"
#include "sim/SimulationState.h"
#include "sky/SkyModel.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

// A scripted "session": uneven frame times, commands between frames, pauses,
// time jumps, and warp through the launch and landing event stops.
void runScriptedSession(SimulationState& s, bool& sawLandingStop) {
    const double frames[] = {0.016, 0.017, 0.033, 0.0161, 0.25, 0.008, 0.016};
    auto frame = [&](int i) {
        auto r = s.advance(frames[i % 7]);
        if (r.landingStarted) sawLandingStop = true;
    };
    for (int i = 0; i < 20; ++i) frame(i);
    s.apply(Command::Speed(20.0));
    s.apply(Command::System(0));
    s.apply(Command::Planet(3));
    for (int i = 0; i < 30; ++i) frame(i);
    int mars = s.findTarget("Mars");
    s.apply(Command::Target(mars));
    s.apply(Command::Vehicle(1));
    s.apply(Command::Plan(mars));
    s.apply(Command::Paused(true));
    for (int i = 0; i < 10; ++i) frame(i); // paused: no ticks counted
    s.apply(Command::Day(s.mission().launchDay - 3.0));
    s.apply(Command::Speed(2.0));
    s.apply(Command::Paused(false));
    for (int i = 0; i < 200; ++i) frame(i); // hits the launch stop, drops to 60x
    s.apply(Command::Speed(200.0));
    s.apply(Command::SkyEpoch(-5000.0));
    for (int i = 0; i < 400; ++i) frame(i); // cruise...
    s.apply(Command::Day(s.timeline().landingStartDay - 0.5));
    s.apply(Command::Speed(1.0));
    for (int i = 0; i < 400; ++i) frame(i); // ...to the landing stop, then real time
    s.apply(Command::Speed(0.001));
    for (int i = 0; i < 50; ++i) frame(i);
}

void testReplay() {
    Catalog cat = Catalog::FromPlanets(Dataset::DemoSolarSystem());
    const double start = 9800.25;

    // 1) Record from startup.
    SimulationState live(cat, start);
    live.startRecording();
    bool landingStop = false;
    runScriptedSession(live, landingStop);
    check("ENG-1", "Scripted session reaches the landing event stop", 1.0, landingStop ? 1.0 : 0.0, 0.0, "bool",
          "Expected behaviour (exercises warp-to-event)");

    ReplayFile file = ReplayFile::FromRecording(live, start, "test");
    std::string path = (std::filesystem::temp_directory_path() / "exosim_replay_test.jsonl").string();
    bool saved = file.save(path);
    ReplayFile loaded;
    std::string err;
    bool ok = saved && ReplayFile::load(path, loaded, err);
    check("ENG-2", "Recording round-trips through its JSON-lines file", 1.0,
          ok && loaded.commands.size() == file.commands.size() && loaded.endHash == file.endHash ? 1.0 : 0.0, 0.0, "bool",
          "Format spec (Replay.h)");

    SimulationState replayed(cat, loaded.startDay);
    ReplayPlayer player(loaded);
    bool ran = player.runToEnd(replayed, err);
    check("ENG-3", "Replay reproduces the final state hash", 1.0, ran && replayed.hash() == live.hash() ? 1.0 : 0.0, 0.0, "bool",
          "ENG-4 (deterministic replay)");
    double dayDiff = replayed.day() - live.day();
    check("ENG-4", "Replay reproduces the final simulation time bit-for-bit", 0.0, std::fabs(dayDiff), 0.0, "days", "ENG-4");
    check("ENG-5", "Replay ends on the recorded tick", static_cast<double>(live.recordingTicks()),
          static_cast<double>(replayed.clock().ticks), 0.0, "ticks", "ENG-4");

    // 2) Recording started mid-session (mission active, paused) replays from a fresh state.
    SimulationState mid(cat, start);
    bool unused = false;
    runScriptedSession(mid, unused);
    mid.apply(Command::Paused(true));
    const double midStart = mid.day();
    mid.startRecording();
    mid.apply(Command::Paused(false));
    mid.apply(Command::Speed(0.5));
    for (int i = 0; i < 300; ++i) mid.advance(0.0213);
    ReplayFile midFile = ReplayFile::FromRecording(mid, midStart, "test");
    SimulationState midReplay(cat, midStart);
    ReplayPlayer midPlayer(midFile);
    bool midRan = midPlayer.runToEnd(midReplay, err);
    check("ENG-6", "Mid-session recording (active mission) replays to the same hash", 1.0,
          midRan && midReplay.hash() == mid.hash() && midReplay.missionActive() ? 1.0 : 0.0, 0.0, "bool", "ENG-4");

    // 3) A tampered recording is detected.
    if (!midFile.commands.empty()) midFile.commands.back().command.value *= 1.01; // e.g. a different speed
    SimulationState tampered(cat, midStart);
    ReplayPlayer tamperedPlayer(midFile);
    tamperedPlayer.runToEnd(tampered, err);
    check("ENG-7", "A changed command changes the replayed hash", 1.0, tampered.hash() != mid.hash() ? 1.0 : 0.0, 0.0, "bool",
          "Expected behaviour (hash covers the state)");

    std::ofstream bad(path);
    bad << "{\"type\":\"header\",\"startDay\":1}\n{\"tick\":5,\"cmd\":\"Teleport\"}\n";
    bad.close();
    check("ENG-8", "Unknown command in a recording is rejected with an error", 0.0,
          ReplayFile::load(path, loaded, err) ? 1.0 : 0.0, 0.0, "bool", "Failure handling");
    std::filesystem::remove(path);

    // 4) Only Commands mutate the state: two states fed the same commands and
    //    frame times agree, whatever happened in between with const access.
    SimulationState a(cat, start), b(cat, start);
    bool la = false, lb = false;
    runScriptedSession(a, la);
    (void)b.moonsFor(0);
    (void)b.craftPosition(9000.0);
    (void)b.hash(); // read-only calls
    runScriptedSession(b, lb);
    check("ENG-9", "Read-only access does not perturb the simulation", 1.0, a.hash() == b.hash() ? 1.0 : 0.0, 0.0, "bool",
          "Design (SimulationState API)");
}

void testFormattingAndSky() {
    check("ENG-10", "UTC formatting at J2000 epoch (got " + Format::Utc(0.0) + ")", 1.0,
          Format::Utc(0.0) == "2000-01-01 12:00:00" ? 1.0 : 0.0, 0.0, "bool", "J2000 = 2000-01-01 12:00 TT (approx. UTC)");
    check("ENG-11", "Duration picks units: 7.9 days", 1.0, Format::Duration(7.9 * 86400.0) == "7.9 days" ? 1.0 : 0.0, 0.0, "bool",
          "Format spec");
    check("ENG-12", "Warp label for real time", 1.0, Format::Warp(1.0) == "1.0x real time" ? 1.0 : 0.0, 0.0, "bool",
          "Format spec");
    check("ENG-13", "Case-insensitive search", 1.0, Format::ContainsCaseInsensitive("Kepler-20", "KEPLER") ? 1.0 : 0.0, 0.0,
          "bool", "Expected behaviour");

    // The Sun seen from 10 pc has apparent magnitude = absolute magnitude (4.83).
    StarSystem far;
    far.distPc = 10.0;
    far.hostRaDeg = 100.0;
    far.hostDecDeg = 20.0;
    Sky::SkyView v = Sky::Compute({}, far, 9800.0, 0.0);
    check("ENG-14", "Sun's apparent magnitude from 10 pc", 4.83, v.sunApparentVmag, 1e-6, "mag",
          "Definition of absolute magnitude (float direction vector)");
    // A 1 pc star seen from 2 pc straight behind it dims by 5 log10(2) mag.
    StarCatalogEntry st;
    st.raDeg = 100.0;
    st.decDeg = 20.0;
    st.vmag = 1.0;
    st.bv = 0.6;
    st.parallaxMas = 1000.0;
    far.distPc = 3.0; // observer 3 pc out along the same line: star now 2 pc away
    Sky::SkyView v2 = Sky::Compute({st}, far, 9800.0, 0.0);
    // Brightness is encoded in point size; invert VmagToPointSize for the check.
    double expectedSize = Sky::VmagToPointSize(1.0 + 5.0 * std::log10(2.0));
    check("ENG-15", "Parallax re-projection dims a star by the inverse-square law", expectedSize,
          v2.sizes.empty() ? -1.0 : v2.sizes[0], 1e-6, "px", "m = M + 5 log10(d/10pc)");
}

void testConfigAndLog() {
    Config c;
    std::string err;
    bool ok = Config::Parse("{}", c, err);
    check("ENG-16", "Empty config keeps every default", 1.0,
          ok && c.windowWidth == 1280 && c.speedDaysPerSec == 5.0 && c.warpToEvents && c.dataDir == "data" ? 1.0 : 0.0, 0.0,
          "bool", "Config.h defaults = built-in behaviour");
    Config c2;
    ok = Config::Parse(R"({"window":{"width":1920,"vsync":"yes"},"simulation":{"speedDaysPerSec":999},"colour":1})", c2, err);
    check("ENG-17", "Config: valid key applied, bad type/range/unknown key each warned", 1.0,
          ok && c2.windowWidth == 1920 && c2.vsync && c2.speedDaysPerSec == 5.0 && c2.warnings.size() == 3 ? 1.0 : 0.0, 0.0,
          "bool", "Config.h");
    Config c3;
    check("ENG-18", "Malformed config is rejected and defaults kept", 0.0, Config::Parse("{ not json", c3, err) ? 1.0 : 0.0, 0.0,
          "bool", "Failure handling");

    std::string line = Log::FormatJson(Log::Level::Warn, "replay", "say \"hi\"\n", {{"ticks", 42}, {"path", "a\\b"}},
                                       "2026-01-01T00:00:00.000Z");
    const char* expected =
        R"({"t":"2026-01-01T00:00:00.000Z","level":"warn","component":"replay","msg":"say \"hi\"\n","ticks":42,"path":"a\\b"})";
    check("ENG-19", "Structured log line is valid, escaped JSON", 1.0, line == expected ? 1.0 : 0.0, 0.0, "bool",
          "JSON (RFC 8259) escaping");
}

} // namespace

void testEngineering() {
    testReplay();
    testFormattingAndSky();
    testConfigAndLog();
}
