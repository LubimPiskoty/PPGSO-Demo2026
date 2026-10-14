#include "barrel_run.hpp"

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
#include <random>

namespace scenes {

namespace {

// Barrel models have their origin at the bottom. The body node sits at the
// barrel's center (so it rotates around it) and the mesh is a child moved
// down by this much.
const glm::vec3 kBarrelCenter{0.025f, 0.42f, 0.014f};
const float kBarrelRadius = 0.42f;

// Drops a barrel every `interval` seconds. Past `maxBarrels` the oldest is
// recycled back to the top.
class BarrelSpawner : public component::ComponentBase<BarrelSpawner> {
public:
  std::vector<std::shared_ptr<render::Model>> models;
  std::shared_ptr<render::Material> material;
  float interval = 0.6f;
  std::size_t maxBarrels = 60;
  glm::vec3 dropPoint{0.f};
  float dropSpread = 0.8f; // random z offset around dropPoint

  void update(float dt) override {
    timer += dt;
    while (timer >= interval) {
      timer -= interval;
      spawn();
    }
  }

  const char *type_name() const override { return "BarrelSpawner"; }

  std::string info() const override {
    return std::to_string(barrels.size()) + " barrels";
  }

private:
  float timer = 0.f;
  std::deque<std::shared_ptr<scn::Node>> barrels;
  std::mt19937 rng{42};

  glm::vec3 randomDropPos() {
    std::uniform_real_distribution<float> d(-dropSpread, dropSpread);
    return dropPoint + glm::vec3(d(rng) * 0.5f, 0.f, d(rng));
  }

