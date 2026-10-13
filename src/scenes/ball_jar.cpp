#include "ball_jar.hpp"

#include "../components/camera.hpp"
#include "../components/mesh.hpp"
#include "../components/physicsbody.hpp"
#include "../loaders/model_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/material.hpp"

#include <cmath>
#include <deque>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <random>

namespace scenes {

namespace {

// Drops a ball every `interval` seconds as a child of its node (which sits
// at the origin, so child local == world). Past `maxBalls` the oldest ball
// is recycled back to the top instead, keeping the body count bounded for
// the O(n^2) broad phase.
class BallSpawner : public component::ComponentBase<BallSpawner> {
  public:
    std::shared_ptr<render::Model> model;
    std::vector<std::shared_ptr<render::Material>> materials;
    float radius = 0.3f;
    float interval = 0.1f;
    std::size_t maxBalls = 500;
    glm::vec3 dropPoint{0.f, 9.f, 0.f};
    float dropSpread = 1.5f; // random xz offset around dropPoint

    void update(float dt) override {
        timer += dt;
        while (timer >= interval) {
            timer -= interval;
            spawn();
        }

        // Once a second, warn if the average fps was below 60
        fpsTimer += dt;
        fpsFrames++;
        if (fpsTimer >= 1.f) {
            float fps = fpsFrames / fpsTimer;
            if (fps < 60.f)
                std::cout << "[ball jar] fps dropped to " << (int)fps
                          << " with " << balls.size() << " balls\n";
            fpsTimer = 0.f;
            fpsFrames = 0;
        }
    }

    const char *type_name() const override {
        return "BallSpawner";
    }

    std::string info() const override {
        return std::to_string(balls.size()) + " balls";
    }

  private:
    float timer = 0.f;
    float fpsTimer = 0.f;
    int fpsFrames = 0;
    std::deque<std::shared_ptr<scn::Node>> balls;
    std::mt19937 rng{1234};

    glm::vec3 randomDropPos() {
        std::uniform_real_distribution<float> d(-dropSpread, dropSpread);
        return dropPoint + glm::vec3(d(rng), 0.f, d(rng));
    }

    void spawn() {
        auto owner = node.lock();
        if (!owner)
            return;

        if (balls.size() >= maxBalls) {
            // Recycle the oldest ball
            auto ball = balls.front();
            balls.pop_front();
            ball->setLocalPos(randomDropPos());
            auto body = ball->get_component<component::PhysicsBody>();
            body->body.velocity = glm::vec3(0.f);
            body->body.angularVelocity = glm::vec3(0.f);
            balls.push_back(ball);
            return;
        }

        auto ball = scn::Node::create("Ball", randomDropPos());
        ball->add_component<component::Mesh>(
            model, materials[balls.size() % materials.size()]);
        auto body = ball->add_component<component::PhysicsBody>();
        body->collider = physics::SphereDesc{radius};
        const float mass = 1.f;
        body->body.mass = mass;
        body->body.invMass = 1.f / mass;
        body->body.restitution = 0.4f;
        body->body.invInertiaLocal =
            glm::mat3(1.f / (0.4f * mass * radius * radius));
        body->body.invInertiaWorld = body->body.invInertiaLocal;
        owner->add_child(ball);
        balls.push_back(ball);
    }
};

} // namespace

scn::Scene ballJar() {
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

    auto floorMat = makeMaterial("Floor", {0.30f, 0.33f, 0.38f});
    auto jarMat = makeMaterial("Jar", {0.45f, 0.55f, 0.60f});

    auto makeStatic = [](component::PhysicsBody &body) {
        body.body.mass = 0.f;
        body.body.invMass = 0.f;
        body.body.invInertiaLocal = glm::mat3(0.f);
        body.body.invInertiaWorld = glm::mat3(0.f);
        body.body.restitution = 1.f; // neutral, the ball's value decides
    };

    // Floor
    {
        auto node = scn::Node::create("Floor", glm::vec3(0.f));
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::createPlane(40.f, 2.f)),
            floorMat);
        auto body = node->add_component<component::PhysicsBody>();
        body->collider = physics::PlaneDesc{}; // infinite, faces +Y
        makeStatic(*body);
        scene.root->add_child(node);
    }

    // ---- Jar: a ring of finite planes facing inward. The floor is its
    // bottom, the base is just a visual patch on it. ----
    const float jarRadius = 2.5f;
    const float jarHeight = 4.f;
    const int segments = 12;

    {
        auto node = scn::Node::create("Jar base", glm::vec3(0.f, 0.01f, 0.f));
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::createPlane(2.f * jarRadius, 2.f)),
            jarMat);
        scene.root->add_child(node);
    }

    // Each wall is a rectangle tangent to the circle, slightly wider than
    // its side of the polygon so neighbours overlap and leave no gaps
    const float step = glm::two_pi<float>() / segments;
    const glm::vec2 wallHalf{jarRadius * std::tan(step * 0.5f) * 1.1f,
                             jarHeight * 0.5f};
    for (int i = 0; i < segments; i++) {
        const float angle = i * step;
        const glm::vec3 outward{std::sin(angle), 0.f, std::cos(angle)};
        // Plane frame: local +Y (the colliding side) faces the jar's center,
        // local X runs along the wall, local Z points up
        const glm::vec3 x{std::cos(angle), 0.f, -std::sin(angle)};
        const glm::vec3 y = -outward;
        const glm::mat3 frame(x, y, glm::cross(x, y));

        auto node = scn::Node::create(
            "Jar wall", outward * jarRadius + glm::vec3(0.f, wallHalf.y, 0.f));
        node->setLocalRot(glm::quat_cast(frame));
        // Walls facing the camera (+z side) get no mesh so the inside of the
        // jar stays visible; they still collide
        const bool front = outward.z > 0.6f;
        if (!front)
            node->add_component<component::Mesh>(
                std::make_shared<render::Model>(
                    loader::createRect(wallHalf, 2.f)),
                jarMat);
        auto body = node->add_component<component::PhysicsBody>();
        body->collider = physics::PlaneDesc{wallHalf};
        makeStatic(*body);
        scene.root->add_child(node);
    }

    // ---- Spawner ----
    {
        auto node = scn::Node::create("Spawner", glm::vec3(0.f));
        auto spawner = node->add_component<BallSpawner>();
        spawner->model = std::make_shared<render::Model>(
            loader::createSphere(spawner->radius, 2.f));
        spawner->materials = {
            makeMaterial("Terracotta", {0.76f, 0.41f, 0.31f}),
            makeMaterial("Teal", {0.24f, 0.58f, 0.57f}),
            makeMaterial("Mustard", {0.80f, 0.65f, 0.27f}),
            makeMaterial("Plum", {0.58f, 0.38f, 0.62f}),
        };
        spawner->dropPoint = {0.f, jarHeight + 5.f, 0.f};
        spawner->dropSpread = jarRadius * 0.6f;
        scene.root->add_child(node);
    }

    // Camera above and in front, looking down into the jar
    auto camNode = scn::Node::create("Camera", glm::vec3{0.f, 6.5f, 7.f});
    auto camera = camNode->add_component<component::Camera>(60.f, 1.f, 1.f);
    scene.root->add_child(camNode);
    scene.activeCamera = camera;
    camera->lookAt({0.f, 3.f, 0.f});

    return scene;
}

} // namespace scenes
