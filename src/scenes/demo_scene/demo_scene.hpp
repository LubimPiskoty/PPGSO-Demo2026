#pragma once

#include "../../scene/scene.hpp"

namespace scenes {

// The final scene: the Medieval City demo, put back together from its
// separate objects (city_layout.hpp, made by tools/split_city_demo.py,
// sorted into people, houses, boats... by tools/sort_city_layout.py).
// Needs a current GL context.
scn::Scene demoScene();

} // namespace scenes
