#include "scene.hpp"

#include "../components/camera.hpp"
#include "../components/light.hpp"
#include "../debug/profiler.hpp"
#include <cstdio>
#include <memory>
#include <vector>

namespace scn {

Scene::Scene() { this->root = Node::create("root"); }

void Scene::draw() {
    if (activeCamera.expired()) {
        std::puts("No camera is active!! Output will be blank");
        return;
    }
    uploadLights(); // upload all lights before draw
    if (auto sky = skybox.lock())
        sky->draw(activeCamera.lock()); // skybox last
    const auto camera_projection = activeCamera.lock()->projection;
    const auto inv_camera_transform =
        glm::inverse(activeCamera.lock()->node.lock()->globalTransform());
    this->root->draw(camera_projection, inv_camera_transform);
}

void Scene::update(float dt) { this->root->update(dt); }

std::shared_ptr<Node> Scene::findByGuid(util::Guid guid) const {
    std::vector<std::shared_ptr<Node>> stack{root};
    while (!stack.empty()) {
        auto node = stack.back();
        stack.pop_back();
        if (node->guid == guid)
            return node;
        for (auto const &child : node->children)
            stack.push_back(child);
    }
    return nullptr;
}

void Scene::physics_step() {
    // Gather physics bodies
    physics_context.bodies.clear();
    gatherPhysicsBodies(root);
    profiler::addCount(profiler::PhysicsCounter::Bodies,
                       (int)physics_context.bodies.size());

    auto &ctx = physics_context;
    if (ctx.timeScale <= 0.f)
        return; // Paused

    physics::step(ctx, ctx.fixedDt * ctx.timeScale);
}

void Scene::gatherPhysicsBodies(std::shared_ptr<Node> node) {
    if (!node->enabled)
        return;

    auto pBody = node->get_component<component::PhysicsBody>();
    if (pBody)
        physics_context.bodies.push_back(pBody.get());

    for (const auto &child : node->children)
        gatherPhysicsBodies(child);
}

void Scene::uploadLights() {
    std::vector<render::GpuLight> lights; // vector of all lights
    glm::vec3 viewPos{0.f};

    // camera position for specular calc
    if (auto cam = activeCamera.lock())
        if (auto camNode = cam->node.lock())
            viewPos = camNode->globalPosition();

    // go throught all nodes and take lioghts
    std::vector<std::shared_ptr<Node>> stack{root};
    while (!stack.empty()) {
        auto node = stack.back();
        stack.pop_back();
        if (!node->enabled)
            continue;

        auto light = node->get_component<component::Light>(); // get the light
        if (light && light->enabled)
            lights.push_back(light->toGpu());

        for (auto &child : node->children) // look for child components
            stack.push_back(child);
    }

    lightBuffer.upload(lights, viewPos); // upload to UBOO
}

}; // namespace scn
