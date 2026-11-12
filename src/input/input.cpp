#include "input.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#ifdef WITH_IMGUI
#include "imgui.h"
#endif

namespace input {

static GLFWwindow *bound_window = nullptr;
template <typename F> using Callbacks = std::vector<std::pair<int, F>>;
static Callbacks<std::function<void(int, int)>> key_callbacks;
static Callbacks<std::function<void(int, int)>> mouse_button_callbacks;
static Callbacks<std::function<void(double, double)>> cursor_pos_callbacks;
static int next_id = 1;

// Calls every callback on a copy of the list: one may (un)subscribe while
// the event is handed out
template <typename F, typename... Args>
static void dispatch(const Callbacks<F> &callbacks, Args... args) {
    const Callbacks<F> copy = callbacks;
    for (const auto &[id, callback] : copy)
        callback(args...);
}

template <typename F> static int add(Callbacks<F> &callbacks, F callback) {
    callbacks.emplace_back(next_id, std::move(callback));
    return next_id++;
}

template <typename F> static void remove(Callbacks<F> &callbacks, int id) {
    callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(),
                                   [id](const auto &c) { return c.first == id; }),
                    callbacks.end());
}

static void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                        int mods) {
#ifdef WITH_IMGUI
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)
        return;
#endif
    dispatch(key_callbacks, key, action);
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
    dispatch(mouse_button_callbacks, button, action);
}

static void cursorPosCallback(GLFWwindow *window, double x, double y) {
    dispatch(cursor_pos_callbacks, x, y);
}

void init(GLFWwindow *window) {
    bound_window = window;
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
}

int onKey(std::function<void(int, int)> callback) {
    return add(key_callbacks, std::move(callback));
}

int onMouseButton(std::function<void(int, int)> callback) {
    return add(mouse_button_callbacks, std::move(callback));
}

int onCursorPos(std::function<void(double, double)> callback) {
    return add(cursor_pos_callbacks, std::move(callback));
}

void unsubscribe(int id) {
    remove(key_callbacks, id);
    remove(mouse_button_callbacks, id);
    remove(cursor_pos_callbacks, id);
}

void setCursorCaptured(bool captured) {
    if (bound_window)
        glfwSetInputMode(bound_window, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

} // namespace input
