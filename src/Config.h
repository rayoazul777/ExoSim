#pragma once
#include <string>
#include <vector>
#include "Log.h"

// Runtime configuration from exosim.json (next to the exe, or --config).
// Every key is optional; a missing file means all defaults, which reproduce
// the built-in behavior. Unknown keys and wrong types are reported (as
// warnings) rather than silently ignored, and the default is kept.
//
// {
//   "dataDir": "data",
//   "window": { "width": 1280, "height": 800, "vsync": true },
//   "simulation": { "speedDaysPerSec": 5.0, "warpToEvents": true },
//   "paths": { "telemetry": "telemetry", "recordings": "recordings" },
//   "log": { "console": "info", "file": "logs/exosim.jsonl", "fileLevel": "debug" }
// }
struct Config {
    std::string dataDir = "data";
    int windowWidth = 1280;
    int windowHeight = 800;
    bool vsync = true;
    double speedDaysPerSec = 5.0;
    bool warpToEvents = true;
    std::string telemetryDir = "telemetry";
    std::string recordingsDir = "recordings";
    Log::Level consoleLogLevel = Log::Level::Info;
    std::string logFile = "logs/exosim.jsonl";
    Log::Level fileLogLevel = Log::Level::Debug;

    std::vector<std::string> warnings; // unknown keys, wrong types, out-of-range values

    // Returns true if the file existed and parsed. On a parse error, `error`
    // is set and `out` keeps its defaults. A missing file is not an error.
    static bool Load(const std::string& path, Config& out, std::string& error);
    static bool Parse(const std::string& jsonText, Config& out, std::string& error);
};
