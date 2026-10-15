
#pragma once

#include "../components/camera.hpp"
#include "../components/mesh.hpp"
#include "../components/particle.hpp"
#include "../loaders/model_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../render/material.hpp"
#include "../scene/node.hpp"
#include "../scene/scene.hpp"
#include <memory>

namespace scenes {
scn::Scene testingScene() {
    scn::Scene scene;

    const auto shader =
        std::make_shared<render::Shader>("grid_lit.vs", "grid_lit.fs");
    const auto particle_shader =
        std::make_shared<render::Shader>("particle.vs", "particle.fs");
    const auto material = std::make_shared<render::Material>(
        render::Material("Material", shader));
    // texture.fs/grid_lit.fs sample uTexture; without one bound it's black
    material->setTexture(render::TextureSlot::Albedo,
                         loader::loadTexture("grid_dark.png"));
    material->diffuse = {0.30f, 0.33f, 0.38f};
    material->setFloat("uBrighten", 4.f);
    const auto instanced_material = std::make_shared<render::Material>(
        render::Material("Particles", particle_shader));
    const auto plane_model =
        std::make_shared<render::Model>(loader::createPlane(25.f));

    auto camera_node = scn::Node::create("Camera");
    scene.activeCamera =
        camera_node->add_component<component::Camera>(50, 600, 600);
    camera_node->setLocalPos({5, 2, 0});
    scene.root->add_child(camera_node);
    scene.activeCamera.lock()->lookAt({0, 1.3, -0.5});

    auto floor_node = scn::Node::create("Floor");
    floor_node->add_component<component::Mesh>(plane_model, material);
    scene.root->add_child(floor_node);

    auto particle_node = scn::Node::create("ParticleSystem", {0, 0.1, 0});
    auto particles = particle_node->add_component<component::ParticleSystem>(
        instanced_material,
        std::make_shared<render::ComputeShader>("spawnParticle.comp"),
        std::make_shared<render::ComputeShader>("updateParticle.comp"));
    // Sparks: short-lived, burst upwards, fall back down, fade orange -> red
    particles->maxParticles = 10000;
    particles->spawnInterval = 0.0001f;
    particles->minLife = 0.5f;
    particles->maxLife = 4.0f;
    particles->minPosition = glm::vec3(-0.1f, 0, -0.1f);
    particles->maxPosition = glm::vec3(0.1f, 0, 0.1f);
    particles->minVelocity = glm::vec3(-1, 2, -1) * 2.f;
    particles->maxVelocity = glm::vec3(1, 4, 1) * 2.f;
    particles->minAccel = glm::vec3(0, -9.81f, 0);
    particles->maxAccel = glm::vec3(0, -9.81f, 0);
    particles->minColor = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f);
    particles->maxColor = glm::vec4(0.8f, 0.1f, 0.0f, 0.0f);
    floor_node->add_child(particle_node);

    return scene;
}
} // namespace scenes
