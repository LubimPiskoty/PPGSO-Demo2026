#pragma once

#include "../scene/scene.hpp"

namespace scenes {

// Barrels (sphere colliders) dropped onto two zigzag ramps, through a funnel
// onto the floor, where wind pushes them into a wall. Needs a current GL
// context. The camera is created 1x1, the SceneManager sizes it.
scn::Scene barrelRun();

} // namespace scenes
