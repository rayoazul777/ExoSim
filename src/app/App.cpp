#include "app/App.h"
#include "app/AppContext.h"
#include "app/Input.h"
#include "CrtPostProcess.h"
#include "Log.h"
#include "MissionPlanner.h"
#include "Renderer.h"
#include "scene/GalaxyScene.h"
#include "scene/Scenes.h"
#include "scene/SystemScene.h"
#include "ui/Panels.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {

bool SavePpm(GLFWwindow* window, const std::string& path) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream ppm(path, std::ios::binary);
    ppm << "P6\n" << w << " " << h << "\n255\n";
    for (int row = h - 1; row >= 0; --row) ppm.write(reinterpret_cast<const char*>(&px[static_cast<size_t>(row) * w * 3]), w * 3);
    ppm.close();
    if (!ppm) {
        Log::Error("app", "Could not write screenshot " + path);
        return false;
    }
    std::printf("Saved %s (%dx%d)\n", path.c_str(), w, h);
    return true;
}

// Applies the scripted-view options (after the first scene build).

bool ApplyStartupOptions(AppContext& ctx, const AppOptions& opt) {
    SimulationState& sim = ctx.sim;
    if (opt.flybySequence >= 0) {
        sim.apply(Command::Flyby(opt.flybySequence, sim.day()));
        ctx.view.flybyOpen = true;
        ctx.view.flybySequence = opt.flybySequence;
        if (sim.missionActive()) sim.apply(Command::Day(sim.mission().launchDay + 0.5 * sim.mission().transferDays));
        sim.apply(Command::Paused(true));
    }
    if (!opt.inspectPlanet.empty()) {
        const StarSystem& sys = sim.currentSystem();
        for (size_t local = 0; local < sys.planetIndices.size(); ++local) {
            size_t pIdx = sys.planetIndices[local];
            if (sim.planets()[pIdx].name != opt.inspectPlanet) continue;
            sim.apply(Command::Planet(static_cast<int>(local)));
            ctx.view.enterDetail(sim, pIdx, opt.inspectMoon);
        }
    }
    if (opt.galaxy) {
        OpenGalaxyMap(ctx);
        GalaxyView& g = ctx.view.galaxy;
        g.topDown = !opt.galaxy3d;
        if (opt.galaxyDistancePc > 0.0) {
            g.distance = g.distanceGoal = opt.galaxyDistancePc;
            // Zoomed in: center on the selected host (or the Sun).
            if (opt.galaxyDistancePc < 5000.0) {
                glm::dvec3 c(0.0);
                if (g.selected >= 0) c = ctx.galaxyScene->hosts()[static_cast<size_t>(g.selected)].posPc;
                g.target = g.targetGoal = c;
            }
        }
    }
    if (opt.scenarioBody.empty()) return true;
    int found = sim.findTarget(opt.scenarioBody);
    if (found < 0) {
        Log::Error("app", "Unknown --scenario body: " + opt.scenarioBody);
        return false;
    }
    sim.apply(Command::Target(found));
    sim.apply(Command::Plan(found));
    if (!sim.missionActive()) {
        Log::Error("app", "No launch window found for " + opt.scenarioBody);
        return false;
    }
    sim.apply(Command::Paused(true));
    if (opt.openPorkchop) {
        ctx.view.porkchop.open = true;
        ComputePorkchopFor(ctx);
    }
    if (opt.scenarioEdlTime >= 0.0 && sim.timeline().hasLanding) {
        sim.apply(Command::Day(sim.timeline().landingStartDay + opt.scenarioEdlTime / 86400.0));
        ctx.view.mode = ViewMode::Landing;
        sim.apply(Command::Speed(1.0 / 86400.0));
    } else if (opt.scenarioCruise >= 0.0) {
        sim.apply(Command::Day(sim.mission().launchDay + std::clamp(opt.scenarioCruise, 0.0, 1.0) * sim.mission().transferDays));
        ctx.cam.followingCraft = true;
        ctx.cam.systemDistance = ctx.cam.systemDistanceTarget = 0.6f;
    }
    return true;
}

void SaveRecording(AppContext& ctx, const std::string& path) {
    ReplayFile f = ReplayFile::FromRecording(ctx.sim, ctx.recordingStartDay, ctx.version);
    bool ok = f.save(path);
    ctx.view.sessionStatus = ok ? "Saved " + path : "Could not write " + path;
    if (ok)
        Log::Info(
            "replay", "Recording saved",
            {{"path", path}, {"commands", static_cast<double>(f.commands.size())}, {"ticks", static_cast<double>(f.endTicks)}});
    else
        Log::Error("replay", "Could not write recording " + path);
}

} // namespace

