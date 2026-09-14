// ExoSim - retro-futurist orbital viewer
//
// Pipeline: load exoplanet orbital elements (from the NASA Exoplanet
// Archive, via data/fetch_exoplanets.py) -> propagate each planet's
// position with KeplerOrbit -> draw wireframe orbits + points with
// Renderer -> composite through CrtPostProcess for the CRT look.

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <iostream>
#include <vector>
#include <cmath>

#include "Dataset.h"
#include "KeplerOrbit.h"
#include "Renderer.h"
#include "CrtPostProcess.h"

namespace {
    int g_width = 1280, g_height = 800;

    void framebufferSizeCallback(GLFWwindow*, int w, int h) {
        g_width = w; g_height = h;
        glViewport(0, 0, w, h);
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

    // --- Load data (real archive export if present, else demo system) ---
    std::vector<PlanetRecord> planets;
    if (!Dataset::LoadFromFile("data/exoplanets.json", planets) || planets.empty()) {
        std::cout << "[main] Falling back to built-in demo solar system.\n";
        planets = Dataset::DemoSolarSystem();
    }

    std::vector<KeplerOrbit> orbits;
    orbits.reserve(planets.size());
    for (auto& p : planets) orbits.emplace_back(p.elements);

    // Precompute each orbit's ring geometry once (ellipses don't move).
    for (size_t i = 0; i < planets.size(); ++i) {
        std::vector<glm::vec3> ring;
        constexpr int kSegments = 180;
        for (int s = 0; s <= kSegments; ++s) {
            double t = planets[i].elements.periodDays * (double)s / kSegments;
            glm::dvec3 pos = orbits[i].positionAtTime(t);
            ring.emplace_back(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
        }
        renderer.setOrbitPath(i, ring);
    }

    float simSpeedDaysPerSecond = 5.0f;
    float simTimeDays = 0.0f;
    int selectedPlanet = -1;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        static double lastTime = glfwGetTime();
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        simTimeDays += dt * simSpeedDaysPerSecond;

        crt.resize(g_width, g_height);

        // --- Camera: simple orbiting view of the whole system ---
        float camAngle = static_cast<float>(now) * 0.05f;
        glm::vec3 eye = glm::vec3(std::cos(camAngle), 0.6f, std::sin(camAngle)) * 40.0f;
        glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                           (float)g_width / (float)g_height, 0.1f, 500.0f);
        glm::mat4 viewProj = proj * view;

        // --- Propagate planet positions for this frame ---
        std::vector<glm::vec3> positions(planets.size());
        for (size_t i = 0; i < planets.size(); ++i) {
            glm::dvec3 pos = orbits[i].positionAtTime(simTimeDays);
            positions[i] = glm::vec3(pos);
        }

        // --- Render scene into offscreen buffer, then CRT-composite ---
        crt.beginScene();
        renderer.render(viewProj, positions);
        crt.endSceneAndPresent(static_cast<float>(now));

        // --- HUD (drawn after the CRT pass, so it stays crisp) ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("ExoSim Control");
        ImGui::Text("Planets loaded: %zu", planets.size());
        ImGui::SliderFloat("Sim speed (days/sec)", &simSpeedDaysPerSecond, 0.0f, 200.0f);
        ImGui::Text("Elapsed sim time: %.1f days", simTimeDays);
        ImGui::Separator();
        ImGui::Text("Select a body:");
        for (int i = 0; i < (int)planets.size() && i < 40; ++i) {
            if (ImGui::Selectable(planets[i].name.c_str(), selectedPlanet == i)) {
                selectedPlanet = i;
            }
        }
        if (selectedPlanet >= 0 && selectedPlanet < (int)planets.size()) {
            const auto& p = planets[selectedPlanet];
            ImGui::Separator();
            ImGui::Text("Host star: %s", p.hostStar.c_str());
            ImGui::Text("Semi-major axis: %.3f AU", p.elements.semiMajorAxisAU);
            ImGui::Text("Eccentricity: %.3f", p.elements.eccentricity);
            ImGui::Text("Period: %.1f days", p.elements.periodDays);
        }
        ImGui::End();

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
