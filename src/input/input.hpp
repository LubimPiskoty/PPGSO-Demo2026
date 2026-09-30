#pragma once

#include <GLFW/glfw3.h>
#include <functional>

// Anything can register a callback for key, mouse button or cursor events.
// Events ImGui wants are not forwarded.
namespace input {

// Install the GLFW callbacks on the window. Call before
// ImGui_ImplGlfw_InitForOpenGL so ImGui's own callbacks chain to these.
void init(GLFWwindow *window);

// action is GLFW_PRESS, GLFW_RELEASE or GLFW_REPEAT
void onKey(std::function<void(int key, int action)> callback);
void onMouseButton(std::function<void(int button, int action)> callback);
void onCursorPos(std::function<void(double x, double y)> callback);

// Hide and lock the cursor to the window (for mouse look), or release it
void setCursorCaptured(bool captured);

} // namespace input
