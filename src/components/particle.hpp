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
    // Off: nothing new spawns, the live particles still fly and fade. Bursts
    // (sparks off a hammer blow) switch it on for a moment.
    bool spawning = true;
    // Additive (sparks, embers: they glow) or plain alpha blending (smoke:
    // it covers what is behind it)
    bool additive = true;
    // Multiplies dt, < 1 is slow motion for this system only
    float timeScale = 1.f;
    // Motion smear: seconds of movement stretched behind each particle, 0 = off
    float stretch = 0.05f;
    uint32_t maxParticles = DEFAULT_MAX_PARTICLES;
    uint32_t drawnParticleCount = 0;

    ParticleSystem(std::shared_ptr<render::Material> instanced_material,
                   std::shared_ptr<render::ComputeShader> spawnCompute,
                   std::shared_ptr<render::ComputeShader> updateCompute,
                   EmitterType type = EmitterType::SPRITE);
    // Frees the quad's buffers (the particle buffers free themselves)
    ~ParticleSystem() override;
    ParticleSystem(const ParticleSystem &) = delete;
    ParticleSystem &operator=(const ParticleSystem &) = delete;

    render::Buffer particles;
    render::Buffer freelist;
    render::Buffer indirectDrawBuffer;
    render::Buffer drawlist;
    render::Buffer emitterSettings{GL_UNIFORM_BUFFER};

    void update(float dt) override;

    void draw(glm::mat4 projection, glm::mat4 invCameraTransform) override;

    const char *type_name() const override { return "ParticleSystem"; }

    // Mixed into the spawn shader's random numbers: emitters with the same
    // settings but different seeds don't spawn the same particles in step
    uint32_t seed = 0;

  private:
    GLuint vao = 0, vbo = 0;
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
