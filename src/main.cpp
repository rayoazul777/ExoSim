// ExoSim - retro-futurist orbital simulation and mission-analysis tool.
//
// Layout (see docs/ARCHITECTURE.md):
//   sim/    SimulationState (clock, selection, mission), Command, Replay,
//           Catalog - no OpenGL or UI; unit-tested.
//   sky/    SkyModel - the visible sky from any star system.
//   scene/  System, Detail and Landing scenes (OpenGL).
//   ui/     ImGui panels - read the state, change it only via Commands.
//   app/    Window, input, main loop, self-test.
//   (root)  Core physics and data: KeplerOrbit, MissionPlanner, Lambert,
//           LandingSimulator, Dataset, ...
//
// This file only parses the command line, sets up logging/config, and
// dispatches.

#include "Config.h"
#include "Log.h"
#include "Platform.h"
#include "app/App.h"
#include "app/SelfTest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#ifndef EXOSIM_VERSION
#define EXOSIM_VERSION "dev"
#endif

namespace {
void PrintHelp() {
    std::printf("ExoSim %s\n"
                "  --version                 print version and exit\n"
                "  --selftest                verify the install without opening a window\n"
                "  --config <file>           settings file (default: exosim.json next to the exe)\n"
                "  --record <file.jsonl>     record this session's inputs; saved on exit\n"
                "  --replay <file.jsonl>     play a recorded session back in the window\n"
                "  --verify-replay <file>    replay headlessly and check the final state hash\n"
                "  --scenario <body> [--edl-time <s> | --cruise <0..1>]\n"
                "                            open a planned mission at a given moment\n"
                "  --sim-day <days>          start date, days since J2000 (default: today)\n"
                "  --system <host>           start in this star system\n"
                "  --inspect <planet> [--moon <i>]   open the Detail view\n"
                "  --porkchop                open the porkchop plot for the --scenario target\n"
                "  --flyby <i>               plan gravity-assist chain i (0 = Earth-Jupiter-Saturn, ...)\n"
                "  --log-scale               start with log-scale distances\n"
                "  --galaxy [--galaxy-3d] [--galaxy-distance <pc>]\n"
                "                            open the Milky Way map (zoom 1 pc - 80 kpc)\n"
                "  --screenshot <file.ppm>   render 5 deterministic frames, save the last, exit\n",
                EXOSIM_VERSION);
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path launchDir = std::filesystem::current_path();
    auto fromLaunchDir = [&](const char* p) { return (launchDir / p).string(); };

    AppOptions opt;
    opt.version = EXOSIM_VERSION;
    std::string configPath, verifyReplay;
    bool selfTest = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0) {
            std::printf("ExoSim %s\n", EXOSIM_VERSION);
            return 0;
        }
        if (std::strcmp(a, "--help") == 0) {
            PrintHelp();
            return 0;
        }
        if (std::strcmp(a, "--selftest") == 0) {
            selfTest = true;
            continue;
        }
        if (std::strcmp(a, "--porkchop") == 0) {
            opt.openPorkchop = true;
            continue;
        }
        if (std::strcmp(a, "--log-scale") == 0) {
            opt.logScale = true;
            continue;
        }
        if (std::strcmp(a, "--galaxy") == 0) {
            opt.galaxy = true;
            continue;
        }
        if (std::strcmp(a, "--galaxy-3d") == 0) {
            opt.galaxy = opt.galaxy3d = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "Unknown or incomplete option: %s (see --help)\n", a);
            return 2;
        }
        const char* v = argv[++i];
        if (std::strcmp(a, "--scenario") == 0)
            opt.scenarioBody = v;
        else if (std::strcmp(a, "--edl-time") == 0)
            opt.scenarioEdlTime = std::atof(v);
        else if (std::strcmp(a, "--cruise") == 0)
            opt.scenarioCruise = std::atof(v);
        else if (std::strcmp(a, "--screenshot") == 0)
            opt.screenshotPath = fromLaunchDir(v);
        else if (std::strcmp(a, "--sim-day") == 0)
            opt.startDay = std::atof(v);
        else if (std::strcmp(a, "--system") == 0)
            opt.startSystem = v;
        else if (std::strcmp(a, "--inspect") == 0)
            opt.inspectPlanet = v;
        else if (std::strcmp(a, "--moon") == 0)
            opt.inspectMoon = std::atoi(v);
        else if (std::strcmp(a, "--galaxy-distance") == 0) {
            opt.galaxy = true;
            opt.galaxyDistancePc = std::atof(v);
        } else if (std::strcmp(a, "--flyby") == 0)
            opt.flybySequence = std::atoi(v);
        else if (std::strcmp(a, "--record") == 0)
            opt.recordPath = fromLaunchDir(v);
        else if (std::strcmp(a, "--replay") == 0)
            opt.replayPath = fromLaunchDir(v);
        else if (std::strcmp(a, "--verify-replay") == 0)
            verifyReplay = fromLaunchDir(v);
        else if (std::strcmp(a, "--config") == 0)
            configPath = fromLaunchDir(v);
        else {
            std::fprintf(stderr, "Unknown option: %s (see --help)\n", a);
            return 2;
        }
    }

    // Assets (shaders/, data/, exosim.json) load relative to the exe, not
    // the launch directory, so the app works when started from anywhere.
    std::filesystem::path exeDir = ExecutableDirectory();
    if (!exeDir.empty()) {
        std::error_code ec;
        std::filesystem::current_path(exeDir, ec);
    }

    if (selfTest) return RunSelfTest(EXOSIM_VERSION);

    std::string cfgError;
    const std::string cfgFile = configPath.empty() ? std::string("exosim.json") : configPath;
    bool cfgLoaded = Config::Load(cfgFile, opt.config, cfgError);
    // Screenshot runs are reproducible regardless of local settings files.
    if (!opt.screenshotPath.empty()) {
        opt.config = Config{};
        cfgLoaded = false;
        cfgError.clear();
    }
    Log::SetConsoleLevel(opt.config.consoleLogLevel);
    Log::SetFileLevel(opt.config.fileLogLevel);
    if (!opt.config.logFile.empty() && !Log::OpenFile(opt.config.logFile))
        Log::Warn("app", "Cannot open log file " + opt.config.logFile);
    if (!cfgError.empty())
        Log::Warn("config", cfgError);
    else if (cfgLoaded)
        Log::Info("config", "Loaded " + cfgFile);
    for (const auto& w : opt.config.warnings) Log::Warn("config", w);
    Log::Info("app", std::string("ExoSim ") + EXOSIM_VERSION + " starting", {{"version", EXOSIM_VERSION}});

    if (!verifyReplay.empty()) return RunReplayVerify(verifyReplay, opt.config.dataDir);
    return RunApp(opt);
}
