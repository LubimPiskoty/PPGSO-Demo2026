#pragma once

#include "../scene/scene.hpp"
#include <string>

namespace loader {

// V2: materials are written once in a top-level "materials" list and mesh
// components reference them by name + guid
// V3: + instanced shaders, UserMovement, Instances and Animator components
void saveScene(const scn::Scene *scene, const std::string path);

// Loads V2 and V3 files. Needs a current GL context (models, textures and
// shaders are uploaded while loading). Cameras are created with a 1x1 size,
// call setSize() with the framebuffer size afterwards.
scn::Scene loadScene(std::string path);

} // namespace loader
