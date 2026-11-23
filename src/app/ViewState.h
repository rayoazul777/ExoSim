#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "Dataset.h"
#include "PlanetClassifier.h"
#include "SolarSystemAppearance.h"
#include "Porkchop.h"

class SimulationState;

// Presentation state: which view is showing and how. None of this affects
// the simulation, so it's not recorded or replayed (see Command.h).
enum class ViewMode { System, PlanetDetail, Landing, Galaxy };

struct DetailView {
    size_t planetIdx = 0;
    PlanetClassification classification;
    PlanetAppearance appearance; // isKnown only for Sol's documented bodies
    float surfaceSeed = 0.0f; // varies the procedural pattern per planet
    std::vector<MoonRecord> moons;
    bool moonsReal = false; // true for Sol's built-in moons
    int moonIndex = -1; // >= 0: inspecting moons[moonIndex], not the planet
    bool inspectingMoon() const { return moonIndex >= 0 && moonIndex < static_cast<int>(moons.size()); }
};

// Porkchop window: the grid is derived data (computed on request from the
// catalog), so it lives with the view, not the simulation.
struct PorkchopView {
    bool open = false;
    int target = -1; // mission target the grid was computed for
    int metric = 0; // PorkchopMetric
    PorkchopGrid grid;
    double hohmannLaunch = 0.0, hohmannTof = 0.0;
};

// Milky Way map camera and layers. Positions are heliocentric Galactic
// Cartesian parsecs (see galaxy/GalaxyModel.h). Top-down is an orthographic
// map seen from the North Galactic Pole with the Galactic Center up; 3-D is
// a perspective orbit around `target`. Zoom is logarithmic from 1 pc to 80 kpc.
struct GalaxyView {
    static constexpr double kMinDistancePc = 1.0, kMaxDistancePc = 80000.0;
    static constexpr double kTanHalfFov = 0.41421356237; // tan(22.5 deg)
    bool topDown = true;
    glm::dvec3 target{8178.0, 0.0, 0.0}; // start centered on the Galactic Center
    glm::dvec3 targetGoal{8178.0, 0.0, 0.0};
    double distance = 42000.0, distanceGoal = 42000.0;
    double yaw = 3.14159265358979, pitch = 0.55; // 3-D: eye behind the Sun, looking toward the center
    bool showModel = true, showArms = true, showHosts = true, showLocal = true, showRings = true, showLabels = true;
    int hovered = -1, selected = -1; // indices into GalaxyScene::hosts()
    char search[64] = "";
    bool opened = false; // first open frames the whole Galaxy

    glm::dmat4 viewProj(double aspect) const;
    // Screen-space basis in world units per pixel (for panning / zoom-at-cursor).
    void screenBasis(int winH, glm::dvec3& rightPerPx, glm::dvec3& upPerPx) const;
    void pan(double dxPx, double dyPx, int winH);
    // Zooms by `factor` (< 1 = in), keeping the point under the cursor fixed in top-down mode.
    void zoomAt(double factor, double sx, double sy, int winW, int winH);
    void rotate(double dxPx, double dyPx);
    void flyTo(const glm::dvec3& pc, double distancePc);
    void update(float dt); // eases toward flyTo goals
    double fieldWidthPc(double aspect) const { return 2.0 * distance * kTanHalfFov * aspect; }
};

struct ViewState {
    ViewMode mode = ViewMode::System;
    bool logScale = false;
    float starSizeExaggeration = 30.0f; // real AU-scale stars are near-invisible next to orbits
    char searchBuffer[128] = "";
    int minPlanetsFilter = 1;
    DetailView detail;
    float planetSpinAngle = 0.0f;
    std::string telemetryStatus; // result of the last CSV export
    std::string sessionStatus; // result of the last recording save
    PorkchopView porkchop;
    bool flybyOpen = false;
    int flybySequence = 0;
    GalaxyView galaxy;

    // Opens the Detail view on a planet (moonIndex -1) or one of its moons.
    void enterDetail(const SimulationState& sim, size_t planetIdx, int moonIndex);
};

// Free orbit camera: mouse drag rotates, scroll zooms. Yaw/pitch are shared
// between views; distance is per view (system and planet scales differ by
// orders of magnitude).
struct CameraState {
    bool dragging = false;
    bool panning = false; // right-drag (Milky Way map)
    double lastX = 0.0, lastY = 0.0;
    float yaw = 0.6f;
    float pitch = 0.45f;
    float systemDistance = 20.0f;
    float systemDistanceTarget = 20.0f; // eased toward on system switch / scale change
    float fitDistance = 20.0f; // distance that frames the whole current system
    float planetDistance = 3.0f;
    float planetDistanceTarget = 3.0f;
    float landingZoom = 1.0f; // multiplier on the landing chase camera's automatic distance
    bool followingCraft = false; // system view centred on the spacecraft

    glm::vec3 orbitEye(float distance) const;
    void startFollowing();
    void stopFollowing();
};

// A left click is a press + release that barely moved (otherwise it was a
// camera drag). Picking happens in the scene that knows where things are.
struct ClickState {
    bool pending = false;
    double pressX = 0.0, pressY = 0.0, x = 0.0, y = 0.0;
};
