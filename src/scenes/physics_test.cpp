#include "physics_test.hpp"

#include "../components/camera.hpp"
#include "../components/mesh.hpp"
#include "../components/physicsbody.hpp"
#include "../loaders/model_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/material.hpp"

#include <cmath>
#include <glm/gtc/constants.hpp>

namespace scenes {

namespace {

// Shared setup for the physics demo scenes: materials, body helpers, the
// walled arena and a fixed camera. Everything uses the grid texture, tinted
// per material.
class TestSceneBuilder {
  public:
    scn::Scene scene;

    std::shared_ptr<render::Material> floorMat, wallMat, rampMat;
    std::shared_ptr<render::Material> ballMats[7];

    TestSceneBuilder() {
        shader = std::make_shared<render::Shader>("grid_lit.vs", "grid_lit.fs");
        grid = loader::loadTexture("grid_dark.png");

        // Muted tones, between pastel and fully saturated
        floorMat = makeMaterial("Floor", {0.30f, 0.33f, 0.38f});
        wallMat = makeMaterial("Wall", {0.42f, 0.45f, 0.50f});
        rampMat = makeMaterial("Ramp", {0.36f, 0.39f, 0.44f});
        ballMats[0] = makeMaterial("Terracotta", {0.76f, 0.41f, 0.31f});
        ballMats[1] = makeMaterial("Teal", {0.24f, 0.58f, 0.57f});
        ballMats[2] = makeMaterial("Mustard", {0.80f, 0.65f, 0.27f});
        ballMats[3] = makeMaterial("Plum", {0.58f, 0.38f, 0.62f});
        ballMats[4] = makeMaterial("Sage", {0.50f, 0.65f, 0.44f});
        ballMats[5] = makeMaterial("Steel blue", {0.31f, 0.49f, 0.72f});
        ballMats[6] = makeMaterial("Rust", {0.80f, 0.48f, 0.33f});
    }

    // Floor plus four walls at +-halfX / +-halfZ. The wall facing the
    // camera (+z) is a collider only so it doesn't block the view.
    void addArena(float halfX, float halfZ) {
        const float half = glm::half_pi<float>();
        // Wall meshes are squares, sized to cover the longer side. Centered
        // at half their size so they start exactly at the floor.
        const float size = 2.f * glm::max(halfX, halfZ);
        const float wallH = size * 0.5f;
        // The floor mesh is much larger than the arena, so the ground in
        // front of the open (meshless) front wall is covered too, all the
        // way to the camera. Colliders are infinite planes either way.
        const float floorSize = 40.f;
        // Rotations turn each plane's +Y (its colliding side) inward
        addPlane("Floor", {0, 0, 0}, {0, 0, 0}, floorSize, floorMat);
        addPlane("Back wall", {0, wallH, -halfZ}, {half, 0, 0}, size, wallMat);
        addPlane("Left wall", {-halfX, wallH, 0}, {0, 0, -half}, size, wallMat);
        addPlane("Right wall", {halfX, wallH, 0}, {0, 0, half}, size, wallMat);
        addPlane("Front wall", {0, wallH, halfZ}, {-half, 0, 0}, size, nullptr);
    }

    // Static infinite plane through `center`, facing the node's local +Y
    // after `rotEuler`. The visual is a size x size createPlane; null
    // material = collider only.
    void addPlane(const char *name, glm::vec3 center, glm::vec3 rotEuler,
                  float size, std::shared_ptr<render::Material> material) {
        auto node = scn::Node::create(name, center);
        node->setLocalRotEuler(rotEuler);
        if (material)
            node->add_component<component::Mesh>(
                std::make_shared<render::Model>(loader::createPlane(size, 2.f)),
                material);
        addStaticBody(node, physics::PlaneDesc{}, 1.f);
    }

