#pragma once

#include "../scene/scene.hpp"
#include <string>

namespace loader {
// Maybe change the scene to live on heap but idk most of data is on heap so it
// should be okay
void saveScene(const scn::Scene *scene, const std::string path);
scn::Scene loadScene(std::string path);

} // namespace loader
