#pragma once

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <limits>
#include <glm/vec3.hpp>
#include <variant>

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

typedef std::variant<SphereDesc, BoxDesc, PlaneDesc> Shape;

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
    // Infinite plane: infinite AABB, so it overlaps everything and every
    // plane pair goes to the narrow phase
    if (std::holds_alternative<PlaneDesc>(shape)) {
        constexpr float inf = std::numeric_limits<float>::infinity();
        return {glm::vec3(-inf), glm::vec3(inf)};
    }

    glm::vec3 center(transform[3]);
    glm::mat3 m(transform);

    // Box-like shapes: a rotated/scaled box with local half extents h has
    // world half extents |M| * h (abs of each matrix element)
    auto boxExtents = [&](glm::vec3 h) {
        glm::mat3 absM;
        for (int c = 0; c < 3; c++)
            absM[c] = glm::abs(m[c]);
        return absM * h;
    };

    struct Visitor {
        const glm::mat3 &m;
        decltype(boxExtents) &box;

        // Rotation doesn't change a sphere, only the largest scale axis
        glm::vec3 operator()(const SphereDesc &s) const {
            float maxScale =
                glm::max(glm::length(m[0]),
                         glm::max(glm::length(m[1]), glm::length(m[2])));
            return glm::vec3(s.radius * maxScale);
        }

        glm::vec3 operator()(const BoxDesc &b) const {
            return box(b.halfExtents);
        }

        // Handled above
        glm::vec3 operator()(const PlaneDesc &) const {
            return glm::vec3(0.f);
        }
    };

    glm::vec3 extents = std::visit(Visitor{m, boxExtents}, shape);
    return {center - extents, center + extents};
}

} // namespace physics
