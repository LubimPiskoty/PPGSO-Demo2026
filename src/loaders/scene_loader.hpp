#pragma once

#include "../scene/scene.hpp"
#include <string>

namespace loader {

// V2: materials are written once in a top-level "materials" list and mesh
// components reference them by name + guid
void saveScene(const scn::Scene *scene, const std::string path);

scn::Scene loadScene(std::string path);

} // namespace loader
