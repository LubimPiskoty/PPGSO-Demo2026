#pragma once

#include "../components/light.hpp"
#include "../components/mesh.hpp"
#include "../components/shadow.hpp"
#include "../loaders/model_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/material.hpp"
#include "../scene/scene.hpp"
#include "light_test.hpp"
#include <memory>
#include <string>

namespace scenes {

// A floor with a few shapes on it, lit by a shadow casting sun and a spot
// light. Check the depth preview in the debug UI.
scn::Scene shadowTest() {
    scn::Scene scene;

    auto litShader = std::make_shared<render::Shader>("default.vs", "lit.fs");

    // Procedural meshes have no texture, the lit shader needs a white albedo
    unsigned char white[4] = {255, 255, 255, 255};
    auto white_texture = std::make_shared<render::Texture>(
        render::Texture{loader::createTexture(white, 1, 1), "<white>"});
    auto white_material =
        std::make_shared<render::Material>("White material", litShader);
    white_material->setTexture(render::TextureSlot::Albedo, white_texture);

    auto floor_material =
        std::make_shared<render::Material>("Grid material", litShader);
    floor_material->setTexture(render::TextureSlot::Albedo,
                               loader::loadTexture("grid_dark.png"));

    // Floor: receives shadows only, so no Shadow component
    {
        auto node = scn::Node::create("Floor");
        node->add_component<component::Mesh>(
            std::make_shared<render::Model>(loader::createPlane(30.f)),
            floor_material);
        scene.root->add_child(node);
    }

    // Casters
    auto box =
        std::make_shared<render::Model>(loader::createBox(glm::vec3(0.5f)));
    auto sphere =
        std::make_shared<render::Model>(loader::createSphere(1.f, 1.f, 32, 16));
    auto pyramid =
        std::make_shared<render::Model>(loader::createPyramid(0.6f, 1.2f));

    auto addCaster = [&](const std::string &name,
                         std::shared_ptr<render::Model> model, glm::vec3 pos,
                         float scale) {
        auto node = scn::Node::create(name, pos);
        node->setLocalScale(glm::vec3(scale));
        node->add_component<component::Mesh>(model, white_material);
        node->add_component<component::Shadow>();
        scene.root->add_child(node);
    };
    addCaster("Box", box, {-2.f, 0.5f, 0.f}, 1.f);
    addCaster("Tall box", box, {2.f, 1.f, -1.f}, 2.f);
    addCaster("Sphere", sphere, {0.f, 0.7f, 2.f}, 0.7f);
    addCaster("Pyramid", pyramid, {-1.f, 0.f, -3.f}, 1.f);
    addCaster("Floating box", box, {0.5f, 2.5f, 0.f}, 0.6f);

    // Camera
    {
        auto node = scn::Node::create("Camera", glm::vec3{0.f, 5.f, 9.f});
        auto camera = node->add_component<component::Camera>(45.f, 1.f, 1.f);
        scene.root->add_child(node);
        scene.activeCamera = camera;
        camera->lookAt({0.f, 0.5f, 0.f});
    }

    // Sun (shadow casting, orthographic), slowly sweeping around Y
    {
        auto pivot = scn::Node::create("Sun pivot");
        pivot->add_component<Spin>(8.f);
        scene.root->add_child(pivot);

        auto node = scn::Node::create("Sun", glm::vec3(6.f, 10.f, 4.f));
        // pointing at the origin: yaw -34 deg, pitch -52 deg (-Z forward)
        node->setLocalRot(glm::quat(glm::radians(glm::vec3(-52.f, 34.f, 0.f))));
        auto sun = node->add_component<component::Light>(
            component::Light::directional(glm::vec3(1.f, 0.95f, 0.85f), 1.f));
        sun->castsShadow = true;
        pivot->add_child(node);
    }

    // Spot light from the side (shadow casting, perspective)
    {

        auto pivot = scn::Node::create("Spot pivot");
        pivot->add_component<Spin>(-16.f);
        scene.root->add_child(pivot);
        auto node = scn::Node::create("Spot", glm::vec3(-6.f, 5.f, 4.f));
        node->setLocalRot(
            glm::quat(glm::radians(glm::vec3(-30.f, -55.f, 0.f))));
        auto spot =
            node->add_component<component::Light>(component::Light::spot(
                glm::vec3(0.4f, 0.6f, 1.f), 2.f, 20.f, 30.f));
        spot->castsShadow = true;
        pivot->add_child(node);
    }

    // Point light (shadow casting, 6 cube faces)
    {
        auto node = scn::Node::create("Point light", glm::vec3(3.f, 1.5f, 3.f));
        auto point = node->add_component<component::Light>(
            component::Light::point(glm::vec3(1.f, 0.4f, 0.2f), 2.f));
        point->castsShadow = true;
        point->shadowFar = 30.f;
        scene.root->add_child(node);
    }

    {
        auto node = scn::Node::create("Skybox");
        scene.skybox = node->add_component<component::Skybox>("skybox.png");
        scene.root->add_child(node);
    }

    return scene;
}

} // namespace scenes
