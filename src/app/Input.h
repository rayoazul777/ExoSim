#pragma once

struct GLFWwindow;
struct AppContext;

// Installs GLFW callbacks that drive the camera and click picking from
// mouse input, forwarding every event to ImGui first. Call after ImGui's
// GLFW backend init. The window's user pointer must be the AppContext.
void InstallInputCallbacks(GLFWwindow* window, AppContext& ctx);