int RunApp(const AppOptions& opt) {
    if (!glfwInit()) {
        Log::Error("app", "Failed to init GLFW");
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    const Config& cfg = opt.config;
    GLFWwindow* window = glfwCreateWindow(cfg.windowWidth, cfg.windowHeight, "ExoSim", nullptr, nullptr);
    if (!window) {
        Log::Error("app", "Failed to create window");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(cfg.vsync ? 1 : 0);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        Log::Error("app", "Failed to init GLAD");
        return -1;
    }

    Renderer renderer;
    if (!renderer.init()) {
        Log::Error("app", "Renderer init failed");
        return -1;
    }
    CrtPostProcess crt;
    if (!crt.init(cfg.windowWidth, cfg.windowHeight)) {
        Log::Error("app", "CRT post-process init failed");
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    const bool screenshotMode = !opt.screenshotPath.empty();
    if (screenshotMode) ImGui::GetIO().IniFilename = nullptr; // reproducible window layout

    // --- Simulation ---
    std::unique_ptr<ReplayFile> replay;
    double startDay = std::isnan(opt.startDay) ? MissionPlanner::NowDaysSinceJ2000() : opt.startDay;
    if (!opt.replayPath.empty()) {
        replay = std::make_unique<ReplayFile>();
        std::string err;
        if (!ReplayFile::load(opt.replayPath, *replay, err)) {
            Log::Error("replay", "Cannot load " + opt.replayPath + ": " + err);
            return 1;
        }
        startDay = replay->startDay;
    }
    SimulationState sim(Catalog::Load(cfg.dataDir), startDay);
    sim.apply(Command::Speed(cfg.speedDaysPerSec));
    sim.apply(Command::WarpToEvents(cfg.warpToEvents));
    if (replay && (replay->planets != sim.planets().size() || replay->systems != sim.systems().size()))
        Log::Warn("replay", "Recording was made with different catalog data - playback may diverge.");

    SystemScene systemScene;
    GalaxyScene galaxyScene;
    AppContext ctx(sim, renderer, systemScene);
    ctx.window = window;
    ctx.galaxyScene = &galaxyScene;
    ctx.dataDir = cfg.dataDir;
    ctx.screenshotMode = screenshotMode;
    ctx.width = cfg.windowWidth;
    ctx.height = cfg.windowHeight;
    ctx.telemetryDir = cfg.telemetryDir;
    ctx.recordingsDir = cfg.recordingsDir;
    ctx.version = opt.version;
    InstallInputCallbacks(window, ctx);

    if (!opt.startSystem.empty()) {
        int si = sim.catalog().findSystem(opt.startSystem);
        if (si >= 0)
            sim.apply(Command::System(si));
        else
            Log::Warn("app", "Unknown --system: " + opt.startSystem);
    }
    ctx.view.logScale = opt.logScale;
    systemScene.rebuild(ctx, /*snap=*/true);
    if (!ApplyStartupOptions(ctx, opt)) return 1;

    if (replay) {
        ctx.replayFile = std::move(replay);
        ctx.replayPlayer = std::make_unique<ReplayPlayer>(*ctx.replayFile);
        Log::Info("replay", "Playing " + opt.replayPath, {{"commands", static_cast<double>(ctx.replayFile->commands.size())}});
    } else if (!opt.recordPath.empty()) {
        ctx.recordingStartDay = sim.day();
        sim.startRecording();
    }

    int frameCount = 0;
    bool screenshotOk = true;
    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        ++frameCount;
        if (screenshotMode) {
            now = frameCount / 60.0;
            dt = 1.0f / 60.0f;
        } // deterministic frames
        ctx.now = now;
        ctx.dt = dt;

        // --- Simulation step (fixed ticks; see SimClock) ---
        if (ctx.replaying()) {
            ctx.replayPlayer->advance(sim, dt);
        } else {
            SimulationState::StepResult step = sim.advance(dt);
            // Watching the craft when the landing begins: cut to the landing camera.
            if (step.landingStarted && ctx.cam.followingCraft) ctx.view.mode = ViewMode::Landing;
        }
        ctx.view.planetSpinAngle += dt * 0.4f;
        if (ctx.view.mode == ViewMode::Galaxy) ctx.view.galaxy.update(dt);
        systemScene.syncWithState(ctx); // picks up replayed system/sky changes

        // Ease the system-view camera toward its target (scale can differ by
        // orders of magnitude between systems).
        float easeAmount = 1.0f - std::exp(-dt * 8.0f);
        ctx.cam.systemDistance += (ctx.cam.systemDistanceTarget - ctx.cam.systemDistance) * easeAmount;

        if (ctx.view.mode == ViewMode::Landing && !(sim.missionActive() && sim.timeline().hasLanding))
            ctx.view.mode = ViewMode::System;

        // --- Scene (through the CRT pass) ---
        crt.resize(ctx.width, ctx.height);
        crt.beginScene();
        systemScene.updateCraft(ctx);
        switch (ctx.view.mode) {
            case ViewMode::System: systemScene.render(ctx); break;
            case ViewMode::PlanetDetail: RenderDetailScene(ctx); break;
            case ViewMode::Landing: RenderLandingScene(ctx); break;
            case ViewMode::Galaxy: galaxyScene.render(ctx); break;
        }
        crt.endSceneAndPresent(static_cast<float>(now));

        // --- HUD (after the CRT pass, so it stays crisp) ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        switch (ctx.view.mode) {
            case ViewMode::Landing: DrawLandingTelemetry(ctx); break;
            case ViewMode::Galaxy:
                galaxyScene.drawOverlay(ctx);
                DrawGalaxyPanel(ctx);
                break;
            case ViewMode::System:
                if (ctx.replaying()) ImGui::BeginDisabled();
                DrawControlPanel(ctx);
                DrawMissionPlanner(ctx);
                DrawFlightDirector(ctx);
                DrawPorkchop(ctx);
                DrawFlybyPlanner(ctx);
                if (ctx.replaying()) ImGui::EndDisabled();
                break;
            case ViewMode::PlanetDetail:
                if (ctx.view.detail.inspectingMoon())
                    DrawMoonDetail(ctx);
                else
                    DrawPlanetDetail(ctx);
                break;
        }
        DrawSessionControls(ctx);
        ImGui::Render();
        ctx.click.pending = false; // a click is consumed (or ignored) within its frame
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (screenshotMode && frameCount == 5) {
            screenshotOk = SavePpm(window, opt.screenshotPath);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        glfwSwapBuffers(window);
    }

    if (sim.recording() && !opt.recordPath.empty()) SaveRecording(ctx, opt.recordPath);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    crt.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return screenshotOk ? 0 : 1;
}
