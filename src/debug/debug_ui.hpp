#pragma once

#include <GLFW/glfw3.h>

namespace scenes {
class SceneManager;
} // namespace scenes

// ImGui debug overlay (scene inspector). Only does anything in Debug builds
// (WITH_IMGUI), otherwise every function is a no-op, so callers don't need
// their own #ifdefs.
namespace debug_ui {

// Call after input::init so ImGui's GLFW callbacks chain to ours
void init(GLFWwindow *window);

// Build and render the overlay; call after the scene is drawn. Scene
// switches/reloads are only requested here, the manager applies them on
// the next frame.
void draw(scenes::SceneManager &scenes);

// Texture shown in the "Depth map" window (e.g. the shadow map's depth
// attachment). Pass 0 to show a placeholder instead. The texture is not
// owned, so clear it before it is deleted. perspective = true linearizes
// the depth using the active camera's near/far (a perspective depth buffer
// is almost all white otherwise); pass false for an ortho projection, whose
// depth is already linear.
void setDepthPreview(unsigned int texture, int width, int height,
                     bool perspective = true);

void shutdown();

} // namespace debug_ui
