#pragma once

#include "../scene/scene.hpp"

namespace scenes {

// A jar made of static boxes on a floor, with balls continuously dropped
// into it from above. Needs a current GL context. The camera is created
// 1x1, the SceneManager sizes it.
scn::Scene ballJar();

} // namespace scenes
