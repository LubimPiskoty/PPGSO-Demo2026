#include "scene.hpp"

#include <cstdio>
#include <memory>
#include <vector>

namespace scn {

Scene::Scene() {
    this->root = Node::create("root");
}

void Scene::draw() {
    if (activeCamera.expired()) {
        std::puts("No camera is active!! Output will be blank");
        return;
    }
    this->root->draw(activeCamera.lock());
}

void Scene::update(float dt) {
    this->root->update(dt);
}

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

void Scene::physics_step(float frameDt) {
    // Gather physics bodies
    physics_context.bodies.clear();
    gatherPhysicsBodies(root);

    auto &ctx = physics_context;
    ctx.accumulator += frameDt * ctx.timeScale;

    // Cap the catch-up so a stall doesn't run hundreds of steps
    const int maxSteps = 8;
    int steps = 0;
    while (ctx.accumulator >= ctx.fixedDt && steps < maxSteps) {
        physics::step(ctx, ctx.fixedDt);
        ctx.accumulator -= ctx.fixedDt;
        steps++;
    }
    if (steps == maxSteps)
        ctx.accumulator = 0.f; // drop the backlog
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
}; // namespace scn
