#pragma once

#include "../../scene/scene.hpp"

namespace scenes {

// Shot 4 of the demo: inside the smithy. The forge building's shell (the
// ironheart pack) furnished with the old-smithy pack (smithy_layout.hpp, made
// by tools/export_smithy.py): the bellows blow on a physics coal bed, the
// hammer strikes sparks off the anvil, all on one camera flight. Its own
// scene so the city isn't drawn behind the walls. Needs a current GL context.
scn::Scene smithyScene();

} // namespace scenes
