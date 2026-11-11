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

namespace {
// glPolygonOffset of the shadow pass (factor, units)
constexpr float SHADOW_SLOPE_BIAS = 1.5f;
constexpr float SHADOW_CONSTANT_BIAS = 2.f;
} // namespace

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
        render::Material::skyTint = sky->tint;
    } else {
        render::Material::skyTint = glm::vec3(1.f);
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

    // A light's map(s): the array (shadow.z) and its first layer there
    // (shadow.x, -1 = none), as the shadow_pass laid them out
    auto layerOf = [&](const std::shared_ptr<component::Light> &light) {
        for (size_t i = 0; i < shadowLights.size(); i++)
            if (shadowLights[i].lock() == light)
                return glm::vec2((float)shadowSlots[i].layer,
                                 (float)shadowSlots[i].bank);
        return glm::vec2(-1.f, 0.f);
    };

    // A shadow-only directional light is the sun's close-up map
    float closeUp = -1.f;
    for (const auto &light : getAllComponents<component::Light>())
        if (light->enabled && light->shadowOnly &&
            light->type == component::LightType::Directional)
            closeUp = layerOf(light).x;

    for (const auto &light : getAllComponents<component::Light>()) {
        if (!light->enabled || light->shadowOnly)
            continue; // shadow-only lights add no light
        auto gpu = light->toGpu();
        const glm::vec2 map = layerOf(light);
        gpu.shadow.x = map.x;
        gpu.shadow.z = map.y;
        // The size of a map texel in the world, for lit.fs's normal offset:
        // cutOff.z, metres (w = 0) or metres per metre from the light (w = 1)
        if (map.x >= 0.f) {
            using render::ShadowMaps;
            switch (light->type) {
            case component::LightType::Directional:
                gpu.cutOff.z = 2.f * light->shadowExtent /
                               (float)ShadowMaps::sunResolution();
                break;
            case component::LightType::Spot:
                gpu.cutOff.z =
                    2.f * std::tan(glm::radians(light->outerCutOff)) /
                    (float)ShadowMaps::smallResolution();
                gpu.cutOff.w = 1.f;
                break;
            case component::LightType::Point: // 90 degree faces
                gpu.cutOff.z = 2.f / (float)ShadowMaps::smallResolution();
                gpu.cutOff.w = 1.f;
                break;
            }
        }
        if (light->type == component::LightType::Directional)
            gpu.shadow.y = closeUp;
        lights.push_back(gpu);
    }

    lightBuffer.upload(lights, viewPos); // upload to UBO
}

void Scene::shadow_pass() {
    using render::ShadowMaps;
    // One entry per layer: a point light appears 6 times (once per face).
    // The suns' maps go in the big array, the rest in the small one; the
    // suns' layers first.
    struct Caster {
        std::shared_ptr<component::Light> light;
        int face;
        ShadowMaps::Bank bank;
        size_t layer; // in its array
    };
    std::vector<Caster> casters;
    size_t used[2] = {0, 0};
    for (auto &light : getAllComponents<component::Light>()) {
        if (!light->enabled || !light->castsShadow)
            continue;
        const auto bank = ShadowMaps::bankOf(
            light->type == component::LightType::Directional);
        if (used[bank] + light->shadowLayers() > ShadowMaps::maxLayers(bank))
            continue;
        for (int f = 0; f < light->shadowLayers(); f++)
            casters.push_back({light, f, bank, used[bank]++});
    }
    std::stable_sort(casters.begin(), casters.end(),
                     [](const Caster &a, const Caster &b) {
                         return a.bank < b.bank;
                     });

    const bool wiped[2] = {shadowMaps->sync(ShadowMaps::Sun, used[0]),
                           shadowMaps->sync(ShadowMaps::Small, used[1])};
    shadowLights.clear();
    shadowSlots.clear();
    for (const Caster &c : casters) {
        shadowLights.push_back(c.light);
        shadowSlots.push_back({c.bank, c.layer});
    }
    shadowMatrices.resize(used[0]);
    smallShadowMatrices.resize(used[1]);

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
    // Front faces into the map (back faces culled), so a shadow starts right
    // where a model stands on the ground. Pushed a little deeper, more on
    // slopes, where a texel covers more depth; lit.fs also looks the depth up
    // a texel out along the surface's normal, so nothing shadows itself.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(SHADOW_SLOPE_BIAS, SHADOW_CONSTANT_BIAS);
    for (const Caster &caster : casters) {
        const auto &light = caster.light;
        if (light->bakedShadow && !light->shadowDirty && !wiped[caster.bank])
            continue;
        const auto proj = light->shadowProjection();
        const auto view = light->shadowView(caster.face);
        (caster.bank == ShadowMaps::Sun ? shadowMatrices
                                        : smallShadowMatrices)[caster.layer] =
            proj * view;

        shadowMaps->bindLayer(caster.bank, caster.layer);
        glClear(GL_DEPTH_BUFFER_BIT);
        // A shadow-only sun draws only the casters under its parent node, the
        // other suns draw the rest. Point and spot lights draw everything.
        const bool sun = light->type == component::LightType::Directional;
        scn::Node *own = nullptr;
        if (light->shadowOnly)
            own = light->node.lock()->parent.lock().get();
        for (auto &shadow : shadowCasters)
            if (shadow->enabled &&
                (light->bakedShadow || !shadow->bakedOnly) &&
                (!sun || groupOf(shadow) == own))
                shadow->drawShadow(proj, view, depthShader,
                                   depthShaderInstanced);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    // After the loop, a point light has 6 faces to render
    for (const Caster &caster : casters)
        caster.light->shadowDirty = false;

    render::Framebuffer::unbind(viewport[2], viewport[3]);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    shadowMaps->uploadGPU(shadowMatrices, smallShadowMatrices);
}
}; // namespace scn
