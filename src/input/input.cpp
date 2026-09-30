#include "input.hpp"

#include <vector>

#ifdef WITH_IMGUI
#include "imgui.h"
#endif

namespace input {

static GLFWwindow *bound_window = nullptr;
static std::vector<std::function<void(int, int)>> key_callbacks;
static std::vector<std::function<void(int, int)>> mouse_button_callbacks;
static std::vector<std::function<void(double, double)>> cursor_pos_callbacks;

static void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                        int mods) {
#ifdef WITH_IMGUI
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)
        return;
#endif
    for (auto &callback : key_callbacks)
        callback(key, action);
}

static void mouseButtonCallback(GLFWwindow *window, int button, int action,
                                int mods) {
#ifdef WITH_IMGUI
    // Don't forward clicks on an ImGui window, but always let a release
    // through so held-button state can't get stuck.
    if (action == GLFW_PRESS && ImGui::GetCurrentContext() &&
        ImGui::GetIO().WantCaptureMouse)
        return;
#endif
    for (auto &callback : mouse_button_callbacks)
        callback(button, action);
}

static void cursorPosCallback(GLFWwindow *window, double x, double y) {
    for (auto &callback : cursor_pos_callbacks)
        callback(x, y);
}

void init(GLFWwindow *window) {
    bound_window = window;
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
}

void onKey(std::function<void(int, int)> callback) {
    key_callbacks.push_back(callback);
}

void onMouseButton(std::function<void(int, int)> callback) {
    mouse_button_callbacks.push_back(callback);
}

void onCursorPos(std::function<void(double, double)> callback) {
    cursor_pos_callbacks.push_back(callback);
}

void setCursorCaptured(bool captured) {
    if (bound_window)
        glfwSetInputMode(bound_window, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

} // namespace input
