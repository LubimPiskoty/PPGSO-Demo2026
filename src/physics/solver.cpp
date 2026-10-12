#include "common.hpp"

#include "../components/physicsbody.hpp"
#include "rigidbody.hpp"
#include <cstddef>
#include <utility>
#include <vector>

namespace physics {

// Snapshot transform + AABB of every body -> ctx.cache
static void buildCache(Context &ctx) {
    ctx.cache.clear();
    ctx.cache.reserve(ctx.bodies.size());
    for (Entity e : ctx.bodies) {
        auto n = e->node.lock();
        if (!n)
            continue;
        glm::mat4 tf = n->globalTransform();
        ctx.cache.push_back({e, tf, getAABB(e->collider, tf)});
    }
}

// AABB test for every pair of cached bodies -> ctx.pairs
static void broadPhase(Context &ctx) {
    ctx.pairs.clear();
    for (std::size_t i = 0; i < ctx.cache.size(); i++) {
        for (std::size_t j = i + 1; j < ctx.cache.size(); j++) {
            const Body &a = ctx.cache[i];
            const Body &b = ctx.cache[j];
            // Static vs static never needs solving
            if (a.e->body.invMass == 0.f && b.e->body.invMass == 0.f)
                continue;
            if (overlaps(a.aabb, b.aabb))
                ctx.pairs.emplace_back(&a, &b);
        }
    }
}

// ---- One collide function per shape pair. Convention (matches Contact):
// `a` has the lower Shape::Type (plane < box < sphere), the normal points
// from a toward b. Return true and fill `c` when the shapes touch. ----

static bool collidePlaneSphere(const PlaneDesc &p, const Body &pb,
                               const SphereDesc &s, const Body &sb,
                               Contact &c) {
    // Plane is in world space (dot(normal, x) == offset), the node's
    // transform is ignored, so use offset rather than pb.pos()
    const glm::vec3 center = sb.pos();
    const float dist = glm::dot(p.normal, center) - p.offset;
    const float depth = s.radius - dist;
    if (depth <= 0.f)
        return false;

    c.a = pb;
    c.b = sb;
    c.normal = p.normal; // plane -> sphere
    // Deepest point of the sphere, the one touching/inside the plane
    c.point = center - p.normal * s.radius;
    c.depth = depth;
    return true;
}

static bool collidePlaneBox(const PlaneDesc &, const Body &, const BoxDesc &,
                            const Body &, Contact &) {
    // TODO
    return false;
}

static bool collideBoxSphere(const BoxDesc &, const Body &,
                             const SphereDesc &, const Body &, Contact &) {
    // TODO
    return false;
}

static bool collideBoxBox(const BoxDesc &, const Body &, const BoxDesc &,
                          const Body &, Contact &) {
    // TODO
    return false;
}

static bool collideSphereSphere(const SphereDesc &, const Body &,
                                const SphereDesc &, const Body &, Contact &) {
    // TODO
    return false;
}

// Picks the collide function for the pair. Orders the bodies so `a` has
// the lower type first, so each pair needs only one function.
static bool collide(const Body *a, const Body *b, Contact &c) {
    if (a->e->collider.type > b->e->collider.type)
        std::swap(a, b);

    using T = Shape::Type;
    const Shape &sa = a->e->collider;
    const Shape &sb = b->e->collider;

    if (sa.type == T::Plane && sb.type == T::Sphere)
        return collidePlaneSphere(sa.plane, *a, sb.sphere, *b, c);
    if (sa.type == T::Plane && sb.type == T::Box)
        return collidePlaneBox(sa.plane, *a, sb.box, *b, c);
    if (sa.type == T::Box && sb.type == T::Sphere)
        return collideBoxSphere(sa.box, *a, sb.sphere, *b, c);
    if (sa.type == T::Box && sb.type == T::Box)
        return collideBoxBox(sa.box, *a, sb.box, *b, c);
    if (sa.type == T::Sphere && sb.type == T::Sphere)
        return collideSphereSphere(sa.sphere, *a, sb.sphere, *b, c);

    // Plane vs plane: both static, the broad phase already skips those
    return false;
}

// for each pair: collide -> ctx.contacts
static void narrowPhase(Context &ctx) {
    ctx.contacts.clear();
    for (const auto &[a, b] : ctx.pairs) {
        Contact c{};
        if (collide(a, b, c))
            ctx.contacts.push_back(c);
    }
}

// for each contact: impulse + position correction via component lookups
static void solve(Context &ctx) {
    // Positional correction: push out this fraction of the penetration per
    // step, ignoring the first `slop` so resting contacts don't jitter
    constexpr float correctionPercent = 0.8f;
    constexpr float slop = 0.01f;

    for (const auto &contact : ctx.contacts) {
        // References, the impulse has to land on the actual bodies
        RigidBody &rb_A = contact.a.e->body;
        RigidBody &rb_B = contact.b.e->body;
        const glm::vec3 &n = contact.normal;

        glm::vec3 r_A = contact.point - contact.a.pos();
        glm::vec3 r_B = contact.point - contact.b.pos();
        glm::vec3 v_rel =
            (rb_B.velocity + glm::cross(rb_B.angularVelocity, r_B)) -
            (rb_A.velocity + glm::cross(rb_A.angularVelocity, r_A));
        float v_n = glm::dot(v_rel, n);

        if (v_n < 0) { // Bodies are approaching (n points from a to b)
            // The less bouncy surface wins
            float e = glm::min(rb_A.restitution, rb_B.restitution);

            float denom =
                rb_A.invMass + rb_B.invMass +
                glm::dot(n,
                         glm::cross(rb_A.invInertiaWorld * glm::cross(r_A, n),
                                    r_A)) +
                glm::dot(
                    n,
                    glm::cross(rb_B.invInertiaWorld * glm::cross(r_B, n), r_B));
            if (denom > 0.f) {
                float j = -(1.f + e) * v_n / denom;
                glm::vec3 impulse = j * n;

                // Equal and opposite: a is pushed against n, b along it
                rb_A.velocity -= impulse * rb_A.invMass;
                rb_B.velocity += impulse * rb_B.invMass;
                rb_A.angularVelocity -=
                    rb_A.invInertiaWorld * glm::cross(r_A, impulse);
                rb_B.angularVelocity +=
                    rb_B.invInertiaWorld * glm::cross(r_B, impulse);
            }
        }

        // Separate overlapping bodies, split by inverse mass so static
        // bodies don't move. Without this resting objects slowly sink.
        float invMassSum = rb_A.invMass + rb_B.invMass;
        if (invMassSum > 0.f) {
            glm::vec3 correction = glm::max(contact.depth - slop, 0.f) /
                                   invMassSum * correctionPercent * n;
            // Bodies are root children, so local == world position
            if (auto node = contact.a.e->node.lock())
                node->localPos -= correction * rb_A.invMass;
            if (auto node = contact.b.e->node.lock())
                node->localPos += correction * rb_B.invMass;
        }
    }
}

void step(Context &ctx, float dt) {
    // Apply forces and integrate
    for (const auto &body : ctx.bodies)
        body->physics_update(ctx, dt);

    buildCache(ctx);
    broadPhase(ctx);
    narrowPhase(ctx);
    solve(ctx);
}

} // namespace physics
