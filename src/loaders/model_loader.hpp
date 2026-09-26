#pragma once

#include "../render/model.hpp"
#include <string>

namespace loader {
// filename is the embedded resource path, e.g. "SM_PROP_crate_02.glb"
render::Model loadModel(const std::string &filename);

// Flat size x size plane on XZ centered at the origin, facing +Y. Its mesh
// has no texture of its own, the material supplies one. One texture repeat
// covers unitsPerTile world units.
render::Model createPlane(float size, float unitsPerTile = 1.f);
} // namespace loader
