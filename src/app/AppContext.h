#pragma once
#include <memory>
#include <string>

#include "app/ViewState.h"
#include "sim/Replay.h"
#include "sim/SimulationState.h"

struct GLFWwindow;
class Renderer;
class SystemScene;
class GalaxyScene;

// Everything one frame of the app works with. Scenes and UI panels take an
// AppContext; they read the simulation through `sim` and change it only
// with sim.apply(Command) (see Command.h). The rest is presentation.
struct AppContext {
    SimulationState& sim;
    Renderer& renderer;
    SystemScene& systemScene;
    GLFWwindow* window = nullptr;
    GalaxyScene* galaxyScene = nullptr; // set by RunApp
    std::string dataDir = "data";
    bool screenshotMode = false; // deterministic frames: no hover effects from the (virtual) mouse

    ViewState view;
    CameraState cam;
    ClickState click;
    int width = 1280, height = 800;
    double now = 0.0; // render clock (seconds) for animation effects
    float dt = 0.0f; // real seconds since the previous frame

    // Session recording / replay (see Replay.h).
    std::string version;
    std::string telemetryDir = "telemetry";
    std::string recordingsDir = "recordings";
    double recordingStartDay = 0.0;
    std::unique_ptr<ReplayFile> replayFile;
    std::unique_ptr<ReplayPlayer> replayPlayer;
    bool replaying() const { return replayPlayer != nullptr; }

    AppContext(SimulationState& s, Renderer& r, SystemScene& sc) : sim(s), renderer(r), systemScene(sc) {}
};
