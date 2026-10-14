
#pragma once

#include "../components/light.hpp"
#include "../components/mesh.hpp"
#include "../loaders/model_loader.hpp"
#include "../loaders/shader_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/material.hpp"
#include "../scene/scene.hpp"
#include <memory>
#include <string>

namespace scenes {

// Spins its node around the Y axis; children (the camera) orbit with it
class Spin : public component::ComponentBase<Spin> {
  public:
    explicit Spin(float degPerSec) : speed(glm::radians(degPerSec)) {}
    const char *type_name() const override { return "Spin"; }
    void update(float dt) override {
        if (auto owner = node.lock()) {
            angle += speed * dt;
            owner->setLocalRot(glm::angleAxis(angle, glm::vec3(0.f, 1.f, 0.f)));
        }
    }

  private:
    float speed, angle = 0.f;
};

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
        test_node->setLocalScale(glm::vec3(10.f));
        test_node->setLocalPos(
            glm::vec3(0.f, 0.4f, 0.f)); // wheels on the floor
        test_node->setLocalRotEuler(
            glm::radians(glm::vec3(0.f, -30.f, 0.f))); // front turned to the left
        scene.root->add_child(test_node);
    }

    // camera
    {
        auto pivot = scn::Node::create("Camera pivot");
        scene.root->add_child(pivot);

        auto camNode = scn::Node::create("Camera", glm::vec3{0.f, 2.f, 7.f});
        auto camera = camNode->add_component<component::Camera>(35.f, 1.f, 1.f);
        pivot->add_child(camNode);
        scene.activeCamera = camera;
        camera->lookAt({0.f, 0.8f, 0.f}); // car center, orbit pivot below it
    }
    // sun
    {
        auto node = scn::Node::create("Sun");
        node->setLocalRot(glm::quat(glm::radians(glm::vec3(-60.f, 30.f, 0.f))));
        node->add_component<component::Light>(component::Light::directional(
            {1.f, 0.95f, 0.85f}, 0.9f)); // slightly warm
        scene.root->add_child(node);
    }

    // point lights orbiting the car: children of a spinning pivot, spread
    // evenly around it
    {
        auto pivot = scn::Node::create("Light pivot");
        pivot->add_component<Spin>(30.f);
        scene.root->add_child(pivot);

        const glm::vec3 colors[] = {
            {1.f, 0.2f, 0.2f}, // red
            {0.2f, 1.f, 0.3f}, // green
            {0.2f, 0.4f, 1.f}, // blue
        };
        const float radius = 3.f, height = 1.5f;
        for (int i = 0; i < 3; i++) {
            float angle = glm::radians(120.f * i);
            glm::vec3 pos{radius * glm::cos(angle), height,
                          radius * glm::sin(angle)};

            auto node = scn::Node::create("Point light " + std::to_string(i),
                                          pos);
            node->add_component<component::Light>(
                component::Light::point(colors[i], 3.f, 0.35f, 0.44f));
            node->add_component<component::Mesh>(sphere_model, bulb_material);
            node->setLocalScale(glm::vec3(0.1f));
            pivot->add_child(node);
        }
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
