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
#include <unordered_map>
#include <filesystem>
#include <cstring>
#include <cstdio>

#include "Dataset.h"
#include "KeplerOrbit.h"
#include "Renderer.h"
#include "CrtPostProcess.h"
#include "PlanetClassifier.h"
#include "StarClassifier.h"
#include "SolarSystemAppearance.h"
#include "MissionPlanner.h"
#include "Platform.h"

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

    // KeplerOrbit works in the ecliptic frame (orbital plane = x-y, north =
    // +z). The renderer's world is y-up (camera, rings, companions and guide
    // rings all treat x-z as horizontal). Before this conversion existed,
    // orbits were drawn in x-y - so e.g. Saturn's moons orbited
    // perpendicular to its rings. One rotation, used everywhere.
    glm::dvec3 eclipticToWorld(const glm::dvec3& p) {
        return glm::dvec3(p.x, p.z, -p.y);
    }

    glm::vec3 toDisplaySpace(const glm::dvec3& posEcliptic, bool logScale) {
        glm::dvec3 posAU = eclipticToWorld(posEcliptic);
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

    float hashSeedFromString(const std::string& s) {
        float seed = 0.0f;
        for (char ch : s) seed += static_cast<float>(static_cast<unsigned char>(ch));
        return seed;
    }

    // Converts a real B-V color index to an approximate RGB color via
    // piecewise linear interpolation across a handful of reference
    // points spanning hot blue-white stars to cool red ones. This is a
    // standard style of approximation for star-color visualization, not
    // a precise blackbody-spectrum calculation - the INPUT (B-V) is a
    // real measurement, the color mapping itself is an approximation.
    glm::vec3 bvToColor(double bv) {
        struct Anchor { float bv; glm::vec3 color; };
        static const Anchor anchors[] = {
            {-0.4f, glm::vec3(0.61f, 0.70f, 1.00f)}, // hot blue-white (O/B)
            { 0.0f, glm::vec3(0.93f, 0.93f, 1.00f)}, // white (A)
            { 0.3f, glm::vec3(1.00f, 0.96f, 0.87f)}, // yellow-white (F)
            { 0.6f, glm::vec3(1.00f, 0.89f, 0.70f)}, // yellow, Sun-like (G)
            { 1.0f, glm::vec3(1.00f, 0.75f, 0.48f)}, // orange (K)
            { 1.5f, glm::vec3(1.00f, 0.58f, 0.32f)}, // orange-red (M)
            { 2.0f, glm::vec3(1.00f, 0.45f, 0.22f)}, // deep red (cool M)
        };
        constexpr int n = static_cast<int>(sizeof(anchors) / sizeof(anchors[0]));
        float b = static_cast<float>(bv);
        if (b <= anchors[0].bv) return anchors[0].color;
        if (b >= anchors[n - 1].bv) return anchors[n - 1].color;
        for (int i = 0; i < n - 1; ++i) {
            if (b >= anchors[i].bv && b <= anchors[i + 1].bv) {
                float t = (b - anchors[i].bv) / (anchors[i + 1].bv - anchors[i].bv);
                return glm::mix(anchors[i].color, anchors[i + 1].color, t);
            }
        }
        return anchors[n - 1].color;
    }

    // Point size and brightness from real visual magnitude, using a
    // linear-in-magnitude falloff (the conventional practical choice for
    // star rendering - magnitude is already a log scale of true flux, so
    // this reads as a natural brightness gradient without the extreme
    // dynamic range true linear flux would produce). Constants are tuned
    // by eye for a 1280x800-ish window, not derived from a physical model.
    float vmagToPointSize(double vmag) {
        float size = 1.0f + static_cast<float>(6.5 - vmag) * 0.9f;
        return std::clamp(size, 1.0f, 7.0f);
    }

    float vmagToBrightness(double vmag) {
        float b = 1.5f - static_cast<float>(vmag) * 0.15f;
        return std::clamp(b, 0.35f, 1.6f);
    }

    // Real right ascension/declination (equatorial, ICRS/J2000-ish epoch
    // from Hipparcos) converted to a unit direction in the app's own
    // arbitrary world frame - y-up to match this engine's convention.
    glm::vec3 raDecToDirection(double raDeg, double decDeg) {
        // Equatorial (ICRS RA/Dec) unit vector: x toward the vernal equinox,
        // z toward the celestial north pole.
        double ra = glm::radians(raDeg), dec = glm::radians(decDeg);
        glm::dvec3 eq(std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec));
        // Rotate equatorial -> ecliptic by the J2000 obliquity (23.4393 deg)
        // so stars share the planets' frame, then map into the y-up world
        // with the same eclipticToWorld() the orbits use. (Previously stars
        // stayed equatorial while planets were ecliptic, leaving the real
        // starfield tilted ~23 deg relative to the planetary orbits.)
        const double eps = glm::radians(23.4392911);
        glm::dvec3 ecl(eq.x, eq.y * std::cos(eps) + eq.z * std::sin(eps), -eq.y * std::sin(eps) + eq.z * std::cos(eps));
        return glm::normalize(glm::vec3(eclipticToWorld(ecl)));
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
            m.inclinationDeg = std::fmod(seed * (static_cast<float>(i) + 3.3f), 25.0f);   // illustrative
            m.eccentricity = std::fmod(seed * (static_cast<float>(i) + 1.1f), 28.0f) / 100.0; // illustrative, kept low
            m.argPeriapsisDeg = std::fmod(seed * (static_cast<float>(i) + 5.7f), 360.0f);  // illustrative
            m.longAscNodeDeg = std::fmod(seed * (static_cast<float>(i) + 7.9f), 360.0f);   // illustrative
            m.isReal = false;
            moons.push_back(m);
        }
        return moons;
    }
}

#ifndef EXOSIM_VERSION
#define EXOSIM_VERSION "dev"
#endif

