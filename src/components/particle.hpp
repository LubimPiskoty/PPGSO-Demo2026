#pragma once

#include "../render/compute_shader.hpp"
#include "../render/material.hpp"
#include "../scene/node.hpp"
#include "ecs.hpp"
#include <GL/glew.h>
#include <cstdint>
#include <memory>

namespace component {
// TODO: Draw call types
// https://evanvoodoo.github.io/2025-01-24-gpu-particles/

const uint32_t DEFAULT_MAX_PARTICLES = 1 << 12;

class ParticleSystem : public ComponentBase<ParticleSystem> {
  public:
    enum EmitterType { POINT, SPRITE };

    EmitterType emitterType = EmitterType::SPRITE;
    glm::vec4 minColor = glm::vec4(0, 0, 0, 1), maxColor = glm::vec4(1);
    glm::vec3 minPosition = glm::vec3(0), maxPosition = glm::vec3(0);
    glm::vec3 minVelocity = glm::vec3(-1), maxVelocity = glm::vec3(1);
    glm::vec3 minAccel = glm::vec3(0), maxAccel = glm::vec3(0, 0, -0.5f);
    float minLife = 1, maxLife = 10;
    // Sprite size in world units
    float size = 0.1f;
    float spawnInterval = 0.00001f, timer = 0;
    // Motion smear: seconds of movement stretched behind each particle, 0 = off
    float stretch = 0.05f;
    uint32_t maxParticles = DEFAULT_MAX_PARTICLES;
    uint32_t drawnParticleCount = 0;

    ParticleSystem(std::shared_ptr<render::Material> instanced_material,
                   std::shared_ptr<render::ComputeShader> spawnCompute,
                   std::shared_ptr<render::ComputeShader> updateCompute,
                   EmitterType type = EmitterType::SPRITE);

    render::Buffer particles;
    render::Buffer freelist;
    render::Buffer indirectDrawBuffer;
    render::Buffer drawlist;
    render::Buffer emitterSettings{GL_UNIFORM_BUFFER};

    void update(float dt) override;

    void draw(glm::mat4 projection, glm::mat4 invCameraTransform) override;

    const char *type_name() const override { return "ParticleSystem"; }

  private:
    GLuint vao, vbo;
    uint32_t allocatedParticles = 0;
    uint32_t frame = 0;

    // (Re)creates all GPU buffers for maxParticles, every particle dead
    void allocateBuffers();
    void uploadEmitterSettings();
    std::shared_ptr<render::ComputeShader> spawnCompute;
    std::shared_ptr<render::ComputeShader> updateCompute;
    std::shared_ptr<render::Material> material;
};
} // namespace component