    // Static finite plane (rectangle) of 2 * halfSize, otherwise like
    // addPlane. For ramps and pads.
    void addRect(const char *name, glm::vec3 center, glm::vec3 rotEuler,
                 glm::vec2 halfSize, float restitution,
                 std::shared_ptr<render::Material> material) {
        auto node = scn::Node::create(name, center);
        node->setLocalRotEuler(rotEuler);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::createRect(halfSize, 2.f)),
            material);
        addStaticBody(node, physics::PlaneDesc{halfSize}, restitution);
    }

    // Returns the body so scenes can tweak more (e.g. friction)
    std::shared_ptr<component::PhysicsBody>
    addBall(const char *name, glm::vec3 pos, float radius, float mass,
            float restitution, std::shared_ptr<render::Material> material,
            glm::vec3 velocity = glm::vec3(0.f)) {
        auto node = scn::Node::create(name, pos);
        // node->add_component<component::Mesh>(
        //     std::make_shared<render::Model>(loader::createSphere(radius, 2.f)),
        //     material);
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
        return body;
    }

    // Fixed camera, no user movement
    void addCamera(glm::vec3 pos, glm::vec3 target) {
        auto camNode = scn::Node::create("Camera", pos);
        auto camera = camNode->add_component<component::Camera>(60.f, 1.f, 1.f);
        scene.root->add_child(camNode);
        scene.activeCamera = camera;
        camera->lookAt(target);
    }

  private:
    std::shared_ptr<render::Shader> shader;
    std::shared_ptr<render::Texture> grid;

    std::shared_ptr<render::Material> makeMaterial(const char *name,
                                                   glm::vec3 color) {
        auto material = std::make_shared<render::Material>(name, shader);
        material->setTexture(render::TextureSlot::Albedo, grid);
        material->diffuse = color;
        material->setFloat("uBrighten", 4.f);
        return material;
    }

    // Restitution 1.0 is neutral: with min() combining, the ball's own
    // value decides the bounce
    void addStaticBody(const std::shared_ptr<scn::Node> &node,
                       physics::PlaneDesc collider, float restitution) {
        auto body = node->add_component<component::PhysicsBody>();
        body->collider = collider;
        body->body.mass = 0.f;
        body->body.invMass = 0.f;
        body->body.invInertiaLocal = glm::mat3(0.f);
        body->body.invInertiaWorld = glm::mat3(0.f);
        body->body.restitution = restitution;
        scene.root->add_child(node);
    }
};

} // namespace

scn::Scene physicsBounce() {
    TestSceneBuilder b;
    b.addArena(7.f, 4.f);

    // Same ball, same height, only restitution differs. Pads are neutral
    // (1.0) so each ball's own value decides the bounce.
    const float restitutions[] = {0.2f, 0.5f, 0.8f, 0.95f};
    for (int i = 0; i < 4; i++) {
        float x = -3.75f + i * 2.5f;
        b.addRect("Bounce pad", {x, 0.2f, 0.f}, {0, 0, 0}, {1.f, 1.f}, 1.f,
                  b.rampMat);
        b.addBall("Bounce ball", {x, 8.f, 0.f}, 0.5f, 1.f, restitutions[i],
                  b.ballMats[i]);
    }

    b.addCamera({0.f, 4.5f, 10.f}, {0.f, 3.f, 0.f});
    return std::move(b.scene);
}

