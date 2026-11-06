#include "demo_scene.hpp"
#include "city_layout.hpp"

#include "../../components/UserMovement.hpp"
#include "../../components/animator.hpp"
#include "../../components/camera.hpp"
#include "../../components/instances.hpp"
#include "../../components/light.hpp"
#include "../../components/mesh.hpp"
#include "../../components/shadow.hpp"
#include "../../components/skybox.hpp"
#include "../../loaders/model_loader.hpp"
#include "../../render/material.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace scenes {

// Flat ground tiles and decals (and the leftover terrain and water) only
// receive shadows: casting, they shadow each other and themselves
bool isGround(const std::string &model) {
    for (const char *prefix : {"City_Ground", "Ground_", "Grass_1_Alpha",
                               "Grass_2_Alpha", "Rest_Environment"})
        if (model.rfind(prefix, 0) == 0)
            return true;
    return false;
}

// Leaf cards, seen from both sides
bool isFoliage(const std::string &model) {
    return model.rfind("Tree_", 0) == 0;
}

scn::Scene demoScene() {
    scn::Scene scene;

    auto lit = std::make_shared<render::Material>(
        "Lit", std::make_shared<render::Shader>("default.vs", "lit.fs"));
    auto litInstanced = std::make_shared<render::Material>(
        "Lit instanced", std::make_shared<render::Shader>(
                             "default_instanced.vs", "lit.fs", true));
    auto foliage = std::make_shared<render::Material>(
        "Lit instanced, double sided", litInstanced->getShader());
    foliage->doubleSided = true;

    // ---- The city: every copy of an object in one instanced draw ----
    auto cityNode = scn::Node::create("City");
    // cityNode->setLocalRot(glm::vec3(-glm::half_pi<float>(), 0, 0));
    std::map<std::string, std::vector<component::Instance>> copies;
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const city::Placement &p : city::placements) {
        component::Instance instance;
        instance.position = {p.x, p.y, p.z};
        instance.rotation = {p.rx, p.ry, p.rz};
        instance.scale = glm::vec3(p.scale);
        if (p.mirrored)
            instance.scale.y = -p.scale; // flat, so only the normal flips
        copies[p.model].push_back(instance);
        lo = glm::min(lo, instance.position);
        hi = glm::max(hi, instance.position);
    }
    for (auto &[model, instances] : copies) {
        auto node = scn::Node::create(model);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel("MC_" + model + ".glb")),
            isFoliage(model) ? foliage : litInstanced);
        node->add_component<component::Instances>(std::move(instances));
        if (!isGround(model))
            node->add_component<component::Shadow>();
        cityNode->add_child(node);
    }
    // What the objects don't cover (the ground), where the demo has it
    for (const char *model : city::rest) {
        auto node = scn::Node::create(model);
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(
                loader::loadModel(std::string("MC_") + model + ".glb")),
            lit);
        if (!isGround(model))
            node->add_component<component::Shadow>();
        cityNode->add_child(node);
    }
    scene.root->add_child(cityNode);
    const glm::vec3 center = (lo + hi) * 0.5f;
    const float radius = glm::length(hi - lo) * 0.5f;

    auto sky = scn::Node::create("Skybox");
    auto skybox = sky->add_component<component::Skybox>("skybox.png");
    scene.skybox = skybox;
    scene.root->add_child(sky);

    // ---- Day and night: the sun turns once around the city per DAY ----
    // Pitch 0 is sunrise in the east, -90 noon, -180 sunset, below that the
    // sun shines up from under the ground, so it is dark then
    using component::AnimTarget;
    using V = glm::vec3;
    constexpr float DAY = 120.f; // seconds
    const float yaw = glm::radians(35.f);
    // (fraction of the day, value) -> keys in seconds
    auto day = [&](std::vector<std::pair<float, V>> points) {
        for (auto &point : points)
            point.first *= DAY;
        return points;
    };
    {
        auto node = scn::Node::create("Sun", center);
        auto sun =
            component::Light::directional({1.f, 0.9f, 0.75f}, 1.5f, 0.3f);
        // Moves all the time, so its map is drawn every frame
        sun.castsShadow = true;
        sun.shadowExtent = radius / 2.f + 20.f;
        sun.shadowNear = 0.f;
        sun.shadowFar = 2.f * radius + 100.f;
        node->add_component<component::Light>(sun);
        scene.root->add_child(node);

        auto animator = node->add_component<component::Animator>();
        // Evenly spaced points on a line: Catmull-Rom keeps the speed even,
        // and -360 deg is the same as 0, so the loop doesn't jump
        std::vector<V> pitch;
        for (float deg : {0.f, -90.f, -180.f, -270.f, -360.f})
            pitch.push_back({glm::radians(deg), yaw, 0.f});
        animator->addTrack(AnimTarget::Rotation,
                           component::Curve<V>::smoothEven(pitch, DAY));
        // Orange low over the horizon, white at noon, gone at night. Flat
        // keys so it never overshoots below black.
        const V low{1.f, 0.45f, 0.2f}, mid{1.f, 0.8f, 0.6f},
            noon{1.f, 0.95f, 0.85f}, none{0.f};
        animator->addTrack(AnimTarget::Color,
                           component::Curve<V>::flat(day({{0.f, low},
                                                          {0.08f, mid},
                                                          {0.25f, noon},
                                                          {0.42f, mid},
                                                          {0.5f, low},
                                                          {0.56f, none},
                                                          {0.94f, none},
                                                          {1.f, low}})));
        // The sky follows
        const V dusk{1.f, 0.6f, 0.45f}, night{0.06f, 0.08f, 0.15f};
        animator->addTrack(skybox->tint,
                           component::Curve<V>::flat(day({{0.f, dusk},
                                                          {0.1f, V(1.f)},
                                                          {0.4f, V(1.f)},
                                                          {0.5f, dusk},
                                                          {0.58f, night},
                                                          {0.92f, night},
                                                          {1.f, dusk}})));
        animator->play();

        // Moon: opposite the sun, so it is up while the sun is down. Dim and
        // bluish, no shadow; its ambient keeps the night from going black.
        auto moonNode = scn::Node::create("Moon");
        moonNode->setLocalRotEuler({glm::pi<float>(), 0.f, 0.f});
        node->add_child(moonNode);
        moonNode->add_component<component::Light>(
            component::Light::directional(V(0.f), 1.f, 0.6f));
        const V moon{0.25f, 0.3f, 0.45f};
        auto moonAnimator = moonNode->add_component<component::Animator>();
        moonAnimator->addTrack(AnimTarget::Color,
                               component::Curve<V>::flat(day({{0.f, none},
                                                              {0.5f, none},
                                                              {0.58f, moon},
                                                              {0.92f, moon},
                                                              {1.f, none}})));
        moonAnimator->play();
    }

    // ---- Camera, above the middle of the city ----
    {
        auto node = scn::Node::create(
            "Camera", center + glm::vec3(0.f, radius * 0.25f, radius * 0.6f));
        node->setLocalRotEuler({glm::radians(-32.f), 0.f, 0.f});
        // Near plane not too close: the ground decals sit only 2-7 cm above
        // each other, a 24 bit depth buffer needs the precision far away
        scene.activeCamera =
            node->add_component<component::Camera>(50.f, 1.f, 1.f, 0.5f, 800.f);
        node->add_component<component::UserMovement>(20.f, 0.1f);
        scene.root->add_child(node);
    }

    return scene;
}

} // namespace scenes
