#pragma once

#include "../../scene/scene.hpp"

namespace scenes {

// The final scene: the Medieval City demo, put back together from its
// separate objects (city_layout.hpp, made by tools/split_city_demo.py).
// Needs a current GL context.
scn::Scene demoScene();

} // namespace scenes
