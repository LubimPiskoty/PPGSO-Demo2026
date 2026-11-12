#pragma once

#include <glm/vec2.hpp>

#include "../render/model.hpp"
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace loader {
// filename is the embedded resource path, e.g. "SM_PROP_crate_02.glb"
render::Model loadModel(const std::string &filename);

// The model's triangles on the CPU, three corners each, every mesh's in its
// own space like loadModel (node transforms not applied). For placing things
// on a surface, e.g. finding the ground's height under a point. Only the
// triangles whose UVs' middle lies in [uvMin, uvMax] (the file's UVs, v
// from the image's top as in glTF): one part of a texture atlas.
std::vector<glm::vec3> loadTriangles(const std::string &filename,
                                     glm::vec2 uvMin = glm::vec2(-1e30f),
                                     glm::vec2 uvMax = glm::vec2(1e30f));

// Flat size x size plane on XZ centered at the origin, facing +Y. Its mesh
// has no texture of its own, the material supplies one. One texture repeat
// covers unitsPerTile world units.
render::Model createPlane(float size, float unitsPerTile = 1.f);

// Same as createPlane, but (2 * halfSize.x) x (2 * halfSize.y) on XZ
render::Model createRect(glm::vec2 halfSize, float unitsPerTile = 1.f);

// Box centered at the origin, flat shaded. Textured like createPlane: UVs are
// in world units / unitsPerTile on every face.
render::Model createBox(glm::vec3 halfExtents, float unitsPerTile = 1.f);

// Square pyramid, flat shaded: base (2 * halfBase)^2 on XZ at y = 0, apex at
// (0, height, 0). Handy as an arrow tip, rotate it to point elsewhere.
render::Model createPyramid(float halfBase, float height);

// UV sphere centered at the origin, UVs in world units / unitsPerTile
// (u around the equator, v pole to pole). invertNormals points the normals
// inward, so a light placed at the center lights the whole surface.
render::Model createSphere(float radius, float unitsPerTile = 1.f,
                           int segments = 32, int rings = 16,
                           bool invertNormals = false);
} // namespace loader
