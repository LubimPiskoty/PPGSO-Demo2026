#include "scene.hpp"

#include "../components/camera.hpp"
#include "../components/light.hpp"
#include "../components/shadow.hpp"
#include "../components/skybox.hpp"
#include "../debug/profiler.hpp"
#include "../render/framebuffer.hpp"
#include "../render/material.hpp"
#include <algorithm>
#include <cmath>
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

void Scene::update(float dt) {
    const float scale = std::max(physics_context.timeScale, 0.f);
    // Shader effects (waves, wind) speed up only by the square root: sped
    // up 4x they move 2x, so it shows without them looking frantic
    render::Material::time += dt * std::sqrt(scale);
    // Animations (the day / night cycle) follow the timescale exactly
    this->root->update(dt * scale);
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

void Scene::physics_step() {
    // Gather physics bodies
    physics_context.bodies.clear();
    gatherPhysicsBodies(root);
    profiler::addCount(profiler::PhysicsCounter::Bodies,
                       (int)physics_context.bodies.size());

    auto &ctx = physics_context;
    if (ctx.timeScale <= 0.f)
        return; // Paused

    const float dt = ctx.fixedDt * ctx.timeScale;
    if (ctx.windPulsePeriod > 0.f) {
        // Squeeze (smooth hump) over the first 45 % of the period, then the
        // bellows refill and the air is still
        constexpr float squeeze = 0.45f;
        ctx.windPulseTime += dt;
        const float phase =
            std::fmod(ctx.windPulseTime / ctx.windPulsePeriod, 1.f);
        const float pulse =
            phase < squeeze
                ? std::sin(glm::pi<float>() * phase / squeeze)
                : -ctx.windReturn * std::sin(glm::pi<float>() * (phase - squeeze) /
                                             (1.f - squeeze));
        ctx.windVel = ctx.windPeak * pulse;
    }
    physics::step(ctx, dt);
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

    // First layer of a light's map(s) in the shadow_pass casters, -1 = none
    auto layerOf = [&](const std::shared_ptr<component::Light> &light) {
        for (size_t i = 0; i < shadowLights.size(); i++)
            if (shadowLights[i].lock() == light)
                return (float)i;
        return -1.f;
    };

    // A shadow-only directional light is the sun's close-up map
    float closeUp = -1.f;
    for (const auto &light : getAllComponents<component::Light>())
        if (light->enabled && light->shadowOnly &&
            light->type == component::LightType::Directional)
            closeUp = layerOf(light);

    for (const auto &light : getAllComponents<component::Light>()) {
        if (!light->enabled || light->shadowOnly)
            continue; // shadow-only lights add no light
        auto gpu = light->toGpu();
        gpu.shadow.x = layerOf(light);
        if (light->type == component::LightType::Directional)
            gpu.shadow.y = closeUp;
        lights.push_back(gpu);
    }

    lightBuffer.upload(lights, viewPos); // upload to UBO
}

void Scene::shadow_pass() {
    // One entry per layer: a point light appears 6 times (once per face)
    std::vector<std::shared_ptr<component::Light>> casters;
    std::vector<int> faces;
    for (auto &light : getAllComponents<component::Light>()) {
        if (!light->enabled || !light->castsShadow)
            continue;
        if (casters.size() + light->shadowLayers() >
            render::ShadowMaps::MAX_SHADOW_LIGHTS)
            continue;
        for (int f = 0; f < light->shadowLayers(); f++) {
            casters.push_back(light);
            faces.push_back(f);
        }
    }

    const bool wiped = shadowMaps->sync(casters.size());
    shadowLights.assign(casters.begin(), casters.end());
    shadowMatrices.resize(casters.size());

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Only nodes with a Shadow component are rendered into the maps
    const auto shadowCasters = getAllComponents<component::Shadow>();
    // The parent node of the shadow-only light above a caster, if any
    auto groupOf = [&](const std::shared_ptr<component::Shadow> &shadow) {
        for (auto n = shadow->node.lock(); n; n = n->parent.lock())
            for (auto &c : n->children)
                if (auto l = c->get_component<component::Light>())
                    if (l->shadowOnly)
                        return n.get();
        return (scn::Node *)nullptr;
    };
    // Front faces culled: the map holds the back sides, so a lit surface is
    // never compared with its own depth (no bias needed). Open surfaces
    // (Shadow::open) have no back sides, they draw their front faces.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    for (size_t i = 0; i < casters.size(); i++) {
        if (casters[i]->bakedShadow && !casters[i]->shadowDirty && !wiped)
            continue;
        const auto proj = casters[i]->shadowProjection();
        const auto view = casters[i]->shadowView(faces[i]);
        shadowMatrices[i] = proj * view;

        shadowMaps->bindLayer(i, casters[i]->type == component::LightType::Point);
        glClear(GL_DEPTH_BUFFER_BIT);
        // A shadow-only sun draws only the casters under its parent node, the
        // other suns draw the rest. Point and spot lights draw everything.
        const bool sun = casters[i]->type == component::LightType::Directional;
        scn::Node *own = nullptr;
        if (casters[i]->shadowOnly)
            own = casters[i]->node.lock()->parent.lock().get();
        for (auto &shadow : shadowCasters)
            if (shadow->enabled &&
                (casters[i]->bakedShadow || !shadow->bakedOnly) &&
                (!sun || groupOf(shadow) == own)) {
                if (shadow->open) {
                    glCullFace(GL_BACK);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    // Deeper by a bit more on slopes, where a texel covers
                    // more depth
                    glPolygonOffset(2.f, 4.f);
                }
                shadow->drawShadow(proj, view, depthShader,
                                   depthShaderInstanced);
                if (shadow->open) {
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glCullFace(GL_FRONT);
                }
            }
    }
    glCullFace(GL_BACK);
    shadowMaps->endLayers();
    // After the loop, a point light has 6 faces to render
    for (auto &light : casters)
        light->shadowDirty = false;

    render::Framebuffer::unbind(viewport[2], viewport[3]);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    shadowMaps->uploadGPU(shadowMatrices);
}
}; // namespace scn
