#include "scene.hpp"

#include "../components/camera.hpp"
#include "../components/light.hpp"
#include "../components/mesh.hpp"
#include "../components/shadow.hpp"
#include "../components/skybox.hpp"
#include "../debug/profiler.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/framebuffer.hpp"
#include "../render/material.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
// 1x1 opaque white: what depth.fs alpha-tests a caster without an albedo
// of its own against (a model's own albedo replaces it while it draws)
GLuint opaqueTexture() {
    static GLuint texture = 0;
    if (texture == 0) {
        const unsigned char white[4] = {255, 255, 255, 255};
        texture = loader::createTexture(white, 1, 1);
    }
    return texture;
}

// glPolygonOffset of the shadow pass (factor, units)
constexpr float SHADOW_SLOPE_BIAS = 1.5f;
constexpr float SHADOW_CONSTANT_BIAS = 2.f;
} // namespace

namespace scn {

Scene::Scene()
    : depthShader(std::make_unique<render::Shader>("depth.vs", "depth.fs")),
      depthShaderInstanced(std::make_unique<render::Shader>(
          "depth_instanced.vs", "depth.fs", true)) {
    this->root = Node::create("root");
}

void Scene::depth_prepass() {
    if (activeCamera.expired())
        return;
    const auto camera = activeCamera.lock();
    this->root->drawPrepass(
        camera->projection,
        glm::inverse(camera->node.lock()->globalTransform()));
}

void Scene::cullLights(GLuint depthTexture, int width, int height) {
    if (activeCamera.expired())
        return;
    uploadLights();
    if (!lightCulling)
        lightCulling = std::make_unique<render::LightCulling>();
    const auto camera = activeCamera.lock();
    lightCulling->run(depthTexture, width, height,
                      glm::inverse(camera->node.lock()->globalTransform()),
                      camera->projection);
}

void Scene::draw() {
    if (activeCamera.expired()) {
        std::puts("No camera is active!! Output will be blank");
        return;
    }

    if (auto sky = skybox.lock()) {
        // Where the prepass left the depth at 1 (nothing drawn)
        sky->draw(activeCamera.lock());
        sky->bindForReflections();      // cubemapa on 4th slot for reflectz
        render::Material::skyTint = sky->tint;
    } else {
        render::Material::skyTint = glm::vec3(1.f);
    }
    shadowMaps->bindForSampling(); // depth array on its own slot

    const auto camera_projection = activeCamera.lock()->projection;
    const auto inv_camera_transform =
        glm::inverse(activeCamera.lock()->node.lock()->globalTransform());
    // The prepass already wrote the opaque depth: what's drawn again there
    // is equal, and passes
    glDepthFunc(GL_LEQUAL);

    // Transparent meshes are set aside (Node::draw), drawn by
    // drawTransparent once the opaque depth is resolved (main.cpp)
    transparent.clear();
    this->root->draw(camera_projection, inv_camera_transform, transparent);

    glDepthFunc(GL_LESS);
}

void Scene::drawTransparent() {
    if (activeCamera.expired())
        return;
    const auto camera_projection = activeCamera.lock()->projection;
    const auto inv_camera_transform =
        glm::inverse(activeCamera.lock()->node.lock()->globalTransform());
    glDepthFunc(GL_LEQUAL);

    // Transparent last, far to near: each blends over what's behind it
    std::sort(transparent.begin(), transparent.end(),
              [](const auto &a, const auto &b) { return a.viewDepth > b.viewDepth; });
    for (auto &t : transparent)
        t.mesh->draw(camera_projection, inv_camera_transform);

    // Material::bind leaves these set after a transparent one
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glDepthFunc(GL_LESS);
}

void Scene::update(float dt) {
    const float scale = std::max(physics_context.timeScale, 0.f);
    // Shader effects (waves, wind) speed up only by the square root: sped
    // up 4x they move 2x, so it shows without them looking frantic
    render::Material::time += dt * std::sqrt(scale);
    // Animations (the day / night cycle) follow the timescale exactly
    this->root->update(dt * scale);
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

    for (const auto &light : getAllComponents<component::Light>()) {
        if (!light->enabled)
            continue;
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
        lights.push_back(gpu);
    }

    lightBuffer.upload(lights, viewPos); // to the lights SSBO
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
    // Front faces into the map (back faces culled), so a shadow starts right
    // where a model stands on the ground. Pushed a little deeper, more on
    // slopes, where a texel covers more depth; lit.fs also looks the depth up
    // a texel out along the surface's normal, so nothing shadows itself.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(SHADOW_SLOPE_BIAS, SHADOW_CONSTANT_BIAS);
    // Procedural casters (the lamps' shadow discs, spheres, boxes) bring no
    // albedo: without this depth.fs cut them by whatever texture was bound
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, opaqueTexture());
    for (const Caster &caster : casters) {
        const auto &light = caster.light;
        if (light->bakedShadow && !light->shadowDirty && !wiped[caster.bank] &&
            !redrawBakedShadows)
            continue;
        const auto proj = light->shadowProjection();
        const auto view = light->shadowView(caster.face);
        (caster.bank == ShadowMaps::Sun ? shadowMatrices
                                        : smallShadowMatrices)[caster.layer] =
            proj * view;

        shadowMaps->bindLayer(caster.bank, caster.layer);
        glClear(GL_DEPTH_BUFFER_BIT);
        for (auto &shadow : shadowCasters)
            if (shadow->enabled &&
                (light->bakedShadow || !shadow->bakedOnly))
                shadow->drawShadow(proj, view, *depthShader,
                                   *depthShaderInstanced);
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
