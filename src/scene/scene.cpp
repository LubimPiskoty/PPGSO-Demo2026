#include "scene.hpp"

#include "../components/camera.hpp"
#include "../components/light.hpp"
#include "../components/shadow.hpp"
#include "../components/skybox.hpp"
#include "../debug/profiler.hpp"
#include "../render/framebuffer.hpp"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace scn {

Scene::Scene()
    : depthShader("depth.vs", "depth.fs"),
      depthShaderInstanced("depth_instanced.vs", "depth.fs", true) {
    this->root = Node::create("root");
}

void Scene::draw() {
    if (activeCamera.expired()) {
        std::puts("No camera is active!! Output will be blank");
        return;
    }

    uploadLights(); // upload all lights before draw
    if (auto sky = skybox.lock()) {
        sky->draw(activeCamera.lock()); // skybox last
        sky->bindForReflections();      // cubemapa on 4th slot for reflectz
    }
    shadowMaps->bindForSampling(); // depth array on its own slot

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

void Scene::gatherPhysicsBodies(const std::shared_ptr<Node> &node) {
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

    for (const auto &light : getAllComponents<component::Light>()) {
        if (!light->enabled)
            continue;
        auto gpu = light->toGpu();

        // Layer of this light's map: its position in the shadow_pass casters
        for (size_t i = 0; i < shadowLights.size(); i++) {
            if (shadowLights[i].lock() == light) {
                gpu.shadow.x = (float)i;
                break;
            }
        }
        lights.push_back(gpu);
    }

    lightBuffer.upload(lights, viewPos); // upload to UBO
}

void Scene::shadow_pass() {
    std::vector<std::shared_ptr<component::Light>> casters;
    for (auto &light : getAllComponents<component::Light>()) {
        if (light->enabled && light->castsShadow &&
            light->type != component::LightType::Point &&
            casters.size() < render::ShadowMaps::MAX_SHADOW_LIGHTS)
            casters.push_back(light);
    }

    shadowMaps->sync(casters.size());
    shadowLights.assign(casters.begin(), casters.end());

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Directional shadow boxes follow the camera
    glm::vec3 focus{0.f};
    if (auto cam = activeCamera.lock())
        if (auto camNode = cam->node.lock())
            focus = camNode->globalPosition();

    // Only nodes with a Shadow component are rendered into the maps
    const auto shadowCasters = getAllComponents<component::Shadow>();
    glCullFace(GL_FRONT);
    std::vector<glm::mat4> shadowMat;
    for (size_t i = 0; i < casters.size(); i++) {
        shadowMaps->bindLayer(i);
        glClear(GL_DEPTH_BUFFER_BIT);
        const auto proj = casters[i]->shadowProjection();
        const auto view = casters[i]->shadowView(focus);
        shadowMat.push_back(proj * view);

        for (auto &shadow : shadowCasters)
            if (shadow->enabled)
                shadow->drawShadow(proj, view, depthShader,
                                   depthShaderInstanced);
    }
    glCullFace(GL_BACK);

    render::Framebuffer::unbind(viewport[2], viewport[3]);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    shadowMaps->uploadGPU(shadowMat);
}
}; // namespace scn
