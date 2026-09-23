#pragma once

#include <GLFW/glfw3.h>

namespace ecs {
class UserMovement;
} // namespace ecs

namespace input {

// Forward the window's keyboard/mouse input to the movement component. Call
// before ImGui_ImplGlfw_InitForOpenGL so ImGui's own callbacks chain to these.
void bind(GLFWwindow *window, ecs::UserMovement *movement);

} // namespace input