namespace {
    // Headless check that a packaged build is complete and loads correctly.
    // Runs before any window or GL context exists, so it works on CI
    // machines with no GPU (the release workflow runs it on every package,
    // from a different working directory, to prove path resolution too).
    int RunSelfTest() {
        int failures = 0;
        auto report = [&](bool ok, bool required, const std::string& what) {
            std::printf("[%s] %s\n", ok ? "OK  " : (required ? "FAIL" : "INFO"), what.c_str());
            if (!ok && required) ++failures;
        };
        std::printf("ExoSim %s self-test\nWorking directory: %s\n", EXOSIM_VERSION,
                    std::filesystem::current_path().string().c_str());

        report(std::filesystem::exists("shaders/crt.frag"), true, "shaders/crt.frag present");

        std::vector<PlanetRecord> planets;
        bool loaded = Dataset::LoadFromFile("data/exoplanets.json", planets) && !planets.empty();
        report(loaded, true, "data/exoplanets.json loads: " + std::to_string(planets.size()) + " drawable planets");

        std::vector<PlanetRecord> dwarfs;
        Dataset::LoadDwarfPlanets("data/dwarf_planets.json", dwarfs);
        report(!dwarfs.empty(), false, "data/dwarf_planets.json: " + std::to_string(dwarfs.size()) + " dwarf planets");
        auto stars = Dataset::LoadStarCatalog("data/star_catalog.json");
        report(!stars.empty(), false, "data/star_catalog.json: " + std::to_string(stars.size()) + " stars");
        report(std::filesystem::exists("data/stellar_hosts.json"), false, "data/stellar_hosts.json present");
        report(std::filesystem::exists("data/double_stars.json"), false, "data/double_stars.json present");

        // End-to-end core check: plan the Mars mission from Sol's built-in data.
        auto sol = Dataset::DemoSolarSystem();
        const OrbitalElements* earth = nullptr; const OrbitalElements* mars = nullptr;
        for (const auto& p : sol) {
            if (p.name == "Earth") earth = &p.elements;
            if (p.name == "Mars") mars = &p.elements;
        }
        MissionPlan plan = (earth && mars) ? MissionPlanner::PlanHohmann(*earth, *mars, MissionPlanner::NowDaysSinceJ2000(), "Mars")
                                           : MissionPlan{};
        report(plan.valid, true, plan.valid ? "Mission planner: next Mars window " + MissionPlanner::DayToCalendar(plan.launchDay)
                                            : std::string("Mission planner: no Mars window found"));

        std::printf("Self-test %s\n", failures ? "FAILED" : "PASSED");
        return failures ? 1 : 0;
    }
}