  void spawn() {
    auto owner = node.lock();
    if (!owner)
      return;

    if (barrels.size() >= maxBarrels) {
      auto barrel = barrels.front();
      barrels.pop_front();
      barrel->setGlobalPosition(randomDropPos());
      barrel->setGlobalRotation(glm::quat(1.f, 0.f, 0.f, 0.f));
      auto body = barrel->get_component<component::PhysicsBody>();
      body->body.velocity = glm::vec3(0.f);
      body->body.angularVelocity = glm::vec3(0.f);
      barrels.push_back(barrel);
      return;
    }

    auto barrel = scn::Node::create("Barrel", randomDropPos());
    auto mesh = scn::Node::create("Barrel mesh", -kBarrelCenter);
    mesh->add_component<component::Mesh>(models[barrels.size() % models.size()],
                                         material);
    barrel->add_child(mesh);

    auto body = barrel->add_component<component::PhysicsBody>();
    body->collider = physics::SphereDesc{kBarrelRadius};
    const float mass = 0.5f;
    body->body.mass = mass;
    body->body.invMass = 1.f / mass;
    body->body.restitution = 0.5f;
    body->body.invInertiaLocal =
        glm::mat3(1.f / (0.4f * mass * kBarrelRadius * kBarrelRadius));
    body->body.invInertiaWorld = body->body.invInertiaLocal;
    owner->add_child(barrel);
    barrels.push_back(barrel);
  }
};

// Rotation whose local +Y (the plane's colliding side) is `normal` and
// local X is `x` (must be perpendicular to normal)
glm::quat planeFrame(glm::vec3 x, glm::vec3 normal) {
  x = glm::normalize(x);
  normal = glm::normalize(normal);
  return glm::quat_cast(glm::mat3(x, normal, glm::cross(x, normal)));
}

} // namespace

scn::Scene barrelRun() {
  scn::Scene scene;
  scene.physics_context.windVel = {8.f, 0.f, 0.f}; // blows toward +x

  auto gridShader =
      std::make_shared<render::Shader>("grid_lit.vs", "grid_lit.fs");
  auto grid = loader::loadTexture("grid_dark.png");
  auto makeMaterial = [&](const char *name, glm::vec3 color) {
    auto material = std::make_shared<render::Material>(name, gridShader);
    material->setTexture(render::TextureSlot::Albedo, grid);
    material->diffuse = color;
    material->setFloat("uBrighten", 4.f);
    return material;
  };
  auto floorMat = makeMaterial("Floor", {0.30f, 0.33f, 0.38f});
  auto rampMat = makeMaterial("Ramp", {0.76f, 0.41f, 0.31f});
  auto funnelMat = makeMaterial("Funnel", {0.24f, 0.58f, 0.57f});
  auto wallMat = makeMaterial("Wall", {0.45f, 0.55f, 0.60f});

  // A static finite plane with a matching rectangle mesh (or none)
  auto addPlane = [&](const char *name, glm::vec3 pos, glm::quat rot,
                      glm::vec2 halfSize,
                      std::shared_ptr<render::Material> material) {
    auto node = scn::Node::create(name, pos);
    node->setLocalRot(rot);
    if (material)
      node->add_component<component::Mesh>(
          std::make_shared<render::Model>(loader::createRect(halfSize, 2.f)),
          material);
    auto body = node->add_component<component::PhysicsBody>();
    body->collider = physics::PlaneDesc{halfSize};
    body->body.mass = 0.f;
    body->body.invMass = 0.f;
    body->body.invInertiaLocal = glm::mat3(0.f);
    body->body.invInertiaWorld = glm::mat3(0.f);
    body->body.restitution = 1.f; // neutral, the barrel's value decides
    scene.root->add_child(node);
  };

  // Floor
  {
    auto node = scn::Node::create("Floor", glm::vec3(0.f));
    node->add_component<component::Mesh>(
        std::make_shared<render::Model>(loader::createPlane(40.f, 2.f)),
        floorMat);
    auto body = node->add_component<component::PhysicsBody>();
    body->collider = physics::PlaneDesc{}; // infinite, faces +Y
    body->body.mass = body->body.invMass = 0.f;
    body->body.invInertiaLocal = body->body.invInertiaWorld = glm::mat3(0.f);
    body->body.restitution = 1.f;
    scene.root->add_child(node);
  }

  // ---- Ramps: the first slopes down toward +x, the barrels fly off its
  // end onto the second, which slopes back toward -x into the funnel ----
  const float tilt = glm::radians(20.f);
  const glm::vec2 rampHalf{3.f, 1.5f};
  {
    const glm::vec3 down{std::cos(tilt), -std::sin(tilt), 0.f};
    addPlane("Ramp 1", {-5.f, 10.f, 0.f},
             planeFrame(down, glm::vec3(std::sin(tilt), std::cos(tilt), 0.f)),
             rampHalf, rampMat);
  }
  {
    const glm::vec3 down{-std::cos(tilt), -std::sin(tilt), 0.f};
    addPlane("Ramp 2", {1.f, 7.f, 0.f},
             planeFrame(down, glm::vec3(-std::sin(tilt), std::cos(tilt), 0.f)),
             rampHalf, rampMat);
  }

  // ---- Funnel: a ring of tilted rectangles facing inward and up, wide at
  // the top and narrow at the bottom ----
  {
    const glm::vec3 center{-3.5f, 0.f, 0.f};
    const float topY = 5.f, bottomY = 2.8f;
    const float topR = 2.6f, bottomR = 0.7f; // hole > barrel radius
    const int segments = 12;
    const float step = glm::two_pi<float>() / segments;
    const float slant = std::sqrt((topR - bottomR) * (topR - bottomR) +
                                  (topY - bottomY) * (topY - bottomY));
    // Width taken at the top (widest) plus overlap, so there are no gaps
    const glm::vec2 half{topR * std::tan(step * 0.5f) * 1.1f, slant * 0.5f};

    for (int i = 0; i < segments; i++) {
      const float angle = i * step;
      const glm::vec3 outward{std::sin(angle), 0.f, std::cos(angle)};
      const glm::vec3 x{std::cos(angle), 0.f, -std::sin(angle)};
      // Up along the wall, then the inward normal perpendicular to it
      const glm::vec3 along = glm::normalize(
          outward * (topR - bottomR) + glm::vec3(0.f, topY - bottomY, 0.f));
      glm::vec3 normal = glm::cross(along, x);
      if (glm::dot(normal, outward) > 0.f)
        normal = -normal;

      const float midR = 0.5f * (topR + bottomR);
      const glm::vec3 pos = center + outward * midR +
                            glm::vec3(0.f, 0.5f * (topY + bottomY), 0.f);
      // Front walls have no mesh so the inside stays visible
      const bool front = outward.z > 0.6f;
      addPlane("Funnel wall", pos, planeFrame(x, normal), half,
               front ? nullptr : funnelMat);
    }
  }

  // ---- Wall the wind pushes the barrels into, facing -x ----
  addPlane("Wall", {9.f, 2.f, 0.f},
           planeFrame(glm::vec3(0.f, 0.f, 1.f), glm::vec3(-1.f, 0.f, 0.f)),
           {6.f, 2.f}, wallMat);

  // ---- Spawner above the start of the first ramp ----
  {
    auto texturedShader =
        std::make_shared<render::Shader>("default.vs", "texture.fs");
    auto node = scn::Node::create("Spawner", glm::vec3(0.f));
    auto spawner = node->add_component<BarrelSpawner>();
    for (const char *name : {"SM_PROP_barrel_01.glb", "SM_PROP_barrel_02.glb",
                             "SM_PROP_barrel_03.glb"})
      spawner->models.push_back(
          std::make_shared<render::Model>(loader::loadModel(name)));
    spawner->material =
        std::make_shared<render::Material>("Barrel", texturedShader);
    spawner->dropPoint = {-7.f, 13.f, 0.f};
    scene.root->add_child(node);
  }

  auto camNode = scn::Node::create("Camera", glm::vec3{0.f, 7.f, 17.f});
  auto camera = camNode->add_component<component::Camera>(60.f, 1.f, 1.f);
  scene.root->add_child(camNode);
  scene.activeCamera = camera;
  camera->lookAt({0.f, 5.f, 0.f});

  return scene;
}

} // namespace scenes
