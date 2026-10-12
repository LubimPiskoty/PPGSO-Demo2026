#include "common.hpp"

#include "collision.hpp"

#include "../components/physicsbody.hpp"
#include "../debug/profiler.hpp"
#include "rigidbody.hpp"
#include <cmath>
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
        ctx.cache.push_back(
            {e, tf, getAABB(e->collider, tf), ctx.cache.size()});
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

// for each pair: collide -> ctx.contacts
static void narrowPhase(Context &ctx) {
    ctx.contacts.clear();
    for (const auto &[a, b] : ctx.pairs) {
        Contact c;
        if (collide(a, b, c))
            ctx.contacts.push_back(c);
    }
}

// for each contact: impulse + position correction via component lookups.
// A single pass: only spheres move, and their contacts go through the
// center, so one pass is enough.
static void solve(Context &ctx) {
    // Positional correction: push out this fraction of the penetration per
    // step, ignoring the first `slop` so resting contacts don't jitter
    constexpr float correctionPercent = 0.4f;
    constexpr float slop = 0.02f;

    for (const auto &c : ctx.contacts) {
        // References, the impulse has to land on the actual bodies
        RigidBody &A = c.a.e->body;
        RigidBody &B = c.b.e->body;
        const glm::vec3 &n = c.normal;
        const glm::vec3 r_A = c.point - c.a.pos();
        const glm::vec3 r_B = c.point - c.b.pos();

        // Relative velocity of b w.r.t. a at the contact point
        auto relativeVelocity = [&] {
            return (B.velocity + glm::cross(B.angularVelocity, r_B)) -
                   (A.velocity + glm::cross(A.angularVelocity, r_A));
        };
        // Effective inverse mass of the pair along `dir` at the contact
        auto effectiveInvMass = [&](const glm::vec3 &dir) {
            return A.invMass + B.invMass +
                   glm::dot(dir, glm::cross(A.invInertiaWorld *
                                                glm::cross(r_A, dir),
                                            r_A)) +
                   glm::dot(dir, glm::cross(B.invInertiaWorld *
                                                glm::cross(r_B, dir),
                                            r_B));
        };
        // Equal and opposite: a is pushed against j, b along it
        auto applyImpulse = [&](const glm::vec3 &j) {
            A.velocity -= j * A.invMass;
            B.velocity += j * B.invMass;
            A.angularVelocity -= A.invInertiaWorld * glm::cross(r_A, j);
            B.angularVelocity += B.invInertiaWorld * glm::cross(r_B, j);
        };

        // ---- Normal impulse (bounce) ----
        const float v_n = glm::dot(relativeVelocity(), n);
        const float kN = effectiveInvMass(n);
        if (v_n < 0.f && kN > 0.f) { // Approaching (n points from a to b)
            // The less bouncy surface wins
            const float e = glm::min(A.restitution, B.restitution);
            const float j = -(1.f + e) * v_n / kN;
            applyImpulse(j * n);

            // ---- Friction: oppose sliding, capped by Coulomb ----
            const glm::vec3 v_rel = relativeVelocity();
            const glm::vec3 v_t = v_rel - glm::dot(v_rel, n) * n;
            const float speedT = glm::length(v_t);
            if (speedT > 1e-6f) {
                const glm::vec3 t = v_t / speedT;
                const float kT = effectiveInvMass(t);
                const float mu = std::sqrt(A.friction * B.friction);
                // Impulse that would stop sliding, but at most mu * j
                const float jt = glm::min(speedT / kT, mu * j);
                applyImpulse(-jt * t);
            }
        }

        // ---- Separate overlapping bodies ----
        // Split by inverse mass so static bodies don't move. Without this
        // resting objects slowly sink.
        const float invMassSum = A.invMass + B.invMass;
        if (invMassSum > 0.f) {
            const glm::vec3 correction = glm::max(c.depth - slop, 0.f) /
                                         invMassSum * correctionPercent * n;
            // Contacts hold copies, so accumulate on the cache entries
            ctx.cache[c.a.index].correction -= correction * A.invMass;
            ctx.cache[c.b.index].correction += correction * B.invMass;
        }
    }

    // One node write per moved body instead of one per contact. The cached
    // tf is still this step's position, so no read back is needed.
    for (const Body &body : ctx.cache) {
        if (body.correction == glm::vec3(0.f))
            continue;
        if (auto node = body.e->node.lock())
            node->setGlobalPosition(body.pos() + body.correction);
    }
}

void step(Context &ctx, float dt) {
    using profiler::PhaseScope;
    using profiler::PhysicsCounter;
    using profiler::PhysicsPhase;

    {
        // Apply forces and integrate
        PhaseScope _(PhysicsPhase::Integrate);
        for (const auto &body : ctx.bodies)
            body->physics_update(ctx, dt);
    }
    {
        PhaseScope _(PhysicsPhase::Cache);
        buildCache(ctx);
    }
    {
        PhaseScope _(PhysicsPhase::Broad);
        broadPhase(ctx);
    }
    {
        PhaseScope _(PhysicsPhase::Narrow);
        narrowPhase(ctx);
    }
    {
        PhaseScope _(PhysicsPhase::Solve);
        solve(ctx);
    }

    profiler::addCount(PhysicsCounter::Steps, 1);
    profiler::addCount(PhysicsCounter::Pairs, (int)ctx.pairs.size());
    profiler::addCount(PhysicsCounter::Contacts, (int)ctx.contacts.size());
}

} // namespace physics
