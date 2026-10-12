#include "physics_test.hpp"

#include "../components/UserMovement.hpp"
#include "../components/camera.hpp"
#include "../components/mesh.hpp"
#include "../components/physicsbody.hpp"
#include "../loaders/model_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/material.hpp"

#include <cmath>
#include <glm/gtc/constants.hpp>

namespace scenes {

// Hand built physics test arena: a floor enclosed by four walls, split into
// mini stages that each show one scenario. Everything uses the grid texture,
// tinted per material.
//
//   back-left   BOUNCE   same drop height, increasing restitution
//   back-right  SLIDE    boxes and balls on ramps of increasing steepness
//   front-left  IMPACT   head-on collisions, equal and unequal mass
//   front-right STACK    box tower and a ball rolled into a box wall
scn::Scene physicsTest() {
    scn::Scene scene;

    auto shader =
        std::make_shared<render::Shader>("grid_lit.vs", "grid_lit.fs");
    auto grid = loader::loadTexture("grid_dark.png");

    auto makeMaterial = [&](const char *name, glm::vec3 color) {
        auto material = std::make_shared<render::Material>(name, shader);
        material->setTexture(render::TextureSlot::Albedo, grid);
        material->diffuse = color;
        material->setFloat("uBrighten", 4.f);
        return material;
    };

    // Muted tones, between pastel and fully saturated
    auto floorMat = makeMaterial("Floor", {0.30f, 0.33f, 0.38f});
    auto wallMat = makeMaterial("Wall", {0.42f, 0.45f, 0.50f});
    auto rampMat = makeMaterial("Ramp", {0.36f, 0.39f, 0.44f});
    std::shared_ptr<render::Material> ballMats[] = {
        makeMaterial("Terracotta", {0.76f, 0.41f, 0.31f}),
        makeMaterial("Teal", {0.24f, 0.58f, 0.57f}),
        makeMaterial("Mustard", {0.80f, 0.65f, 0.27f}),
        makeMaterial("Plum", {0.58f, 0.38f, 0.62f}),
    };
    std::shared_ptr<render::Material> boxMats[] = {
        makeMaterial("Sage", {0.50f, 0.65f, 0.44f}),
        makeMaterial("Steel blue", {0.31f, 0.49f, 0.72f}),
        makeMaterial("Rust", {0.80f, 0.48f, 0.33f}),
    };

    auto makeStatic = [](component::PhysicsBody &body) {
        body.body.mass = 0.f;
        body.body.invMass = 0.f;
        body.body.invInertiaLocal = glm::mat3(0.f);
        body.body.invInertiaWorld = glm::mat3(0.f);
    };

    // A static infinite plane: dot(normal, p) == offset. The visual is a
    // createPlane rotated so its +Y faces `normal`. A null material gives a
    // collider only (used for the wall facing the camera, so it doesn't
    // block the view).
    auto addPlane = [&](const char *name, glm::vec3 normal, float offset,
                        glm::vec3 center, glm::vec3 rotEuler, float size,
                        std::shared_ptr<render::Material> material) {
        auto node = scn::Node::create(name, center);
        node->localRotEuler = rotEuler;
        if (material)
            node->add_component<component::Mesh>(
                std::make_shared<render::Model>(loader::createPlane(size, 2.f)),
                material);
        auto body = node->add_component<component::PhysicsBody>();
        body->collider = physics::PlaneDesc{normal, offset};
        makeStatic(*body);
        scene.root->add_child(node);
    };

    auto addBall = [&](const char *name, glm::vec3 pos, float radius,
                       float mass, float restitution,
                       std::shared_ptr<render::Material> material,
                       glm::vec3 velocity = glm::vec3(0.f)) {
        auto node = scn::Node::create(name, pos);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::createSphere(radius, 2.f)),
            material);
        auto body = node->add_component<component::PhysicsBody>();
        body->collider = physics::SphereDesc{radius};
        body->body.mass = mass;
        body->body.invMass = 1.f / mass;
        body->body.restitution = restitution;
        body->body.velocity = velocity;
        body->body.invInertiaLocal =
            glm::mat3(1.f / (0.4f * mass * radius * radius));
        body->body.invInertiaWorld = body->body.invInertiaLocal;
        scene.root->add_child(node);
    };

    // mass == 0 makes the box static (ramps, pedestals)
    auto addBox = [&](const char *name, glm::vec3 pos, glm::vec3 halfExtents,
                      glm::vec3 rotEuler, float mass, float restitution,
                      std::shared_ptr<render::Material> material) {
        auto node = scn::Node::create(name, pos);
        node->localRotEuler = rotEuler;
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::createBox(halfExtents, 2.f)),
            material);
        auto body = node->add_component<component::PhysicsBody>();
        body->collider = physics::BoxDesc{halfExtents};
        body->body.restitution = restitution;
        if (mass <= 0.f) {
            makeStatic(*body);
        } else {
            body->body.mass = mass;
            body->body.invMass = 1.f / mass;
            // Solid cuboid: I = m/3 * (sum of the other two half extents
            // squared)
            glm::vec3 sq = halfExtents * halfExtents;
            glm::vec3 inertia =
                mass / 3.f * glm::vec3(sq.y + sq.z, sq.x + sq.z, sq.x + sq.y);
            body->body.invInertiaLocal = glm::mat3(1.f);
            for (int i = 0; i < 3; i++)
                body->body.invInertiaLocal[i][i] = 1.f / inertia[i];
            body->body.invInertiaWorld = body->body.invInertiaLocal;
        }
        scene.root->add_child(node);
    };

    // ---- Arena: floor + four walls, so nothing can leave ----
    const float half = glm::half_pi<float>();
    const float arena = 16.f; // walls at +-arena on x and z
    const float wallY = 6.f;
    addPlane("Floor", {0, 1, 0}, 0.f, {0, 0, 0}, {0, 0, 0}, 2 * arena,
             floorMat);
    addPlane("Back wall", {0, 0, 1}, -arena, {0, wallY, -arena}, {half, 0, 0},
             2 * arena, wallMat);
    addPlane("Left wall", {1, 0, 0}, -arena, {-arena, wallY, 0}, {0, 0, -half},
             2 * arena, wallMat);
    addPlane("Right wall", {-1, 0, 0}, -arena, {arena, wallY, 0}, {0, 0, half},
             2 * arena, wallMat);
    addPlane("Front wall", {0, 0, -1}, -arena, {0, wallY, arena}, {-half, 0, 0},
             2 * arena, nullptr);

    // ---- Stage 1, BOUNCE: identical balls, same height, restitution
    // 0.2 -> 0.95. Each sits above its own pedestal so the stages are
    // readable at a glance. ----
    {
        const float restitutions[] = {0.2f, 0.5f, 0.8f, 0.95f};
        for (int i = 0; i < 4; i++) {
            float x = -13.f + i * 2.5f;
            addBox("Bounce pad", {x, 0.1f, -10.f}, {1.f, 0.1f, 1.f}, {0, 0, 0},
                   0.f, 0.5f, rampMat);
            addBall("Bounce ball", {x, 8.f, -10.f}, 0.5f, 1.f, restitutions[i],
                    ballMats[i]);
        }
    }

    // ---- Stage 2, SLIDE: ramps at 15, 30 and 45 degrees, descending
    // toward -x. A box rests near the top of each, and a ball next to it
    // so sliding and rolling can be compared. ----
    {
        const float angles[] = {15.f, 30.f, 45.f};
        const glm::vec3 rampHalf{3.f, 0.15f, 1.2f};
        for (int i = 0; i < 3; i++) {
            float theta = glm::radians(angles[i]);
            float z = -13.f + i * 3.f;
            // Raise the ramp so its low end just touches the floor
            glm::vec3 center{8.f, 3.f * std::sin(theta) + 0.2f, z};
            glm::vec3 rot{0.f, 0.f, theta};
            addBox("Ramp", center, rampHalf, rot, 0.f, 0.2f, rampMat);

            // Point on the ramp's top surface, `along` units from its
            // center (local +x is uphill), lifted `lift` off the surface
            auto onRamp = [&](float along, float lift, float zOff) {
                glm::vec3 up{-std::sin(theta), std::cos(theta), 0.f};
                glm::vec3 dir{std::cos(theta), std::sin(theta), 0.f};
                return center + dir * along + up * (rampHalf.y + lift + 0.02f) +
                       glm::vec3(0, 0, zOff);
            };
            addBox("Slide box", onRamp(2.f, 0.4f, -0.5f), glm::vec3(0.4f), rot,
                   1.f, 0.1f, boxMats[i]);
            addBall("Roll ball", onRamp(2.f, 0.35f, 0.6f), 0.35f, 1.f, 0.2f,
                    ballMats[i]);
        }
    }

    // ---- Stage 3, IMPACT: pairs of balls launched at each other ----
    {
        // Equal mass: velocities should swap
        addBall("Impact A", {-13.f, 0.5f, 4.f}, 0.5f, 1.f, 0.9f, ballMats[0],
                {5.f, 0.f, 0.f});
        addBall("Impact B", {-3.f, 0.5f, 4.f}, 0.5f, 1.f, 0.9f, ballMats[1],
                {-5.f, 0.f, 0.f});
        // Heavy vs light: light ball gets thrown back
        addBall("Impact heavy", {-13.f, 1.f, 9.f}, 1.f, 8.f, 0.9f, ballMats[3],
                {4.f, 0.f, 0.f});
        addBall("Impact light", {-3.f, 0.4f, 9.f}, 0.4f, 0.5f, 0.9f,
                ballMats[2], {-4.f, 0.f, 0.f});
    }

    // ---- Stage 4, STACK: a resting tower, and a ball rolled into a
    // wall of boxes ----
    {
        const float s = 0.5f;
        for (int i = 0; i < 5; i++)
            addBox("Tower box", {12.f, s + i * 2.f * s + 0.01f * i, 4.f},
                   glm::vec3(s), {0, 0, 0}, 1.f, 0.1f, boxMats[i % 3]);

        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 4 - row; col++)
                addBox("Wall box",
                       {4.f + col * 2.f * s + row * s,
                        s + row * 2.f * s + 0.01f * row, 10.f},
                       glm::vec3(s), {0, 0, 0}, 1.f, 0.1f,
                       boxMats[(row + col) % 3]);
        addBall("Wrecking ball", {13.f, 1.f, 10.f}, 1.f, 6.f, 0.3f, ballMats[3],
                {-8.f, 0.f, 0.f});
    }

    auto camNode = scn::Node::create("Camera", glm::vec3{0.f, 14.f, 26.f});
    auto camera =
        camNode->add_component<component::Camera>(60.f, 1.f, 1.f);
    camNode->add_component<component::UserMovement>(8.f, 0.1f);
    scene.root->add_child(camNode);
    scene.activeCamera = camera;
    camera->lookAt({0.f, 2.f, 0.f});

    return scene;
}

} // namespace scenes
