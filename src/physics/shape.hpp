#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <limits>
#include <glm/vec3.hpp>

namespace physics {

struct SphereDesc {
    float radius = 0.5f;
};

struct BoxDesc {
    glm::vec3 halfExtents{0.5f};
};

// Infinite plane in world space: points p with dot(normal, p) == offset.
// Ignores the node's transform.
struct PlaneDesc {
    glm::vec3 normal{0, 1, 0};
    float offset = 0.0f;
};

// Collider: `type` says which of the descs below is in use, the others are
// ignored. Assign a desc directly: `collider = SphereDesc{0.5f};`
struct Shape {
    // Order matters: the narrow phase always passes the lower type as `a`
    enum class Type { Plane, Box, Sphere };

    Type type = Type::Sphere;
    SphereDesc sphere;
    BoxDesc box;
    PlaneDesc plane;

    Shape() = default;
    Shape(SphereDesc s) : type(Type::Sphere), sphere(s) {}
    Shape(BoxDesc b) : type(Type::Box), box(b) {}
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

// World AABB of the shape under `transform` (the node's global transform,
// rotation and scale included)
inline AABB getAABB(const Shape &shape, const glm::mat4 &transform) {
    glm::vec3 center(transform[3]);
    glm::mat3 m(transform);
    glm::vec3 extents(0.f);

    switch (shape.type) {
    case Shape::Type::Plane: {
        // Infinite plane: infinite AABB, so it overlaps everything and every
        // plane pair goes to the narrow phase
        constexpr float inf = std::numeric_limits<float>::infinity();
        return {glm::vec3(-inf), glm::vec3(inf)};
    }
    case Shape::Type::Box: {
        // A rotated/scaled box with local half extents h has world half
        // extents |M| * h (abs of each matrix element)
        glm::mat3 absM;
        for (int c = 0; c < 3; c++)
            absM[c] = glm::abs(m[c]);
        extents = absM * shape.box.halfExtents;
        break;
    }
    case Shape::Type::Sphere: {
        // Rotation doesn't change a sphere, only the largest scale axis
        float maxScale = glm::max(
            glm::length(m[0]), glm::max(glm::length(m[1]), glm::length(m[2])));
        extents = glm::vec3(shape.sphere.radius * maxScale);
        break;
    }
    }
    return {center - extents, center + extents};
}

} // namespace physics
