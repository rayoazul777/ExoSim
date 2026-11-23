#pragma once
#include <limits>
#include <string>
#include "Config.h"

// Command-line options for the windowed app (parsed in main.cpp).
struct AppOptions {
    // Reproducible views (docs, visual regression checks):
    std::string scenarioBody; // --scenario <body>: plan a mission there at its next window
    double scenarioEdlTime = -1.0; // --edl-time <s>: open the Landing view this far into the descent
    double scenarioCruise = -1.0; // --cruise <0..1>: follow the spacecraft at this fraction of the transfer
    std::string screenshotPath; // --screenshot <file.ppm>: save frame 5 and exit (deterministic frames)
    double startDay = std::numeric_limits<double>::quiet_NaN(); // --sim-day <days since J2000>
    std::string startSystem; // --system <host name>
    std::string inspectPlanet; // --inspect <planet name>
    int inspectMoon = -1; // --moon <index>
    bool openPorkchop = false; // --porkchop: open (and compute) the porkchop plot for the scenario target
    int flybySequence = -1; // --flyby <index>: plan a gravity-assist chain (GravityAssist::Sequences)
    bool logScale = false; // --log-scale
    bool galaxy = false; // --galaxy: open the Milky Way map
    bool galaxy3d = false; // --galaxy-3d: ... in the 3-D view
    double galaxyDistancePc = -1.0; // --galaxy-distance <pc>: camera distance (zoom); < 5 kpc centers on the current host
    // Session recording / replay:
    std::string recordPath; // --record <file.jsonl>: record from startup, save on exit
    std::string replayPath; // --replay <file.jsonl>: play a recording back in the window
    std::string version = "dev";
    Config config;
};

int RunApp(const AppOptions& options);