scn::Scene physicsSlide() {
    TestSceneBuilder b;
    b.addArena(8.f, 6.f);

    // Finite planes as ramps, descending toward -x, side by side along z,
    // with a ball near the top of each. Friction makes the balls roll
    // instead of slide.
    const float angles[] = {15.f, 30.f, 45.f};
    const glm::vec2 rampHalf{3.f, 1.2f};
    for (int i = 0; i < 3; i++) {
        float theta = glm::radians(angles[i]);
        // Steepest (tallest) ramp at the back so it doesn't hide the others
        float z = 3.f - i * 3.f;
        // Raise the ramp so its low end just touches the floor
        glm::vec3 center{1.f, rampHalf.x * std::sin(theta) + 0.02f, z};
        b.addRect("Ramp", center, {0.f, 0.f, theta}, rampHalf, 0.2f, b.rampMat);

        // Point `along` units uphill from the ramp's center, `lift` above
        // its surface
        const glm::vec3 up{-std::sin(theta), std::cos(theta), 0.f};
        const glm::vec3 dir{std::cos(theta), std::sin(theta), 0.f};
        const float radius = 0.35f;
        b.addBall("Roll ball", center + dir * 2.f + up * (radius + 0.02f),
                  radius, 1.f, 0.2f, b.ballMats[i]);
    }

    // Centered on the ramps (they span x -2..4)
    b.addCamera({1.f, 4.f, 8.5f}, {1.f, 1.f, 0.f});
    return std::move(b.scene);
}

scn::Scene physicsImpact() {
    TestSceneBuilder b;
    // Side walls close to the start positions (balls at +-4) so the
    // rebounds hit them inside the camera view
    b.addArena(5.5f, 4.f);

    // Launched fast and fairly close together, so drag and rolling
    // friction don't slow them down much before they meet (~0.4 s)

    // Heavy vs light, at the back so the big ball doesn't hide the other
    // pair: the light ball gets thrown back
    b.addBall("Impact heavy", {-4.f, 1.f, -2.f}, 1.f, 8.f, 0.9f, b.ballMats[3],
              {10.f, 0.f, 0.f});
    b.addBall("Impact light", {4.f, 0.4f, -2.f}, 0.4f, 0.5f, 0.9f,
              b.ballMats[2], {-10.f, 0.f, 0.f});
    // Equal mass, in front: velocities should swap
    b.addBall("Impact A", {-4.f, 0.5f, 2.f}, 0.5f, 1.f, 0.9f, b.ballMats[0],
              {10.f, 0.f, 0.f});
    b.addBall("Impact B", {4.f, 0.5f, 2.f}, 0.5f, 1.f, 0.9f, b.ballMats[1],
              {-10.f, 0.f, 0.f});

    b.addCamera({0.f, 5.f, 8.5f}, {0.f, 0.5f, 0.f});
    return std::move(b.scene);
}

scn::Scene physicsBreak() {
    TestSceneBuilder b;
    b.addArena(6.f, 6.f);

    // Pool style break: a triangle of 10 balls with gaps between them, and
    // a cue ball rolled into its tip, away from the camera
    const float r = 0.4f;
    const float spacing = 2.f * r + 0.3f;                  // center to center
    const float rowStep = std::sqrt(3.f) * 0.5f * spacing; // equilateral
    //
    // Billiard balls are nearly perfectly elastic and smooth: high
    // restitution, and low friction so ball-ball hits don't turn the balls'
    // rolling spin into vertical hops (the floor still makes them roll,
    // friction combines as sqrt(0.05 * 0.5) ~ 0.16 there). Walls are 1.0, so
    // the cushions use the ball's value too.
    const float restitution = 0.97f;
    const float friction = 0.05f;
    // Almost no air drag, so the balls keep their speed across the arena
    b.scene.physics_context.drag = 0.02f;
    b.scene.physics_context.windVel = {5.f, 0, 0};

    int index = 0;
    for (int row = 0; row < 6; row++)
        for (int i = 0; i <= row; i++)
            b.addBall("Rack ball",
                      {(i - row * 0.5f) * spacing, r, 1.f - row * rowStep}, r,
                      1.f, restitution, b.ballMats[index++ % 7])
                ->body.friction = friction;
    b.addBall("Cue ball", {0.f, r, 4.5f}, r, 4.f, restitution, b.ballMats[0],
              {0.f, 0.f, -15.f})
        ->body.friction = friction;

    b.addCamera({0.f, 7.f, 8.f}, {0.f, 0.f, -0.5f});
    return std::move(b.scene);
}

} // namespace scenes
