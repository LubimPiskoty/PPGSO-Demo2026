
#pragma once

#include "../components/light.hpp"
#include "../components/mesh.hpp"
#include "../components/shadow.hpp"
#include "../components/spin.hpp"
#include "../loaders/model_loader.hpp"
#include "../loaders/shader_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/material.hpp"
#include "../scene/scene.hpp"
#include <cmath>
#include <memory>
#include <string>

namespace scenes {

using component::Spin;

scn::Scene lightTest() {
    scn::Scene scene;

    // Load primitives
    // Inward normals so the point light at its center lights it up
    auto sphere_model = std::make_shared<render::Model>(
        loader::createSphere(1.f, 1.f, 32, 16, true));

    // Load test model
    auto litShader = std::make_shared<render::Shader>("default.vs", "lit.fs");
    auto lit_material =
        std::make_shared<render::Material>("Lit material", litShader);
    auto testing_mesh = std::make_shared<render::Model>(
        loader::loadModel("chevrolet_impala_1967_low.glb"));
    lit_material->setFloat("uReflectivity", 0.2f);   // reflect sky
    lit_material->setFloat("uRoughness", 0.2f);

    // Lit shader multiplies light by the albedo, so the bulbs need a white one
    // (procedural meshes have no texture -> black)
    unsigned char white[4] = {255, 255, 255, 255};
    auto white_texture = std::make_shared<render::Texture>(
        render::Texture{loader::createTexture(white, 1, 1), "<white>"});
    auto bulb_material =
        std::make_shared<render::Material>("Bulb material", litShader);
    bulb_material->setTexture(render::TextureSlot::Albedo, white_texture);

    // Load floor
    auto grid_texture = loader::loadTexture("grid_dark.png");
    auto floor_material =
        std::make_shared<render::Material>("Grid material", litShader);

    auto floor_mesh =
        std::make_shared<render::Model>(loader::createPlane(50.f));
    floor_material->setTexture(render::TextureSlot::Albedo, grid_texture);

    // Floor
    {
        auto floor_node = scn::Node::create("Floor");
        floor_node->add_component<component::Mesh>(floor_mesh, floor_material);
        scene.root->add_child(floor_node);
    }

    { // Test mesh
        auto test_node = scn::Node::create("Test mesh");
        test_node->add_component<component::Mesh>(testing_mesh, lit_material);
        test_node->add_component<component::Shadow>(); // casts into the maps
        test_node->setLocalScale(glm::vec3(10.f));
        test_node->setLocalPos(
            glm::vec3(0.f, 0.4f, 0.f)); // wheels on the floor
        test_node->setLocalRotEuler(glm::radians(
            glm::vec3(0.f, -30.f, 0.f))); // front turned to the left
        scene.root->add_child(test_node);
    }

    // camera
    {
        auto pivot = scn::Node::create("Camera pivot");
        pivot->add_component<Spin>(5.f);
        scene.root->add_child(pivot);

        auto camNode = scn::Node::create("Camera", glm::vec3{0.f, 2.f, 7.f});
        auto camera = camNode->add_component<component::Camera>(35.f, 1.f, 1.f);
        pivot->add_child(camNode);
        scene.activeCamera = camera;
        camera->lookAt({0.f, 0.8f, 0.f}); // car center, orbit pivot below it

        // Shading test sphere, child of the camera so it sits in the bottom
        // right corner of the screen (camera looks down its -Z)
        auto test_sphere = std::make_shared<render::Model>(
            loader::createSphere(1.f, 1.f, 32, 16));
        auto sphereNode = scn::Node::create("Shading test sphere",
                                            glm::vec3{1.2f, -0.6f, -3.f});
        sphereNode->setLocalScale(glm::vec3(0.25f));
        sphereNode->add_component<component::Mesh>(test_sphere, bulb_material);
        camNode->add_child(sphereNode);
    }
    // sun: tilted child of a spinning pivot, so its direction sweeps around Y
    {
        auto pivot = scn::Node::create("Sun pivot");
        scene.root->add_child(pivot);

        auto node = scn::Node::create("Sun", glm::vec3(0, 1.8, 0));
        node->setLocalRot(glm::quat(glm::radians(glm::vec3(-40.f, 30.f, 0.f))));

        node->add_component<component::Light>(
            component::Light::directional(glm::vec3(1), 1.2f)); // slightly warm
        pivot->add_child(node);
    }

    // point lights orbiting the car: children of a spinning pivot, spread
    // evenly around it
    {
        auto pivot = scn::Node::create("Light pivot");
        pivot->add_component<Spin>(10.f);
        scene.root->add_child(pivot);

        const glm::vec3 colors[] = {
            {1.f, 0.2f, 0.2f}, // red
            {0.2f, 1.f, 0.3f}, // green
            {0.2f, 0.4f, 1.f}, // blue
        };
        const float radius = 3.f, height = 1.5f;
        for (int i = 0; i < 3; i++) {
            float angle = glm::radians(120.f * i + 60.f);
            glm::vec3 pos{radius * glm::cos(angle), height,
                          radius * glm::sin(angle)};

            auto node =
                scn::Node::create("Point light " + std::to_string(i), pos);
            node->add_component<component::Light>(
                component::Light::point(colors[i], 3.f, 0.35f, 0.44f));
            node->add_component<component::Mesh>(sphere_model, bulb_material);
            node->setLocalScale(glm::vec3(0.1f));
            pivot->add_child(node);
        }
    }

    // shadow casting point light: static, off to the side of the car so the
    // shadow falls across the floor
    {
        auto node =
            scn::Node::create("Shadow point light", glm::vec3(3.f, 3.f, -2.f));
        auto light = node->add_component<component::Light>(
            component::Light::point(glm::vec3(1.f, 0.9f, 0.7f), 4.f, 0.09f,
                                    0.032f));
        light->castsShadow = true;
        light->shadowFar = 30.f;
        node->add_component<component::Mesh>(sphere_model, bulb_material);
        node->setLocalScale(glm::vec3(0.15f));
        scene.root->add_child(node);
    }

    // spotlight: child of a pivot, shines down at an angle
    {
        auto pivot = scn::Node::create("Spotlight pivot");
        scene.root->add_child(pivot);

        auto node = scn::Node::create("Spot lamp", glm::vec3(2.f, 4.f, 2.f));

        node->setLocalRot(glm::quat(glm::radians(glm::vec3(-70.f, 0.f, 0.f))));

        node->add_component<component::Light>(
    component::Light::spot(glm::vec3(1.f, 0.95f, 0.85f), // farba
                           1.5f,                         // intenzita
                           20.f,                         // cutoff v stupňoch
                           0.09f,                        // linear
                           0.032f));                     // quadratic
        pivot->add_child(node);
    }

    // skybox
    {
        auto node = scn::Node::create("Skybox");
        scene.skybox = node->add_component<component::Skybox>("skybox.png");
        scene.root->add_child(node);
    }

    return scene;
}

} // namespace scenes
