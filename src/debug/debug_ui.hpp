#pragma once

#include <GLFW/glfw3.h>

namespace scn {
class Scene;
} // namespace scn

// ImGui debug overlay (scene inspector). Only does anything in Debug builds
// (WITH_IMGUI), otherwise every function is a no-op, so callers don't need
// their own #ifdefs.
namespace debug_ui {

// Call after input::bind so ImGui's GLFW callbacks chain to ours
void init(GLFWwindow *window);

// Build and render the overlay; call after the scene is drawn
void draw(scn::Scene &scene);

void shutdown();

} // namespace debug_ui
