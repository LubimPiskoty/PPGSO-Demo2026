#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <limits>

namespace physics {

struct SphereDesc {
    float radius = 0.5f;
};

// Plane in the node's local XZ plane, facing local +Y, placed and oriented
// by the node's transform (scale ignored). Infinite by default; with a
// finite halfSize it's a rectangle (ramps, pads, jar walls). Collides on
// its front (+Y) side.
struct PlaneDesc {
    static constexpr float infinite = std::numeric_limits<float>::infinity();
    glm::vec2 halfSize{infinite}; // local x, local z

    bool isInfinite() const {
        return halfSize.x == infinite || halfSize.y == infinite;
    }
};

// Collider: `type` says which of the descs below is in use, the other is
// ignored. Assign a desc directly: `collider = SphereDesc{0.5f};`
struct Shape {
    // Order matters: the narrow phase always passes the lower type as `a`
    enum class Type { Plane, Sphere };

    Type type = Type::Sphere;
    SphereDesc sphere;
    PlaneDesc plane;
    // Shape center in the node's local space (moves with its rotation and
    // scale). Set after assigning the desc: `collider.offset = {0, 1, 0};`
    glm::vec3 offset{0.f};

    Shape() = default;

    Shape(SphereDesc s) : type(Type::Sphere), sphere(s) {}

    Shape(PlaneDesc p) : type(Type::Plane), plane(p) {}
};

// Axis aligned bounding box in world space
struct AABB {
    glm::vec3 min{0.f}, max{0.f};
};

inline bool overlaps(const AABB &a, const AABB &b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && //
           a.min.y <= b.max.y && a.max.y >= b.min.y && //
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// World position of the shape's center under `transform`
inline glm::vec3 shapeCenter(const Shape &shape, const glm::mat4 &transform) {
    return glm::vec3(transform * glm::vec4(shape.offset, 1.f));
}

// World AABB of the shape under `transform` (the node's global transform)
inline AABB getAABB(const Shape &shape, const glm::mat4 &transform) {
    const glm::vec3 center = shapeCenter(shape, transform);
    const glm::mat3 m(transform);
    glm::vec3 extents(0.f);

    switch (shape.type) {
    case Shape::Type::Plane: {
        if (shape.plane.isInfinite()) {
            // Overlaps everything, so every plane pair reaches the narrow
            // phase
            constexpr float inf = PlaneDesc::infinite;
            return {glm::vec3(-inf), glm::vec3(inf)};
        }
        // Rotated rectangle: world half extents are |R| * h (abs of each
        // element of the rotation, scale ignored like in the collision)
        const glm::vec3 h(shape.plane.halfSize.x, 0.f, shape.plane.halfSize.y);
        for (int c = 0; c < 3; c++)
            extents += glm::abs(glm::normalize(m[c])) * h[c];
        break;
    }
    case Shape::Type::Sphere: {
        // Rotation doesn't change a sphere, only the largest scale axis
        const float maxScale = glm::max(
            glm::length(m[0]), glm::max(glm::length(m[1]), glm::length(m[2])));
        extents = glm::vec3(shape.sphere.radius * maxScale);
        break;
    }
    }
    return {center - extents, center + extents};
}

} // namespace physics
