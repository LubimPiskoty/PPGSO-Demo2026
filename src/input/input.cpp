#include "input.hpp"

#include "../ecs/UserMovement.hpp"

#ifdef WITH_IMGUI
#include "imgui.h"
#endif

namespace input {

static ecs::UserMovement *getMovement(GLFWwindow *window) {
    return static_cast<ecs::UserMovement *>(glfwGetWindowUserPointer(window));
}

static void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                        int mods) {
#ifdef WITH_IMGUI
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)
        return;
#endif
    if (action == GLFW_REPEAT)
        return;
    auto movement = getMovement(window);
    if (movement->enabled)
        movement->handleInput(key, action == GLFW_PRESS);
}

static void mouseButtonCallback(GLFWwindow *window, int button, int action,
                                int mods) {
    bool is_pressed = action == GLFW_PRESS;
#ifdef WITH_IMGUI
    // Don't start looking when clicking on an ImGui window, but always let a
    // release through so looking can't get stuck.
    if (is_pressed && ImGui::GetCurrentContext() &&
        ImGui::GetIO().WantCaptureMouse)
        return;
#endif
    auto movement = getMovement(window);
    if (!movement->enabled)
        return;
    movement->handleMouseButton(button, is_pressed);
    glfwSetInputMode(window, GLFW_CURSOR,
                     movement->looking ? GLFW_CURSOR_DISABLED
                                       : GLFW_CURSOR_NORMAL);
}

static void cursorPosCallback(GLFWwindow *window, double x, double y) {
    auto movement = getMovement(window);
    if (movement->enabled)
        movement->handleMouseMove(x, y);
}

void bind(GLFWwindow *window, ecs::UserMovement *movement) {
    glfwSetWindowUserPointer(window, movement);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
}

} // namespace input
