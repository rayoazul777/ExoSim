#include "app/Input.h"
#include "app/AppContext.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <algorithm>
#include <cmath>

namespace {
AppContext& ctxOf(GLFWwindow* w) { return *static_cast<AppContext*>(glfwGetWindowUserPointer(w)); }

void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    AppContext& ctx = ctxOf(w);
    ctx.width = width;
    ctx.height = height;
    glViewport(0, 0, width, height);
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    if (ImGui::GetIO().WantCaptureMouse) return;
    AppContext& ctx = ctxOf(window);
    if (button == GLFW_MOUSE_BUTTON_RIGHT && ctx.view.mode == ViewMode::Galaxy) {
        ctx.cam.panning = action == GLFW_PRESS;
        glfwGetCursorPos(window, &ctx.cam.lastX, &ctx.cam.lastY);
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        ctx.cam.dragging = true;
        glfwGetCursorPos(window, &ctx.cam.lastX, &ctx.cam.lastY);
        ctx.click.pressX = ctx.cam.lastX;
        ctx.click.pressY = ctx.cam.lastY;
    } else if (action == GLFW_RELEASE) {
        ctx.cam.dragging = false;
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        if (std::hypot(x - ctx.click.pressX, y - ctx.click.pressY) < 5.0) {
            ctx.click.pending = true;
            ctx.click.x = x;
            ctx.click.y = y;
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
    AppContext& ctx = ctxOf(window);
    CameraState& cam = ctx.cam;
    if (ctx.view.mode == ViewMode::Galaxy && (cam.dragging || cam.panning)) {
        GalaxyView& g = ctx.view.galaxy;
        double dx = xpos - cam.lastX, dy = ypos - cam.lastY;
        int winW = 1, winH = 1;
        glfwGetWindowSize(window, &winW, &winH);
        if (cam.panning || g.topDown)
            g.pan(dx, dy, winH); // drag the map (left in 2-D, right in both)
        else
            g.rotate(dx, dy);
        cam.lastX = xpos;
        cam.lastY = ypos;
        return;
    }
    if (!cam.dragging) return;
    cam.yaw += static_cast<float>(xpos - cam.lastX) * 0.006f;
    cam.pitch -= static_cast<float>(ypos - cam.lastY) * 0.006f;
    cam.pitch = std::clamp(cam.pitch, -1.5f, 1.5f); // avoid flipping over the poles
    cam.lastX = xpos;
    cam.lastY = ypos;
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    if (ImGui::GetIO().WantCaptureMouse) return;
    AppContext& ctx = ctxOf(window);
    CameraState& cam = ctx.cam;
    const float factor = std::pow(0.9f, static_cast<float>(yoffset));
    if (ctx.view.mode == ViewMode::Galaxy) {
        double x = 0.0, y = 0.0;
        int winW = 1, winH = 1;
        glfwGetCursorPos(window, &x, &y);
        glfwGetWindowSize(window, &winW, &winH);
        ctx.view.galaxy.zoomAt(std::pow(0.85, yoffset), x, y, winW, winH);
        return;
    }
    if (ctx.view.mode == ViewMode::Landing) {
        cam.landingZoom = std::clamp(cam.landingZoom * factor, 0.05f, 40.0f);
        return;
    }
    bool detail = ctx.view.mode == ViewMode::PlanetDetail;
    float& dist = detail ? cam.planetDistance : cam.systemDistance;
    float& target = detail ? cam.planetDistanceTarget : cam.systemDistanceTarget;
    dist *= factor;
    float minD = detail ? 1.05f : (cam.followingCraft ? 0.002f : 0.5f);
    float maxD = detail ? 30.0f : 20000.0f;
    dist = std::clamp(dist, minD, maxD);
    target = dist; // manual zoom is instant - only programmatic changes ease in
}
} // namespace

void InstallInputCallbacks(GLFWwindow* window, AppContext& ctx) {
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
}
