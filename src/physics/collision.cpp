#include "collision.hpp"

#include "../components/physicsbody.hpp"
#include <cmath>
#include <glm/geometric.hpp>
#include <utility>

namespace physics {

// ---- One collide function per shape pair. Convention (matches Contact):
// `a` has the lower Shape::Type (plane < sphere), the normal points from a
// toward b. Return true and fill `c` when the shapes touch. ----

// Rotation part of a body's transform (columns normalized, scale ignored)
static glm::mat3 rotationOf(const Body &body) {
    return glm::mat3(glm::normalize(glm::vec3(body.tf[0])),
                     glm::normalize(glm::vec3(body.tf[1])),
                     glm::normalize(glm::vec3(body.tf[2])));
}

static bool collidePlaneSphere(const PlaneDesc &p, const Body &pb,
                               const SphereDesc &s, const Body &sb,
                               Contact &c) {
    // Plane's local space: the plane is y = 0, the rectangle
    // |x| <= halfSize.x, |z| <= halfSize.y (infinite: no limit), front +Y
    const glm::mat3 R = rotationOf(pb);
    const glm::vec3 local = glm::transpose(R) * (sb.center - pb.center);

    // Closest point of the rectangle to the sphere center
    const glm::vec3 closest(glm::clamp(local.x, -p.halfSize.x, p.halfSize.x),
                            0.f,
                            glm::clamp(local.z, -p.halfSize.y, p.halfSize.y));

    glm::vec3 nLocal;
    float depth;
    if (local.y > 0.f) {
        // In front: touching if the closest point is within the radius
        // (also handles hitting a rectangle's edge)
        const glm::vec3 d = local - closest;
        const float dist = glm::length(d);
        if (dist >= s.radius)
            return false;
        nLocal = dist > 1e-6f ? d / dist : glm::vec3(0.f, 1.f, 0.f);
        depth = s.radius - dist;
    } else {
        // Behind (sunk in): push out the front, but only if the center is
        // over the rectangle and not deeper than the radius, otherwise the
        // sphere is just passing behind it
        const bool over = closest.x == local.x && closest.z == local.z;
        if (!over || (!p.isInfinite() && local.y <= -s.radius))
            return false;
        nLocal = glm::vec3(0.f, 1.f, 0.f);
        depth = s.radius - local.y;
    }

    c.a = pb;
    c.b = sb;
    c.normal = R * nLocal; // plane -> sphere
    c.point = pb.center + R * closest;
    c.depth = depth;
    return true;
}

static bool collideSphereSphere(const SphereDesc &sA, const Body &sbA,
                                const SphereDesc &sB, const Body &sbB,
                                Contact &c) {
    const glm::vec3 d = sbB.center - sbA.center;
    const float dist = glm::length(d);
    if (dist < 1e-6f) // Zero guard with epsilon
        return false;
    const glm::vec3 n = d / dist;
    c.depth = sA.radius + sB.radius - dist;
    if (c.depth <= 0.f)
        return false; // Not touching
    c.a = sbA;
    c.b = sbB;
    c.normal = n;
    c.point = sbA.center + n * (sA.radius - c.depth / 2);
    return true;
}

// Picks the collide function for the pair. Orders the bodies so `a` has
// the lower type first, so each pair needs only one function.
bool collide(const Body *a, const Body *b, Contact &c) {
    if (a->e->collider.type > b->e->collider.type)
        std::swap(a, b);

    using T = Shape::Type;
    const Shape &sa = a->e->collider;
    const Shape &sb = b->e->collider;

    if (sa.type == T::Plane && sb.type == T::Sphere)
        return collidePlaneSphere(sa.plane, *a, sb.sphere, *b, c);
    if (sa.type == T::Sphere && sb.type == T::Sphere)
        return collideSphereSphere(sa.sphere, *a, sb.sphere, *b, c);

    // Plane vs plane: both static, the broad phase already skips those
    return false;
}

} // namespace physics
