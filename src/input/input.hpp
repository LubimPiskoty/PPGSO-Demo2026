#pragma once

#include <GL/glew.h> // must come before glfw3.h
#include <GLFW/glfw3.h>
#include <functional>

// Anything can register a callback for key, mouse button or cursor events.
// Events ImGui wants are not forwarded.
namespace input {

// Install the GLFW callbacks on the window. Call before
// ImGui_ImplGlfw_InitForOpenGL so ImGui's own callbacks chain to these.
void init(GLFWwindow *window);

// Each returns an id for unsubscribe(). Something that goes away before the
// window does (a component, with its scene) must unsubscribe, or the next
// event calls into it.
// action is GLFW_PRESS, GLFW_RELEASE or GLFW_REPEAT
int onKey(std::function<void(int key, int action)> callback);
int onMouseButton(std::function<void(int button, int action)> callback);
int onCursorPos(std::function<void(double x, double y)> callback);
// Removes the callback with that id, whichever kind it is
void unsubscribe(int id);

// Hide and lock the cursor to the window (for mouse look), or release it
void setCursorCaptured(bool captured);

} // namespace input
