// ExoSim - retro-futurist orbital viewer
//
// Pipeline: load exoplanet orbital elements (NASA Exoplanet Archive via
// data/fetch_exoplanets.py) -> group by host star into StarSystems ->
// propagate the *currently selected* system's planet positions with
// KeplerOrbit -> draw wireframe orbits + points with Renderer -> composite
// through CrtPostProcess for the CRT look.
//
// Two view modes:
//   System   - orbit rings + planet points for one star system at a time.
//   Detail   - a single planet as a CRT wireframe globe, with an
//              estimated composition/atmosphere readout (PlanetClassifier).
//
// Camera is a free mouse-drag-orbit + scroll-zoom camera (see CameraState).

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "Dataset.h"
#include "KeplerOrbit.h"
#include "Renderer.h"
#include "CrtPostProcess.h"
#include "PlanetClassifier.h"
#include "StarClassifier.h"

namespace {
    int g_width = 1280, g_height = 800;

    enum class ViewMode { System, PlanetDetail };
    ViewMode g_viewMode = ViewMode::System;

    // Free orbit camera, driven by mouse drag (rotate) + scroll (zoom).
    // Yaw/pitch are shared between view modes (rotation feel carries
    // over); distance is tracked separately per mode since system view
    // and planet-detail view operate at wildly different scales.
    struct CameraState {
        bool dragging = false;
        double lastX = 0.0, lastY = 0.0;
        float yaw = 0.6f;
        float pitch = 0.45f;
        float systemDistance = 20.0f;
        float systemDistanceTarget = 20.0f; // eased toward on system switch / scale change
        float planetDistance = 3.0f;
        float planetDistanceTarget = 3.0f;
    };
    CameraState g_cam;

    void framebufferSizeCallback(GLFWwindow*, int w, int h) {
        g_width = w; g_height = h;
        glViewport(0, 0, w, h);
    }