int main(int argc, char** argv) {
    // Load assets relative to the exe, not the launch directory.
    std::filesystem::path exeDir = ExecutableDirectory();
    if (!exeDir.empty()) {
        std::error_code ec;
        std::filesystem::current_path(exeDir, ec);
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) { std::printf("ExoSim %s\n", EXOSIM_VERSION); return 0; }
        if (std::strcmp(argv[i], "--selftest") == 0) return RunSelfTest();
        if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("ExoSim %s\n  --version   print version and exit\n  --selftest  verify the install without opening a window\n", EXOSIM_VERSION);
            return 0;
        }
    }

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
    // Our own solar system is always included, alongside whatever the
    // Archive fetch produced, as a real-data reference point - see the
    // isHome flag below and its priority placement in the system list.
    std::vector<PlanetRecord> planets;
    bool loadedRealArchiveData = Dataset::LoadFromFile("data/exoplanets.json", planets) && !planets.empty();
    if (!loadedRealArchiveData) {
        std::cout << "[main] No archive data found - using the built-in solar system only.\n";
        planets = Dataset::DemoSolarSystem();
    } else {
        std::vector<PlanetRecord> sol = Dataset::DemoSolarSystem();
        planets.insert(planets.end(), sol.begin(), sol.end());
    }

    // Real dwarf-planet data from NASA/JPL's SBDB - a different NASA
    // source from the Exoplanet Archive above (see Dataset::LoadDwarfPlanets).
    // Called before GroupByHost so these join the "Sun" system automatically.
    Dataset::LoadDwarfPlanets("data/dwarf_planets.json", planets);

    std::vector<KeplerOrbit> orbits;
    orbits.reserve(planets.size());
    for (auto& p : planets) orbits.emplace_back(p.elements);

    std::vector<StarSystem> systems = Dataset::GroupByHost(planets);
    for (auto& sys : systems) if (sys.hostName == "Sun") sys.isHome = true;
    // Home system always leads the list, ahead of the usual
    // most-planets-first ordering GroupByHost already applied.
    std::stable_sort(systems.begin(), systems.end(),
        [](const StarSystem& a, const StarSystem& b) { return a.isHome && !b.isHome; });

    Dataset::LoadStellarHosts("data/stellar_hosts.json", systems);

    std::cout << "[main] Grouped " << planets.size() << " planets into "
              << systems.size() << " star systems.\n";

    // Real Hipparcos star catalog (CDS VizieR TAP - a third data source).
    // Kept in memory so the visible sky can be recomputed for:
    //  - the OBSERVER's position: from an exoplanet system, stars with a
    //    real Hipparcos parallax are placed in 3D (distance = 1000/parallax
    //    pc) and re-projected from that system's real position (host
    //    RA/Dec + sy_dist), with brightness corrected by the inverse-square
    //    law (magnitude + 5*log10(newDist/oldDist)). Nearby constellations
    //    genuinely distort, and our own Sun appears as a star when close
    //    enough to be visible. Stars without a usable parallax stay fixed
    //    (treated as infinitely distant).
    //  - the EPOCH: real proper motions shift stars over +-100,000 years
    //    (linear approximation - ignores radial velocity/perspective
    //    acceleration, which matters over very long spans for nearby stars).
    std::vector<StarCatalogEntry> starCatalog = Dataset::LoadStarCatalog("data/star_catalog.json");
    float skyEpochOffsetYears = 0.0f;
    int visibleStarCount = 0;
    bool sunVisibleInSky = false;
    double sunApparentVmag = 0.0;

    auto rebuildStarField = [&](const StarSystem& sys) {
        glm::dvec3 observer(0.0);
        bool fromSun = sys.isHome || sys.distPc <= 0.0;
        if (!fromSun) observer = glm::dvec3(raDecToDirection(sys.hostRaDeg, sys.hostDecDeg)) * sys.distPc;

        std::vector<glm::vec3> dirs, colors;
        std::vector<float> sizes;
        dirs.reserve(starCatalog.size()); colors.reserve(starCatalog.size()); sizes.reserve(starCatalog.size());
        // Hipparcos positions are at catalog epoch J1991.25, so proper motion
        // is applied from 1991.25 to the current date, plus the slider offset.
        const double yearsSinceCatalogEpoch = 8.75 + MissionPlanner::NowDaysSinceJ2000() / 365.25;
        const double dtYears = yearsSinceCatalogEpoch + skyEpochOffsetYears;

        for (const auto& st : starCatalog) {
            double cosDec = std::cos(glm::radians(st.decDeg));
            double ra = st.raDeg + (cosDec > 1e-6 ? st.pmRaMasYr * dtYears / 3.6e6 / cosDec : 0.0);
            double dec = std::clamp(st.decDeg + st.pmDecMasYr * dtYears / 3.6e6, -90.0, 90.0);
            glm::dvec3 dir(raDecToDirection(ra, dec));
            double vmag = st.vmag;
            if (st.parallaxMas > 0.5 && !fromSun) { // > 0.5 mas: within ~2 kpc, parallax meaningful
                double dPc = 1000.0 / st.parallaxMas;
                glm::dvec3 rel = dir * dPc - observer;
                double dNew = glm::length(rel);
                if (dNew < 0.05) continue; // this is the observer's own host star
                dir = rel / dNew;
                vmag += 5.0 * std::log10(dNew / dPc);
            }
            if (vmag > 7.5) continue; // too faint to see from here
            dirs.push_back(glm::vec3(dir));
            colors.push_back(bvToColor(st.bv) * vmagToBrightness(vmag));
            sizes.push_back(vmagToPointSize(vmag));
        }

        sunVisibleInSky = false;
        if (!fromSun) {
            // The Sun (absolute magnitude 4.83) seen from this system.
            double d = glm::length(observer);
            sunApparentVmag = 4.83 + 5.0 * std::log10(d / 10.0);
            if (sunApparentVmag < 7.5) {
                sunVisibleInSky = true;
                dirs.push_back(glm::vec3(-observer / d));
                colors.push_back(bvToColor(0.65) * vmagToBrightness(sunApparentVmag) * 1.3f);
                sizes.push_back(vmagToPointSize(sunApparentVmag) + 1.5f);
            }
        }
        visibleStarCount = static_cast<int>(dirs.size());
        renderer.setStarCatalog(dirs, colors, sizes);
    };

    int currentSystemIndex = 0;
    int selectedPlanetLocal = -1; // index within systems[currentSystemIndex].planetIndices
    bool logScaleEnabled = false;
    float starSizeExaggeration = 30.0f; // real AU-scale stars are near-invisible next to orbits

    char searchBuffer[128] = "";
    int minPlanetsFilter = 1;

    // Planet detail view state.
    size_t detailPlanetGlobalIndex = 0;
    PlanetClassification detailClassification;
    PlanetAppearance detailAppearance; // isKnown=false unless this is one of our own 8 Solar System planets
    float planetSpinAngle = 0.0f;
    float detailSurfaceSeed = 0.0f; // varies the procedural pattern per planet
    std::vector<MoonRecord> detailMoons;
    bool detailMoonsAreReal = false; // true only for the built-in demo solar system
    int detailMoonIndex = -1;        // >= 0: the Detail view is inspecting detailMoons[this], not the planet

    // Mission planner state (Sol only).
    struct MissionTarget { std::string label; size_t planetIdx; bool isEarthMoon; };
    std::vector<MissionTarget> missionTargets;
    int missionTargetChoice = 0;
    int missionVehicleChoice = 0;
    bool missionActive = false;
    MissionPlan activeMission;

    // Speculative moons are deterministic from the planet's name, but are
    // cached after first generation (rather than recomputed every click)
    // so they behave like stable, inspectable data for the rest of the
    // session instead of being silently regenerated each time.
    std::unordered_map<size_t, std::vector<MoonRecord>> speculativeMoonCache;
    auto getMoonsForPlanet = [&](size_t pIdx) -> const std::vector<MoonRecord>& {
        if (!planets[pIdx].moons.empty()) return planets[pIdx].moons;
        auto it = speculativeMoonCache.find(pIdx);
        if (it != speculativeMoonCache.end()) return it->second;
        float seed = 0.0f;
        for (char ch : planets[pIdx].name) seed += static_cast<float>(static_cast<unsigned char>(ch));
        PlanetClassification cls = PlanetClassifier::Classify(planets[pIdx]);
        auto inserted = speculativeMoonCache.emplace(pIdx, generateSpeculativeMoons(cls.type, seed));
        return inserted.first->second;
    };

    // Current system's star (recomputed whenever the selected system changes).
    StarClassification currentStar;
    float starDisplayRadius = 0.05f; // in the same display-space units as orbit rings

    // Companion stars for the current system (empty unless numStars > 1).
    // Position is always schematic (see StarComponent's comment) - the
    // Archive has no bulk separation field, so these are arranged evenly
    // around the primary rather than placed at any claimed real distance.
    // They also slowly orbit the primary - real binaries DO orbit each
    // other, but we have no real separation or period to animate from, so
    // this uses a fixed illustrative angular speed. It exists purely so a
    // multi-star system doesn't look like a frozen diagram; it is not a
    // simulation of the real orbital dynamics.
    struct CompanionVisual {
        float orbitRadius;
        float baseAngle;
        bool realSeparation = false; // true: orbitRadius from a real WDS projected separation
        double separationAU = 0.0;
        double periodDays = 0.0;     // Kepler's 3rd law, only when realSeparation
        glm::vec3 color;
        float displayRadius;
        bool hasData;
        std::string label;
    };
    std::vector<CompanionVisual> currentCompanions;

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

        // Companion stars: real temperature/mass/radius where the Archive
        // has resolved them (see StarComponent), but ALWAYS a schematic
        // position - evenly spaced around the primary at a distance
        // that's clearly separated but never overruns the innermost
        // orbit. This is a visualization of "a companion exists here",
        // not a claim about its real physical separation.
        currentCompanions.clear();
        int numCompanions = static_cast<int>(sys.companions.size());
        if (numCompanions > 0) {
            double offsetRadius = std::min(minDisplay * 0.5, static_cast<double>(starDisplayRadius) * 8.0);
            offsetRadius = std::max(offsetRadius, static_cast<double>(starDisplayRadius) * 2.5);
            for (int i = 0; i < numCompanions; ++i) {
                double angle = 2.0 * 3.14159265358979323846 * i / numCompanions;
                CompanionVisual cv;
                cv.orbitRadius = static_cast<float>(offsetRadius);
                cv.baseAngle = static_cast<float>(angle);
                const StarComponent& comp = sys.companions[i];
                if (comp.projectedSepAU > 0.0) {
                    // Real projected separation (WDS). Period from Kepler's
                    // 3rd law, P[yr] = sqrt(a[AU]^3 / (M1+M2)[Msun]), using the
                    // projected separation as a - a lower bound on the true
                    // separation, so the period is a lower bound too.
                    cv.realSeparation = true;
                    cv.separationAU = comp.projectedSepAU;
                    cv.orbitRadius = static_cast<float>(displayMagnitude(comp.projectedSepAU, logScaleEnabled));
                    double m1 = sys.starMassSolar > 0.0 ? sys.starMassSolar : 1.0;
                    double m2 = comp.massSolar > 0.0 ? comp.massSolar : 0.0;
                    double a = comp.projectedSepAU;
                    cv.periodDays = std::sqrt(a * a * a / (m1 + m2)) * 365.25;
                }
                if (comp.hasData) {
                    StarClassification compCls = StarClassifier::Classify(comp.teffK, comp.radiusSolar, comp.spectralType);
                    cv.color = compCls.color;
                    double compRealAU = compCls.displayRadiusSolar * kSolarRadiusAU;
                    cv.displayRadius = static_cast<float>(std::min(compRealAU * starSizeExaggeration, minDisplay * 0.3));
                    cv.displayRadius = std::max(cv.displayRadius, starDisplayRadius * 0.3f);
                    cv.hasData = true;
                    cv.label = compCls.spectralClass;
                } else {
                    cv.color = glm::vec3(0.55f, 0.55f, 0.6f);
                    cv.displayRadius = starDisplayRadius * 0.6f;
                    cv.hasData = false;
                    cv.label = "Unknown companion (no resolved stellar data)";
                }
                currentCompanions.push_back(cv);
            }
        }

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

        rebuildStarField(sys); // the sky as seen from THIS system
    };

    selectSystem(currentSystemIndex, /*snap=*/true);

    float simSpeedDaysPerSecond = 5.0f;
    // Day 0 = J2000 (2000-01-01 12:00). Sol's planets use real J2000
    // elements, so starting at today's real date puts them where they
    // actually are right now. (Exoplanet orbital phases are unknown, so
    // their positions are illustrative regardless of date.)
    double simTimeDays = MissionPlanner::NowDaysSinceJ2000();

    // Mission targets: every Sol planet (except Earth) and every real moon.
    for (const auto& sys : systems) {
        if (!sys.isHome) continue;
        for (size_t pIdx : sys.planetIndices) {
            if (planets[pIdx].name != "Earth") missionTargets.push_back({planets[pIdx].name, pIdx, false});
            for (const auto& m : planets[pIdx].moons) {
                bool earthMoon = planets[pIdx].name == "Earth";
                missionTargets.push_back({m.name + " (moon of " + planets[pIdx].name + ")", pIdx, earthMoon});
            }
        }
    }

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

            glm::mat4 viewRotOnly = glm::mat4(glm::mat3(view));
            glm::mat4 viewProjRotFwd = proj * viewRotOnly;
            glm::mat4 invViewProjRot = glm::inverse(viewProjRotFwd);
            renderer.renderSkybox(invViewProjRot, static_cast<float>(now),
                static_cast<float>(currentSystem.galacticLatDeg), static_cast<float>(currentSystem.galacticLonDeg),
                hashSeedFromString(currentSystem.hostName));
            renderer.renderStarCatalog(viewProjRotFwd);

            std::vector<glm::vec3> positions(currentSystem.planetIndices.size());
            for (size_t local = 0; local < currentSystem.planetIndices.size(); ++local) {
                size_t pIdx = currentSystem.planetIndices[local];
                glm::dvec3 pos = orbits[pIdx].positionAtTime(simTimeDays);
                positions[local] = toDisplaySpace(pos, logScaleEnabled);
            }

            renderer.render(viewProj, positions);

            glm::mat4 starModel = glm::scale(glm::mat4(1.0f), glm::vec3(starDisplayRadius));
            renderer.renderStar(viewProj, starModel, currentStar.color);

            for (const auto& comp : currentCompanions) {
                // Stylized orbital speed (not physically derived - see
                // CompanionVisual's comment). Slower for larger schematic
                // orbits so a system with multiple companions doesn't look
                // frantic.
                float angle = comp.realSeparation && comp.periodDays > 0.0
                    ? comp.baseAngle + static_cast<float>(2.0 * 3.14159265358979323846 * std::fmod(simTimeDays / comp.periodDays, 1.0))
                    : comp.baseAngle + static_cast<float>(now) * (0.15f / comp.orbitRadius);
                glm::vec3 compPos(comp.orbitRadius * std::cos(angle), 0.0f, comp.orbitRadius * std::sin(angle));
                glm::mat4 compModel = glm::translate(glm::mat4(1.0f), compPos)
                                     * glm::scale(glm::mat4(1.0f), glm::vec3(comp.displayRadius));
                renderer.renderStar(viewProj, compModel, comp.color);
            }

            for (const auto& gr : guideRings) {
                renderer.renderAuxiliaryRing(viewProj, gr.points, glm::vec3(0.5f, 0.5f, 0.55f), 0.25f);
            }

            // Mission trajectory (Sol only; Earth-Moon transfers are far too
            // small to see at AU scale, so those are HUD-only).
            if (missionActive && currentSystem.isHome && !activeMission.earthMoonMission) {
                KeplerOrbit transfer(activeMission.transferElements);
                std::vector<glm::vec3> arc;
                constexpr int kArcSegs = 128;
                for (int k = 0; k <= kArcSegs; ++k)
                    arc.push_back(toDisplaySpace(transfer.positionAtTime(activeMission.transferDays * k / kArcSegs), logScaleEnabled));
                for (int k = kArcSegs; k >= 0; --k) arc.push_back(arc[k]); // retrace so the line loop doesn't draw a closing chord
                renderer.renderAuxiliaryRing(viewProj, arc, glm::vec3(1.0f, 0.65f, 0.15f), 0.45f);

                double tRel = std::clamp(simTimeDays - activeMission.launchDay, 0.0, activeMission.transferDays);
                glm::vec3 craft = toDisplaySpace(transfer.positionAtTime(tRel), logScaleEnabled);
                glm::vec3 ahead = toDisplaySpace(transfer.positionAtTime(std::min(tRel + 1.0, activeMission.transferDays)), logScaleEnabled);
                glm::vec3 dir = glm::length(ahead - craft) > 1e-6f ? glm::normalize(ahead - craft) : glm::vec3(1, 0, 0);
                glm::vec3 side = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)) + glm::vec3(1e-6f));
                float sz = g_cam.systemDistance * 0.018f; // constant on-screen size
                // Wireframe rocket glyph: nose cone, body, fins.
                std::vector<glm::vec3> glyph = {
                    craft + dir * sz * 1.2f,
                    craft + dir * sz * 0.5f + side * sz * 0.25f,
                    craft - dir * sz * 0.8f + side * sz * 0.25f,
                    craft - dir * sz * 1.2f + side * sz * 0.6f,
                    craft - dir * sz * 1.0f,
                    craft - dir * sz * 1.2f - side * sz * 0.6f,
                    craft - dir * sz * 0.8f - side * sz * 0.25f,
                    craft + dir * sz * 0.5f - side * sz * 0.25f,
                };
                renderer.renderAuxiliaryRing(viewProj, glyph, glm::vec3(1.0f, 0.9f, 0.5f), 1.0f);
            }
        } else {
            eye = orbitCameraEye(g_cam.planetDistance);
            view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
            proj = glm::perspective(glm::radians(45.0f), (float)g_width / (float)g_height, 0.01f, 100.0f);
            glm::mat4 viewProj = proj * view;

            glm::mat4 viewRotOnly = glm::mat4(glm::mat3(view));
            glm::mat4 viewProjRotFwd = proj * viewRotOnly;
            glm::mat4 invViewProjRot = glm::inverse(viewProjRotFwd);
            const StarSystem& detailSystem = systems[currentSystemIndex];
            renderer.renderSkybox(invViewProjRot, static_cast<float>(now),
                static_cast<float>(detailSystem.galacticLatDeg), static_cast<float>(detailSystem.galacticLonDeg),
                hashSeedFromString(detailSystem.hostName));
            renderer.renderStarCatalog(viewProjRotFwd);

            // Real axial tilt (e.g. Uranus rolling on its side) for known
            // Solar System bodies; untilted for generic exoplanets, since
            // we have no real obliquity data for those.
            glm::mat4 tilt = detailAppearance.isKnown
                ? glm::rotate(glm::mat4(1.0f), glm::radians(detailAppearance.axialTiltDeg), glm::vec3(0.0f, 0.0f, 1.0f))
                : glm::mat4(1.0f);
            glm::mat4 spin = glm::rotate(glm::mat4(1.0f), planetSpinAngle, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 model = tilt * spin;
            const glm::vec3 phosphorGreen(0.15f, 0.95f, 0.55f); // matches the orbit-ring accent color

            // Zoom-driven detail: each halving of the distance to the surface
            // adds one fractal octave (5 at a comfortable distance, up to 11
            // right at the surface) - this is what keeps close-ups sharp.
            float surfaceGap = std::max(g_cam.planetDistance - 1.0f, 0.01f);
            renderer.setSurfaceDetail(std::clamp(5.0f + std::log2(2.0f / surfaceGap), 5.0f, 11.0f));

            bool inspectingMoon = detailMoonIndex >= 0 && detailMoonIndex < static_cast<int>(detailMoons.size());
            if (inspectingMoon) {
                const MoonRecord& moon = detailMoons[detailMoonIndex];
                PlanetAppearance moonLook = moon.isReal ? SolarSystemAppearance::Lookup(moon.name) : PlanetAppearance{};
                glm::mat4 moonTilt = moonLook.isKnown
                    ? glm::rotate(glm::mat4(1.0f), glm::radians(moonLook.axialTiltDeg), glm::vec3(0.0f, 0.0f, 1.0f))
                    : glm::mat4(1.0f);
                glm::mat4 moonModelMain = moonTilt * spin;
                renderer.setSurfaceEqTemp(0.0f);
                // Rocky/icy generic look for moons without a real appearance
                // (speculative exomoons): illustrative only, labeled in the HUD.
                renderer.renderPlanetSurface(viewProj, moonModelMain, eye, 1, glm::vec3(0.62f, 0.60f, 0.58f), phosphorGreen,
                                              static_cast<float>(now), detailSurfaceSeed + static_cast<float>(detailMoonIndex) * 7.0f,
                                              moonLook.isKnown ? &moonLook : nullptr);
                renderer.renderPlanetDetail(viewProj, glm::scale(moonModelMain, glm::vec3(1.01f)), phosphorGreen * 0.8f);
            } else {
            renderer.setSurfaceEqTemp(static_cast<float>(planets[detailPlanetGlobalIndex].eqTempK));
            renderer.renderPlanetSurface(viewProj, model, eye, static_cast<int>(detailClassification.type),
                                          detailClassification.displayColor, phosphorGreen,
                                          static_cast<float>(now), detailSurfaceSeed,
                                          detailAppearance.isKnown ? &detailAppearance : nullptr);

            // Wireframe targeting-grid overlay, very slightly inflated so
            // it doesn't z-fight with the solid surface underneath.
            glm::mat4 gridModel = glm::scale(model, glm::vec3(1.01f));
            renderer.renderPlanetDetail(viewProj, gridModel, phosphorGreen * 0.8f);

            // Rings (Saturn, Uranus) - drawn in the SAME tilted plane as
            // the planet (not spinning with it, since rings are
            // azimuthally uniform and spin wouldn't be visible anyway).
            if (detailAppearance.isKnown && detailAppearance.hasRings) {
                renderer.renderPlanetRings(viewProj, tilt, detailAppearance.ringBands,
                    detailAppearance.ringColor, detailSurfaceSeed);
            }

            // Moons: real ones for the demo solar system, otherwise
            // speculative/illustrative ones (see generateSpeculativeMoons).
            // Distances are compressed with a log curve into a viewable
            // range - real moon distances (tens of planet-radii) would
            // otherwise put them far outside the camera's usual zoom range.
            // Positions are propagated with the same KeplerOrbit math used
            // for planets, so inclination and eccentricity genuinely tilt
            // and distort each moon's path rather than a flat circle.
            renderer.setSurfaceDetail(5.0f); // moons here are small on screen
            renderer.setSurfaceEqTemp(0.0f);
            for (size_t mi = 0; mi < detailMoons.size(); ++mi) {
                const MoonRecord& moon = detailMoons[mi];
                float dispR = 1.3f + 0.9f * std::log10(1.0f + static_cast<float>(moon.orbitRadiusPlanetRadii));

                OrbitalElements moonElements;
                moonElements.semiMajorAxisAU = dispR; // repurposed as a display-space distance, not real AU
                moonElements.eccentricity = moon.eccentricity;
                moonElements.inclinationDeg = moon.inclinationDeg;
                moonElements.argPeriapsisDeg = moon.argPeriapsisDeg;
                moonElements.longAscNodeDeg = moon.longAscNodeDeg;
                moonElements.periodDays = std::max(moon.orbitPeriodDays, 0.1);
                moonElements.meanAnomalyAtEpochDeg = static_cast<double>(mi) * 61.0 + detailSurfaceSeed;
                KeplerOrbit moonOrbit(moonElements);

                double fastDays = static_cast<double>(now) * 15.0; // stylized playback speed, not real-time
                glm::vec3 moonPos(eclipticToWorld(moonOrbit.positionAtTime(fastDays)));

                glm::mat4 moonModel = glm::translate(glm::mat4(1.0f), moonPos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.12f));

                PlanetAppearance moonAppearance = moon.isReal ? SolarSystemAppearance::Lookup(moon.name) : PlanetAppearance{};
                if (moonAppearance.isKnown) {
                    // Real, well-photographed moon (Io, Europa, Titan, etc.)
                    // - same solid-surface + wireframe-grid pipeline as planets.
                    float moonSeed = detailSurfaceSeed + static_cast<float>(mi) * 7.0f;
                    renderer.renderPlanetSurface(viewProj, moonModel, eye, 0,
                                                  moonAppearance.primaryColor, phosphorGreen,
                                                  static_cast<float>(now), moonSeed, &moonAppearance);
                    glm::mat4 moonGridModel = glm::scale(moonModel, glm::vec3(1.03f));
                    renderer.renderPlanetDetail(viewProj, moonGridModel, phosphorGreen * 0.7f, 0.4f);
                } else {
                    // Speculative moon, or a real one we haven't hand-modeled
                    // yet - plain wireframe dot, dimmer to signal lower certainty.
                    glm::vec3 moonColor = moon.isReal ? glm::vec3(0.8f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.65f);
                    float moonAlpha = moon.isReal ? 0.7f : 0.4f;
                    renderer.renderPlanetDetail(viewProj, moonModel, moonColor, moonAlpha);
                }

                // Orbit path guide, sampled across a full period so it
                // traces the real tilted/eccentric ellipse, not a flat circle.
                glm::vec3 guideColor = moon.isReal ? glm::vec3(0.8f, 0.85f, 0.95f) : glm::vec3(0.6f, 0.6f, 0.65f);
                float guideAlpha = moon.isReal ? 0.7f : 0.4f;
                std::vector<glm::vec3> orbitPts;
                constexpr int kOrbitSegs = 64;
                for (int s = 0; s <= kOrbitSegs; ++s) {
                    double t = moonElements.periodDays * s / kOrbitSegs;
                    orbitPts.emplace_back(eclipticToWorld(moonOrbit.positionAtTime(t)));
                }
                renderer.renderAuxiliaryRing(viewProj, orbitPts, guideColor, guideAlpha * 0.4f);
            }
            } // end: planet (not moon) inspection
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
            ImGui::Text("Sim date: %s", MissionPlanner::DayToCalendar(simTimeDays).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Today")) simTimeDays = MissionPlanner::NowDaysSinceJ2000();
            ImGui::TextDisabled(currentSystem.isHome ? "(Sol: real J2000-based planet positions)"
                                                     : "(exoplanet orbital phases are unknown - positions illustrative)");
            if (ImGui::SliderFloat("Sky epoch (years from now)", &skyEpochOffsetYears, -100000.0f, 100000.0f, "%.0f")) {
                rebuildStarField(currentSystem);
            }
            ImGui::TextDisabled("(real Hipparcos proper motions; linear approximation)");
            ImGui::Text("Visible stars from here: %d", visibleStarCount);
            if (!currentSystem.isHome && currentSystem.distPc > 0.0) {
                ImGui::TextDisabled("Sky recomputed from %.1f pc away via real parallaxes.", currentSystem.distPc);
                if (sunVisibleInSky) ImGui::Text("Our Sun is visible from here (mag %.1f).", sunApparentVmag);
                else ImGui::TextDisabled("Our Sun is too faint to see from here (mag %.1f).", sunApparentVmag);
            }
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
                                "Host star: %s%s", currentStar.spectralClass.c_str(),
                                currentSystem.isHome ? "  [SOL - HOME SYSTEM]" : "");
            if (currentStar.hasMeasuredRadius) {
                ImGui::Text("Radius: %.2f solar radii (measured)", currentStar.displayRadiusSolar);
            } else {
                ImGui::TextDisabled("Radius: not measured (assumed Sun-like, 1.0 solar radii)");
            }
            if (currentSystem.numStars > 1) {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Multi-star system (%d stars).", currentSystem.numStars);
                ImGui::TextDisabled("Companion position and orbital motion shown are schematic, not");
                ImGui::TextDisabled("a real separation or period - the Archive has no bulk field for");
                ImGui::TextDisabled("either. Color/type below IS real where the Archive resolved it.");
                for (const auto& comp : currentCompanions) {
                    ImGui::TextColored(ImVec4(comp.color.r, comp.color.g, comp.color.b, 1.0f),
                        "  Companion: %s", comp.label.c_str());
                    if (comp.realSeparation) {
                        ImGui::Text("    Projected separation: %.0f AU (real, WDS; a lower bound)", comp.separationAU);
                        ImGui::Text("    Orbital period: >= ~%.0f years (Kepler's 3rd law)", comp.periodDays / 365.25);
                        if (!logScaleEnabled && comp.orbitRadius > g_cam.systemDistance)
                            ImGui::TextDisabled("    (off-screen at true scale - zoom out or enable log scale)");
                    } else {
                        ImGui::TextDisabled("    Separation unknown - position/motion schematic.");
                    }
                }
            }
            ImGui::Separator();

            ImGui::Text("Find a star system:");
            ImGui::InputTextWithHint("##search", "Search host star name...", searchBuffer, sizeof(searchBuffer));
            ImGui::SliderInt("Min planets in system", &minPlanetsFilter, 1, 8);

            ImGui::BeginChild("SystemList", ImVec2(0, 140), true);
            std::string search(searchBuffer);
            for (int i = 0; i < (int)systems.size(); ++i) {
                if ((int)systems[i].planetIndices.size() < minPlanetsFilter) continue;
                std::string searchableName = systems[i].hostName + (systems[i].isHome ? " Sol Home" : "");
                if (!containsCaseInsensitive(searchableName, search)) continue;

                std::string displayName = systems[i].isHome ? "Sol (Home System)" : systems[i].hostName;
                std::string label = displayName + " ("
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
            ImGui::Text("Planets in %s:", currentSystem.isHome ? "Sol (Home System)" : currentSystem.hostName.c_str());
            for (int local = 0; local < (int)currentSystem.planetIndices.size(); ++local) {
                size_t pIdx = currentSystem.planetIndices[local];
                const auto& moonsForRow = getMoonsForPlanet(pIdx);
                std::string rowLabel = planets[pIdx].name;
                if (!moonsForRow.empty()) {
                    bool rowMoonsReal = !planets[pIdx].moons.empty();
                    rowLabel += " (" + std::to_string(moonsForRow.size())
                              + (rowMoonsReal ? " moons)" : " candidate moons)");
                }
                if (ImGui::Selectable(rowLabel.c_str(), selectedPlanetLocal == local)) {
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
                    detailAppearance = SolarSystemAppearance::Lookup(p.name);
                    detailSurfaceSeed = 0.0f;
                    for (char ch : p.name) detailSurfaceSeed += static_cast<float>(static_cast<unsigned char>(ch));
                    detailMoons = getMoonsForPlanet(pIdx);
                    detailMoonsAreReal = !p.moons.empty();
                    detailMoonIndex = -1;
                    g_viewMode = ViewMode::PlanetDetail;
                }
                const auto& moonsHere = getMoonsForPlanet(pIdx);
                for (int mi = 0; mi < static_cast<int>(moonsHere.size()); ++mi) {
                    std::string lbl = "Inspect " + moonsHere[mi].name + (moonsHere[mi].isReal ? "" : " (speculative)") + "##m" + std::to_string(mi);
                    if (ImGui::SmallButton(lbl.c_str())) {
                        detailPlanetGlobalIndex = pIdx;
                        detailClassification = PlanetClassifier::Classify(p);
                        detailAppearance = SolarSystemAppearance::Lookup(p.name);
                        detailSurfaceSeed = hashSeedFromString(p.name);
                        detailMoons = moonsHere;
                        detailMoonsAreReal = !p.moons.empty();
                        detailMoonIndex = mi;
                        g_viewMode = ViewMode::PlanetDetail;
                    }
                }
            }
            ImGui::End();

            // --- Mission planner (Sol only) ---
            if (currentSystem.isHome && !missionTargets.empty()) {
                ImGui::Begin("Mission Planner");
                const char* vehicles[] = {"Falcon Heavy (SpaceX)", "SLS Block 1 (Artemis)"};
                ImGui::Combo("Launch vehicle", &missionVehicleChoice, vehicles, 2);
                if (ImGui::BeginCombo("Destination", missionTargets[missionTargetChoice].label.c_str())) {
                    for (int ti = 0; ti < static_cast<int>(missionTargets.size()); ++ti)
                        if (ImGui::Selectable(missionTargets[ti].label.c_str(), ti == missionTargetChoice)) missionTargetChoice = ti;
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Plan next launch window & run simulation")) {
                    const MissionTarget& mt = missionTargets[missionTargetChoice];
                    if (mt.isEarthMoon) {
                        activeMission = MissionPlanner::PlanEarthMoon(simTimeDays);
                    } else {
                        const OrbitalElements* earthEl = nullptr;
                        for (size_t pIdx : currentSystem.planetIndices)
                            if (planets[pIdx].name == "Earth") earthEl = &planets[pIdx].elements;
                        if (earthEl) activeMission = MissionPlanner::PlanHohmann(*earthEl, planets[mt.planetIdx].elements,
                                                                                  simTimeDays, planets[mt.planetIdx].name);
                        activeMission.targetName = mt.label;
                    }
                    missionActive = activeMission.valid;
                }
                if (missionActive) {
                    ImGui::SameLine();
                    if (ImGui::Button("Jump to launch")) simTimeDays = activeMission.launchDay;
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) missionActive = false;
                }

                if (missionActive) {
                    const MissionPlan& m = activeMission;
                    ImGui::Separator();
                    ImGui::Text("Target: %s", m.targetName.c_str());
                    ImGui::Text("Optimal launch: %s", MissionPlanner::DayToCalendar(m.launchDay).c_str());
                    ImGui::Text("Arrival:        %s", MissionPlanner::DayToCalendar(m.arrivalDay).c_str());
                    if (m.transferDays < 60.0) ImGui::Text("Transfer time: %.1f days", m.transferDays);
                    else ImGui::Text("Transfer time: %.0f days (%.1f years)", m.transferDays, m.transferDays / 365.25);
                    if (m.synodicPeriodDays > 0.0)
                        ImGui::Text("Windows repeat every ~%.0f days (synodic period)", m.synodicPeriodDays);
                    ImGui::Text("Departure burn from 200 km LEO: %.2f km/s", m.dvFromLeoKms);
                    ImGui::Text("Arrival excess speed: %.2f km/s (capture burn not included)", m.vInfArrivalKms);

                    if (simTimeDays < m.launchDay)
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "T-minus %.1f days to launch", m.launchDay - simTimeDays);
                    else if (simTimeDays < m.arrivalDay)
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "In flight: %.0f%% - ETA %.1f days",
                                           100.0 * (simTimeDays - m.launchDay) / m.transferDays, m.arrivalDay - simTimeDays);
                    else
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Arrived.");

                    ImGui::Separator();
                    if (missionVehicleChoice == 0) {
                        ImGui::TextWrapped("Falcon Heavy published capacity (SpaceX, fully expendable): "
                                           "63,800 kg to LEO, 16,800 kg to Mars, 3,500 kg to Pluto. "
                                           "It launched NASA's Europa Clipper in Oct 2024.");
                    } else {
                        ImGui::TextWrapped("SLS Block 1 (NASA): ~27,000 kg to trans-lunar injection. "
                                           "Artemis is a LUNAR program - Artemis I (2022) reached the Moon "
                                           "in ~5 days. No SLS planetary mission is currently planned, so "
                                           "non-lunar destinations here are hypothetical.");
                    }
                    ImGui::TextDisabled("Published payload figures only exist for some destinations - none are");
                    ImGui::TextDisabled("invented here. Trajectory time/energy don't depend on the vehicle.");
                    ImGui::Separator();
                    ImGui::TextWrapped("Idealized minimum-energy (Hohmann) transfer from real orbital "
                                       "elements. Real outer-planet missions use gravity assists to "
                                       "arrive far sooner (Voyager 2: Neptune in ~12 yr; New Horizons: "
                                       "Pluto in ~9.5 yr). Planet inclinations are ignored in the transfer.");
                    if (m.earthMoonMission)
                        ImGui::TextDisabled("Earth-Moon transfer is too small to draw at AU scale.");
                    if (m.arrivalBodyName != m.targetName && !m.earthMoonMission)
                        ImGui::TextDisabled("Moon targets: arrival in the parent planet's system; the final "
                                            "capture and moon transfer aren't modeled.");
                }
                ImGui::End();
            }
        } else if (detailMoonIndex >= 0 && detailMoonIndex < static_cast<int>(detailMoons.size())) {
            const MoonRecord& moon = detailMoons[detailMoonIndex];
            const auto& parent = planets[detailPlanetGlobalIndex];
            PlanetAppearance moonLook = moon.isReal ? SolarSystemAppearance::Lookup(moon.name) : PlanetAppearance{};
            ImGui::Begin("Moon Detail");
            ImGui::Text("%s  (moon of %s)", moon.name.c_str(), parent.name.c_str());
            ImGui::TextDisabled("Scroll to zoom in - surface detail increases as you get closer.");
            ImGui::Separator();
            if (moon.isReal) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Real moon - orbital data from published values.");
                if (moon.radiusKm > 0.0) ImGui::Text("Mean radius: %.1f km", moon.radiusKm);
                ImGui::Text("Orbital period: %.3f days", moon.orbitPeriodDays);
                ImGui::Text("Orbit radius: %.1f %s radii", moon.orbitRadiusPlanetRadii, parent.name.c_str());
                ImGui::Text("Inclination: %.2f deg   Eccentricity: %.4f", moon.inclinationDeg, moon.eccentricity);
                if (moonLook.isKnown)
                    ImGui::TextWrapped("Colors and feature types reflect this moon's real, spacecraft-imaged "
                                       "appearance; fine surface detail is procedurally generated.");
                else
                    ImGui::TextDisabled("No hand-modeled appearance for this moon yet - generic rocky look.");
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "SPECULATIVE - not a real detection.");
                ImGui::TextWrapped("No exomoon has been confirmed around this planet. This body, its orbit, "
                                   "and its surface are illustrative placeholders only.");
            }
            ImGui::Separator();
            std::string backLbl = "Back to " + parent.name;
            if (ImGui::Button(backLbl.c_str())) detailMoonIndex = -1;
            ImGui::SameLine();
            if (ImGui::Button("Back to System View")) { detailMoonIndex = -1; g_viewMode = ViewMode::System; }
            ImGui::End();
        } else {
            const auto& p = planets[detailPlanetGlobalIndex];
            ImGui::Begin("Planet Detail");
            ImGui::Text("%s", p.name.c_str());
            ImGui::Text("Host star: %s", p.hostStar.c_str());
            ImGui::TextDisabled("Scroll to zoom in on the surface pattern below.");
            ImGui::Separator();

            if (detailAppearance.isKnown) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "KNOWN APPEARANCE (real, well-photographed):");
                ImGui::TextWrapped("Colors and features (bands, continents, polar caps, spots, "
                                    "haze, rings, axial tilt) below reflect this planet's actual "
                                    "documented look, not a statistical guess.");
                ImGui::Separator();
            } else if (detailClassification.hasRealAtmosphereFindings) {
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

            if (detailAppearance.isKnown) {
                ImGui::TextDisabled("The classification/atmosphere lines above are still generated");
                ImGui::TextDisabled("by the same density-based estimator used for exoplanets (it");
                ImGui::TextDisabled("happens to check out here) - but the VISUAL above is real,");
                ImGui::TextDisabled("not estimated, unlike every exoplanet in this app.");
            } else {
                ImGui::TextDisabled("Composition/atmosphere are statistical estimates from");
                ImGui::TextDisabled("radius/mass, not direct measurements - the Archive rarely");
                ImGui::TextDisabled("has spectroscopic data outside a handful of flagship targets.");
                ImGui::TextDisabled("The surface/cloud pattern above is a procedural stylization");
                ImGui::TextDisabled("driven by that classification - not a real image. No exoplanet");
                ImGui::TextDisabled("has ever been directly imaged in this kind of visual detail.");
            }

            ImGui::Separator();
            if (detailMoons.empty()) {
                ImGui::TextDisabled("No moons shown for this planet.");
            } else if (detailMoonsAreReal) {
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Moons (real, approximate known orbits):");
                for (int mi = 0; mi < static_cast<int>(detailMoons.size()); ++mi) {
                    const auto& m = detailMoons[mi];
                    ImGui::BulletText("%s - period ~%.2f days, inclination ~%.1f deg", m.name.c_str(), m.orbitPeriodDays, m.inclinationDeg);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Inspect##dm" + std::to_string(mi)).c_str())) detailMoonIndex = mi;
                }
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "No confirmed moons on record.");
                ImGui::TextDisabled("The Archive doesn't include exomoon detections (only a couple");
                ImGui::TextDisabled("of disputed candidates exist in the literature, system-wide).");
                ImGui::TextDisabled("The moons below are illustrative placeholders only:");
                for (int mi = 0; mi < static_cast<int>(detailMoons.size()); ++mi) {
                    ImGui::BulletText("%s (speculative, not a real detection)", detailMoons[mi].name.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("Inspect##ds" + std::to_string(mi)).c_str())) detailMoonIndex = mi;
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
