#pragma once

#include <glm/vec2.hpp>

#include "../render/model.hpp"
#include <string>

namespace loader {
// filename is the embedded resource path, e.g. "SM_PROP_crate_02.glb"
render::Model loadModel(const std::string &filename);

// Flat size x size plane on XZ centered at the origin, facing +Y. Its mesh
// has no texture of its own, the material supplies one. One texture repeat
// covers unitsPerTile world units.
render::Model createPlane(float size, float unitsPerTile = 1.f);

// Same as createPlane, but (2 * halfSize.x) x (2 * halfSize.y) on XZ
render::Model createRect(glm::vec2 halfSize, float unitsPerTile = 1.f);

// Box centered at the origin, flat shaded. Textured like createPlane: UVs are
// in world units / unitsPerTile on every face.
render::Model createBox(glm::vec3 halfExtents, float unitsPerTile = 1.f);

// UV sphere centered at the origin, UVs in world units / unitsPerTile
// (u around the equator, v pole to pole).
render::Model createSphere(float radius, float unitsPerTile = 1.f,
                           int segments = 32, int rings = 16);
} // namespace loader