    void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
        if (ImGui::GetIO().WantCaptureMouse) return;

        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                g_cam.dragging = true;
                glfwGetCursorPos(window, &g_cam.lastX, &g_cam.lastY);
            } else if (action == GLFW_RELEASE) {
                g_cam.dragging = false;
            }
        }
    }

    void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
        ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
        if (!g_cam.dragging) return;

        double dx = xpos - g_cam.lastX;
        double dy = ypos - g_cam.lastY;
        g_cam.yaw   += static_cast<float>(dx) * 0.006f;
        g_cam.pitch -= static_cast<float>(dy) * 0.006f;
        g_cam.pitch = std::clamp(g_cam.pitch, -1.5f, 1.5f); // avoid flipping over the poles
        g_cam.lastX = xpos;
        g_cam.lastY = ypos;
    }

    void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
        if (ImGui::GetIO().WantCaptureMouse) return;

        bool detail = (g_viewMode == ViewMode::PlanetDetail);
        float& dist = detail ? g_cam.planetDistance : g_cam.systemDistance;
        float& target = detail ? g_cam.planetDistanceTarget : g_cam.systemDistanceTarget;
        dist *= std::pow(0.9f, static_cast<float>(yoffset));
        float minD = detail ? 1.05f : 0.5f;
        float maxD = detail ? 30.0f : 20000.0f;
        dist = std::clamp(dist, minD, maxD);
        target = dist; // manual zoom is instant - only programmatic changes (system switch) ease in
    }

    glm::vec3 orbitCameraEye(float distance) {
        return glm::vec3(
            distance * std::cos(g_cam.pitch) * std::cos(g_cam.yaw),
            distance * std::sin(g_cam.pitch),
            distance * std::cos(g_cam.pitch) * std::sin(g_cam.yaw)
        );
    }

    // --- Log-scale display helpers ---
    // Real semi-major axes range from a fraction of an AU to hundreds of
    // AU across different systems. Compressing large distances with a
    // log curve keeps compact systems from vanishing into a single pixel
    // next to sprawling ones, while preserving each planet's *angle*
    // (direction from the star) exactly - only the radius is remapped.
    double displayMagnitude(double auMagnitude, bool logScale) {
        if (!logScale) return auMagnitude;
        return std::log10(1.0 + auMagnitude) * 3.0;
    }

    glm::vec3 toDisplaySpace(const glm::dvec3& posAU, bool logScale) {
        double r = glm::length(posAU);
        if (r < 1e-9) return glm::vec3(0.0f);
        double displayR = displayMagnitude(r, logScale);
        glm::dvec3 dir = posAU / r;
        glm::dvec3 displayPos = dir * displayR;
        return glm::vec3(displayPos);
    }

    bool containsCaseInsensitive(const std::string& haystack, const std::string& needle) {
        if (needle.empty()) return true;
        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        return it != haystack.end();
    }

    // NASA's Exoplanet Archive contains no confirmed exomoon detections
    // (only a small handful of disputed, unconfirmed candidates exist in
    // the literature, system-wide). So for any Archive-loaded planet, this
    // generates a deterministic set of purely illustrative "candidate"
    // moons for visualization - never real data, always labeled as such
    // wherever it's shown. Count and placement are seeded from the
    // planet's name (via `seed`) so the same planet always shows the same
    // illustrative moons, and biased loosely by composition (gas/ice
    // giants get more, in keeping with the solar-system pattern).
    std::vector<MoonRecord> generateSpeculativeMoons(PlanetType type, float seed) {
        int count = 0;
        switch (type) {
            case PlanetType::GasGiant:    count = 2 + (static_cast<int>(seed) % 3); break; // 2-4
            case PlanetType::IceGiant:    count = 1 + (static_cast<int>(seed) % 3); break; // 1-3
            case PlanetType::MiniNeptune: count = 1 + (static_cast<int>(seed) % 2); break; // 1-2
            case PlanetType::SuperEarth:  count = static_cast<int>(seed) % 2; break;       // 0-1
            case PlanetType::Rocky:       count = static_cast<int>(seed) % 2; break;       // 0-1
            default:                      count = 0; break;
        }

        std::vector<MoonRecord> moons;
        for (int i = 0; i < count; ++i) {
            MoonRecord m;
            m.name = "Candidate " + std::string(1, static_cast<char>('I' + i)); // "Candidate I", "II", ...
            float variety = std::fmod(seed * (static_cast<float>(i) + 1.7f), 23.0f);
            m.orbitRadiusPlanetRadii = 3.0 + variety;      // purely illustrative spacing
            m.orbitPeriodDays = 2.0 + variety * 1.5;       // purely illustrative, not physically derived
            m.isReal = false;
            moons.push_back(m);
        }
        return moons;
    }
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(g_width, g_height, "ExoSim", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSwapInterval(1); // vsync

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return -1;
    }

    Renderer renderer;
    if (!renderer.init()) { std::cerr << "Renderer init failed\n"; return -1; }

    CrtPostProcess crt;
    if (!crt.init(g_width, g_height)) { std::cerr << "CRT post-process init failed\n"; return -1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Install our own callbacks AFTER ImGui's init, and forward every
    // event into the ImGui_ImplGlfw_* functions manually - this lets us
    // both drive the camera AND keep ImGui's mouse handling working.
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    // --- Load data (real archive export if present, else demo system) ---
    std::vector<PlanetRecord> planets;
    if (!Dataset::LoadFromFile("data/exoplanets.json", planets) || planets.empty()) {
        std::cout << "[main] Falling back to built-in demo solar system.\n";
        planets = Dataset::DemoSolarSystem();
    }

    std::vector<KeplerOrbit> orbits;
    orbits.reserve(planets.size());
    for (auto& p : planets) orbits.emplace_back(p.elements);

    std::vector<StarSystem> systems = Dataset::GroupByHost(planets);
    std::cout << "[main] Grouped " << planets.size() << " planets into "
              << systems.size() << " star systems.\n";

    int currentSystemIndex = 0;
    int selectedPlanetLocal = -1; // index within systems[currentSystemIndex].planetIndices
    bool logScaleEnabled = false;
    float starSizeExaggeration = 30.0f; // real AU-scale stars are near-invisible next to orbits

    char searchBuffer[128] = "";
    int minPlanetsFilter = 1;

    // Planet detail view state.
    size_t detailPlanetGlobalIndex = 0;
    PlanetClassification detailClassification;
    float planetSpinAngle = 0.0f;
    float detailSurfaceSeed = 0.0f; // varies the procedural pattern per planet
    std::vector<MoonRecord> detailMoons;
    bool detailMoonsAreReal = false; // true only for the built-in demo solar system

    // Current system's star (recomputed whenever the selected system changes).
    StarClassification currentStar;
    float starDisplayRadius = 0.05f; // in the same display-space units as orbit rings

    // Reference-distance "guide rings" shown only when log-scale is on, so
    // the compression doesn't silently imply orbits are literally that
    // close together. Each entry is a real AU value plus its precomputed
    // display-space circle.
    struct GuideRing { double realAU; std::vector<glm::vec3> points; };
    std::vector<GuideRing> guideRings;

    constexpr double kSolarRadiusAU = 0.00465047; // 1 solar radius, in AU

    // Rebuilds the orbit-ring geometry, star classification/size, guide
    // rings, and camera fit for one star system. Called on startup, on
    // system switch, and whenever log-scale is toggled (since ring
    // geometry depends on it). `snap` jumps the camera straight to the
    // new distance instead of easing - used only for the very first call,
    // so the view doesn't animate in from nothing on startup.
    auto selectSystem = [&](int sysIdx, bool snap = false) {
        renderer.clearOrbits();
        const StarSystem& sys = systems[sysIdx];
        double maxA = 0.0;
        double minA = 1e18;

        for (size_t local = 0; local < sys.planetIndices.size(); ++local) {
            size_t pIdx = sys.planetIndices[local];
            double a = planets[pIdx].elements.semiMajorAxisAU;
            maxA = std::max(maxA, a);
            minA = std::min(minA, a);

            std::vector<glm::vec3> ring;
            constexpr int kSegments = 180;
            for (int s = 0; s <= kSegments; ++s) {
                double t = planets[pIdx].elements.periodDays * (double)s / kSegments;
                glm::dvec3 pos = orbits[pIdx].positionAtTime(t);
                ring.push_back(toDisplaySpace(pos, logScaleEnabled));
            }
            renderer.setOrbitPath(local, ring);
        }

        double maxDisplay = displayMagnitude(std::max(maxA, 0.05), logScaleEnabled);
        double minDisplay = sys.planetIndices.empty() ? maxDisplay
                                                       : displayMagnitude(std::max(minA, 0.01), logScaleEnabled);

        currentStar = StarClassifier::Classify(sys.starTeffK, sys.starRadiusSolar, sys.starSpectralType);
        double starRealAU = currentStar.displayRadiusSolar * kSolarRadiusAU;
        double exaggerated = starRealAU * starSizeExaggeration;
        // Never let the star visually swallow the innermost orbit, however
        // large the exaggeration slider is set.
        starDisplayRadius = static_cast<float>(std::min(exaggerated, minDisplay * 0.3));
        starDisplayRadius = std::max(starDisplayRadius, 0.01f);

        // Guide rings: only meaningful (and only drawn) with log-scale on,
        // since without it, distance in the view already IS real AU.
        guideRings.clear();
        if (logScaleEnabled) {
            for (double refAU : {1.0, 10.0, 100.0, 1000.0}) {
                if (refAU < maxA * 0.05 || refAU > maxA * 20.0) continue; // skip irrelevant scales
                GuideRing gr;
                gr.realAU = refAU;
                double r = displayMagnitude(refAU, true);
                constexpr int kRingSegs = 96;
                for (int s = 0; s <= kRingSegs; ++s) {
                    double phi = 2.0 * 3.14159265358979323846 * s / kRingSegs;
                    gr.points.emplace_back(static_cast<float>(r * std::cos(phi)), 0.0f, static_cast<float>(r * std::sin(phi)));
                }
                guideRings.push_back(std::move(gr));
                if (guideRings.size() >= 3) break; // keep it uncluttered
            }
        }

        float fitDistance = static_cast<float>(std::max(maxDisplay, 0.5)) * 2.5f;
        g_cam.systemDistanceTarget = fitDistance;
        if (snap) g_cam.systemDistance = fitDistance;
    };

    selectSystem(currentSystemIndex, /*snap=*/true);

    float simSpeedDaysPerSecond = 5.0f;
    float simTimeDays = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        static double lastTime = glfwGetTime();
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        simTimeDays += dt * simSpeedDaysPerSecond;
        planetSpinAngle += dt * 0.4f;

        // Ease the system-view camera distance toward its target rather
        // than snapping - matters most when switching systems, since
        // scale can differ by orders of magnitude between them.
        float easeAmount = 1.0f - std::exp(-dt * 8.0f);
        g_cam.systemDistance += (g_cam.systemDistanceTarget - g_cam.systemDistance) * easeAmount;

        crt.resize(g_width, g_height);

        glm::mat4 proj;
        glm::mat4 view;
        glm::vec3 eye;

        crt.beginScene();

        if (g_viewMode == ViewMode::System) {
            const StarSystem& currentSystem = systems[currentSystemIndex];

            eye = orbitCameraEye(g_cam.systemDistance);
            view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
            proj = glm::perspective(glm::radians(45.0f), (float)g_width / (float)g_height,
                                     0.01f, g_cam.systemDistance * 50.0f);
            glm::mat4 viewProj = proj * view;

            std::vector<glm::vec3> positions(currentSystem.planetIndices.size());
            for (size_t local = 0; local < currentSystem.planetIndices.size(); ++local) {
                size_t pIdx = currentSystem.planetIndices[local];
                glm::dvec3 pos = orbits[pIdx].positionAtTime(simTimeDays);
                positions[local] = toDisplaySpace(pos, logScaleEnabled);
            }

            renderer.render(viewProj, positions);

            glm::mat4 starModel = glm::scale(glm::mat4(1.0f), glm::vec3(starDisplayRadius));
            renderer.renderStar(viewProj, starModel, currentStar.color);

            for (const auto& gr : guideRings) {
                renderer.renderAuxiliaryRing(viewProj, gr.points, glm::vec3(0.5f, 0.5f, 0.55f), 0.25f);
            }
        } else {
            eye = orbitCameraEye(g_cam.planetDistance);
            view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
            proj = glm::perspective(glm::radians(45.0f), (float)g_width / (float)g_height, 0.01f, 100.0f);
            glm::mat4 viewProj = proj * view;

            glm::mat4 model = glm::rotate(glm::mat4(1.0f), planetSpinAngle, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 phosphorGreen(0.15f, 0.95f, 0.55f); // matches the orbit-ring accent color

            renderer.renderPlanetSurface(viewProj, model, eye, static_cast<int>(detailClassification.type),
                                          detailClassification.displayColor, phosphorGreen,
                                          static_cast<float>(now), detailSurfaceSeed);

            // Wireframe targeting-grid overlay, very slightly inflated so
            // it doesn't z-fight with the solid surface underneath.
            glm::mat4 gridModel = glm::scale(model, glm::vec3(1.01f));
            renderer.renderPlanetDetail(viewProj, gridModel, phosphorGreen * 0.8f);

            // Moons: real ones for the demo solar system, otherwise
            // speculative/illustrative ones (see generateSpeculativeMoons).
            // Distances are compressed with a log curve into a viewable
            // range - real moon distances (tens of planet-radii) would
            // otherwise put them far outside the camera's usual zoom range.
            for (size_t mi = 0; mi < detailMoons.size(); ++mi) {
                const MoonRecord& moon = detailMoons[mi];
                float dispR = 1.3f + 0.9f * std::log10(1.0f + static_cast<float>(moon.orbitRadiusPlanetRadii));
                float speed = 0.5f / dispR; // closer moons orbit faster, stylized not physical
                float phase = static_cast<float>(mi) * 2.1f + detailSurfaceSeed * 0.01f;
                float angle = static_cast<float>(now) * speed + phase;
                glm::vec3 moonPos(dispR * std::cos(angle), 0.0f, dispR * std::sin(angle));

                glm::mat4 moonModel = glm::translate(glm::mat4(1.0f), moonPos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));
                glm::vec3 moonColor = moon.isReal ? glm::vec3(0.8f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.65f);
                float moonAlpha = moon.isReal ? 0.7f : 0.4f; // dimmer for speculative ones
                renderer.renderPlanetDetail(viewProj, moonModel, moonColor, moonAlpha);

                // Faint orbit path guide.
                std::vector<glm::vec3> orbitPts;
                constexpr int kOrbitSegs = 48;
                for (int s = 0; s <= kOrbitSegs; ++s) {
                    float p = 2.0f * 3.14159265f * s / kOrbitSegs;
                    orbitPts.emplace_back(dispR * std::cos(p), 0.0f, dispR * std::sin(p));
                }
                renderer.renderAuxiliaryRing(viewProj, orbitPts, moonColor, moonAlpha * 0.4f);
            }
        }

        crt.endSceneAndPresent(static_cast<float>(now));

        // --- HUD (drawn after the CRT pass, so it stays crisp) ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (g_viewMode == ViewMode::System) {
            const StarSystem& currentSystem = systems[currentSystemIndex];

            ImGui::Begin("ExoSim Control");
            ImGui::Text("Star systems loaded: %zu", systems.size());
            ImGui::SliderFloat("Sim speed (days/sec)", &simSpeedDaysPerSecond, 0.0f, 200.0f);
            ImGui::Text("Elapsed sim time: %.1f days", simTimeDays);
            if (ImGui::Checkbox("Log-scale distances", &logScaleEnabled)) {
                selectSystem(currentSystemIndex);
            }
            ImGui::TextDisabled("(compresses large orbits so compact systems don't vanish)");
            if (!guideRings.empty()) {
                std::string legend = "Reference rings shown:";
                for (const auto& gr : guideRings) {
                    legend += " " + (gr.realAU >= 1000.0 ? std::to_string((int)(gr.realAU / 1000.0)) + "k" : std::to_string((int)gr.realAU)) + " AU";
                }
                ImGui::TextDisabled("%s", legend.c_str());
            }
            if (ImGui::SliderFloat("Star size exaggeration", &starSizeExaggeration, 1.0f, 200.0f)) {
                selectSystem(currentSystemIndex);
            }
            ImGui::TextDisabled("(a star's true AU-scale size is nearly invisible next to orbits -");
            ImGui::TextDisabled(" this exaggerates it uniformly, so relative sizes between");
            ImGui::TextDisabled(" different stars stay accurate even though absolute size isn't)");
            ImGui::Separator();

            ImGui::TextColored(ImVec4(currentStar.color.r, currentStar.color.g, currentStar.color.b, 1.0f),
                                "Host star: %s", currentStar.spectralClass.c_str());
            if (currentStar.hasMeasuredRadius) {
                ImGui::Text("Radius: %.2f solar radii (measured)", currentStar.displayRadiusSolar);
            } else {
                ImGui::TextDisabled("Radius: not measured (assumed Sun-like, 1.0 solar radii)");
            }
            if (currentSystem.numStars > 1) {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                    "Multi-star system (%d stars) - only the primary is modeled here;", currentSystem.numStars);
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                    "companion star(s) aren't rendered.");
            }
            ImGui::Separator();

            ImGui::Text("Find a star system:");
            ImGui::InputTextWithHint("##search", "Search host star name...", searchBuffer, sizeof(searchBuffer));
            ImGui::SliderInt("Min planets in system", &minPlanetsFilter, 1, 8);

            ImGui::BeginChild("SystemList", ImVec2(0, 140), true);
            std::string search(searchBuffer);
            for (int i = 0; i < (int)systems.size(); ++i) {
                if ((int)systems[i].planetIndices.size() < minPlanetsFilter) continue;
                if (!containsCaseInsensitive(systems[i].hostName, search)) continue;

                std::string label = systems[i].hostName + " ("
                    + std::to_string(systems[i].planetIndices.size()) + " planet"
                    + (systems[i].planetIndices.size() != 1 ? "s" : "") + ")";
                if (ImGui::Selectable(label.c_str(), i == currentSystemIndex)) {
                    if (i != currentSystemIndex) {
                        currentSystemIndex = i;
                        selectSystem(currentSystemIndex);
                        selectedPlanetLocal = -1;
                    }
                }
            }
            ImGui::EndChild();

            ImGui::Separator();
            ImGui::Text("Planets in %s:", currentSystem.hostName.c_str());
            for (int local = 0; local < (int)currentSystem.planetIndices.size(); ++local) {
                size_t pIdx = currentSystem.planetIndices[local];
                if (ImGui::Selectable(planets[pIdx].name.c_str(), selectedPlanetLocal == local)) {
                    selectedPlanetLocal = local;
                }
            }

            if (selectedPlanetLocal >= 0 && selectedPlanetLocal < (int)currentSystem.planetIndices.size()) {
                size_t pIdx = currentSystem.planetIndices[selectedPlanetLocal];
                const auto& p = planets[pIdx];
                ImGui::Separator();
                ImGui::Text("Host star: %s", p.hostStar.c_str());
                ImGui::Text("Semi-major axis: %.3f AU", p.elements.semiMajorAxisAU);
                ImGui::Text("Eccentricity: %.3f", p.elements.eccentricity);
                ImGui::Text("Period: %.1f days", p.elements.periodDays);
                if (p.elements.inclinationMeasured) {
                    ImGui::Text("Inclination: %.2f deg (measured)", p.elements.inclinationDeg);
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                        "Inclination: not measured (assumed coplanar, 0 deg)");
                }

                if (ImGui::Button("Inspect Planet")) {
                    detailPlanetGlobalIndex = pIdx;
                    detailClassification = PlanetClassifier::Classify(p);
                    detailSurfaceSeed = 0.0f;
                    for (char ch : p.name) detailSurfaceSeed += static_cast<float>(static_cast<unsigned char>(ch));
                    if (!p.moons.empty()) {
                        detailMoons = p.moons;
                        detailMoonsAreReal = true;
                    } else {
                        detailMoons = generateSpeculativeMoons(detailClassification.type, detailSurfaceSeed);
                        detailMoonsAreReal = false;
                    }
                    g_viewMode = ViewMode::PlanetDetail;
                }
            }
            ImGui::End();
        } else {
            const auto& p = planets[detailPlanetGlobalIndex];
            ImGui::Begin("Planet Detail");
            ImGui::Text("%s", p.name.c_str());
            ImGui::Text("Host star: %s", p.hostStar.c_str());
            ImGui::TextDisabled("Scroll to zoom in on the surface pattern below.");
            ImGui::Separator();

            if (detailClassification.hasRealAtmosphereFindings) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "MEASURED (published spectroscopy):");
                ImGui::TextWrapped("%s", detailClassification.realAtmosphereFindings.c_str());
                ImGui::Separator();
            }

            ImGui::TextColored(ImVec4(detailClassification.displayColor.r,
                                       detailClassification.displayColor.g,
                                       detailClassification.displayColor.b, 1.0f),
                                "%s", detailClassification.typeLabel.c_str());
            ImGui::TextWrapped("Surface: %s", detailClassification.surfaceGuess.c_str());
            ImGui::TextWrapped("Atmosphere: %s", detailClassification.atmosphereGuess.c_str());

            ImGui::Separator();
            if (p.densityGcm3 > 0.0) {
                ImGui::Text("Bulk density: %.2f g/cm^3", p.densityGcm3);
            }
            if (p.radiusJupiter > 0.0) {
                ImGui::Text("Radius: %.3f Jupiter radii (%.2f Earth radii)",
                            p.radiusJupiter, p.radiusJupiter * 11.2);
            }
            if (p.massJupiter > 0.0) {
                ImGui::Text("Mass: %.3f Jupiter masses (%.2f Earth masses)",
                            p.massJupiter, p.massJupiter * 317.8);
            }
            if (p.eqTempK > 0.0) {
                ImGui::Text("Equilibrium temperature: %.0f K", p.eqTempK);
            }
            if (!detailClassification.hasAnyPhysicalData) {
                ImGui::TextDisabled("No radius, density, or mass on record for this planet.");
            } else if (detailClassification.basedOnDensity) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1.0f),
                    "Classified from measured bulk density - the most reliable signal available.");
            } else if (!detailClassification.basedOnRadius) {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                    "Estimated from mass only (no measured radius or density) - lower confidence.");
            }
            ImGui::TextDisabled("Composition/atmosphere are statistical estimates from");
            ImGui::TextDisabled("radius/mass, not direct measurements - the Archive rarely");
            ImGui::TextDisabled("has spectroscopic data outside a handful of flagship targets.");
            ImGui::TextDisabled("The surface/cloud pattern above is a procedural stylization");
            ImGui::TextDisabled("driven by that classification - not a real image. No exoplanet");
            ImGui::TextDisabled("has ever been directly imaged in this kind of visual detail.");

            ImGui::Separator();
            if (detailMoons.empty()) {
                ImGui::TextDisabled("No moons shown for this planet.");
            } else if (detailMoonsAreReal) {
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Moons (real, approximate known orbits):");
                for (const auto& m : detailMoons) {
                    ImGui::BulletText("%s - orbital period ~%.2f days", m.name.c_str(), m.orbitPeriodDays);
                }
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "No confirmed moons on record.");
                ImGui::TextDisabled("The Archive doesn't include exomoon detections (only a couple");
                ImGui::TextDisabled("of disputed candidates exist in the literature, system-wide).");
                ImGui::TextDisabled("The moons below are illustrative placeholders only:");
                for (const auto& m : detailMoons) {
                    ImGui::BulletText("%s (speculative, not a real detection)", m.name.c_str());
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Back to System View")) {
                g_viewMode = ViewMode::System;
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    crt.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
