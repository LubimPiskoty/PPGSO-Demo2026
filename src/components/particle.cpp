#include "particle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

namespace component {

namespace {
// Mirrors `struct Particle` in the compute shaders (std430)
constexpr size_t PARTICLE_SIZE = 96;
constexpr uint32_t PARTICLES_PER_THREAD = 128;

// Mirrors `uniform EmitterSettings` (std140: vec3s padded to 16 bytes)
struct EmitterSettingsGPU {
    glm::vec4 minColor, maxColor;
    glm::vec3 minOffset; float pad0;
    glm::vec3 maxOffset; float pad1;
    glm::vec3 minVelocity; float pad2;
    glm::vec3 maxVelocity; float pad3;
    glm::vec3 minAccel; float pad4;
    glm::vec3 maxAccel; float pad5;
    glm::mat4 modelMatrix;
    glm::mat4 rotationMatrix;
    float minLife, maxLife;
    float pad6[2];
};
static_assert(offsetof(EmitterSettingsGPU, modelMatrix) == 128);
static_assert(offsetof(EmitterSettingsGPU, minLife) == 256);

// Same layout as GL's DrawArraysIndirectCommand
struct DrawArraysIndirectCommand {
    GLuint count, instanceCount, first, baseInstance;
};

GLuint workGroupsFor(uint32_t items, int workGroupSize) {
    const uint32_t perGroup = PARTICLES_PER_THREAD * workGroupSize;
    return (items + perGroup - 1) / perGroup;
}
} // namespace

ParticleSystem::ParticleSystem(
    std::shared_ptr<render::Material> instanced_material,
    std::shared_ptr<render::ComputeShader> spawnCompute,
    std::shared_ptr<render::ComputeShader> updateCompute, EmitterType type)
    : material(instanced_material), spawnCompute(spawnCompute),
      updateCompute(updateCompute), emitterType(type) {

    constexpr float quadVertices[] = {-0.5f, -0.5f, 0.5f, -0.5f,
                                      -0.5f, 0.5f,  0.5f, 0.5f};

    // Subject to later refactoring..
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices,
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
};

void ParticleSystem::allocateBuffers() {
    // life = 0 everywhere -> all dead
    std::vector<char> zeros(maxParticles * PARTICLE_SIZE, 0);
    particles.setData(zeros.size(), zeros.data());

    // { int count; int indices[]; } with every particle free
    std::vector<int> free(maxParticles + 1);
    free[0] = (int)maxParticles;
    std::iota(free.begin() + 1, free.end(), 0);
    freelist.setData(free.size() * sizeof(int), free.data());

    DrawArraysIndirectCommand cmd{4, 0, 0, 0};
    indirectDrawBuffer.setData(sizeof(cmd), &cmd);

    drawlist.setData(maxParticles * sizeof(GLuint), nullptr);
    emitterSettings.setData(sizeof(EmitterSettingsGPU), nullptr);

    allocatedParticles = maxParticles;
}

void ParticleSystem::uploadEmitterSettings() {
    const glm::mat4 model = node.lock()->globalTransform();
    glm::mat4 rotation = model;
    rotation[3] = glm::vec4(0, 0, 0, 1);

    EmitterSettingsGPU s{};
    s.minColor = minColor;
    s.maxColor = maxColor;
    s.minOffset = minPosition;
    s.maxOffset = maxPosition;
    s.minVelocity = minVelocity;
    s.maxVelocity = maxVelocity;
    s.minAccel = minAccel;
    s.maxAccel = maxAccel;
    s.modelMatrix = model;
    s.rotationMatrix = rotation;
    s.minLife = minLife;
    s.maxLife = maxLife;

    glBindBuffer(GL_UNIFORM_BUFFER, emitterSettings.bufferID);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(s), &s);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void ParticleSystem::update(float dt) {
    if (allocatedParticles != maxParticles)
        allocateBuffers();

    timer += dt;
    spawnInterval =
        std::max(spawnInterval, std::numeric_limits<float>::epsilon());
    uint32_t particlesToSpawn = static_cast<uint32_t>(timer / spawnInterval);
    timer = std::fmod(timer, spawnInterval);
    particlesToSpawn = std::min(particlesToSpawn, maxParticles);

    particles.bindSSBO(0);
    freelist.bindSSBO(1);
    indirectDrawBuffer.bindSSBO(2);
    drawlist.bindSSBO(3);

    // Spawn particles
    if (particlesToSpawn > 0) {
        uploadEmitterSettings();
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, emitterSettings.bufferID);

        spawnCompute->bind();
        const GLuint prog = spawnCompute->program;
        glUniform1ui(glGetUniformLocation(prog, "u_particlesToSpawn"),
                     particlesToSpawn);
        glUniform1ui(glGetUniformLocation(prog, "u_seed"), frame++);
        spawnCompute->run(workGroupsFor(
            particlesToSpawn, spawnCompute->QueryLinearWorkGroupSize()));
    }

    // Update: rebuilds the draw list, so start from zero instances
    const GLuint zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, indirectDrawBuffer.bufferID);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    offsetof(DrawArraysIndirectCommand, instanceCount),
                    sizeof(zero), &zero);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    updateCompute->bind();
    const GLuint prog = updateCompute->program;
    glUniform1f(glGetUniformLocation(prog, "u_dt"), dt);
    // No view culling yet: the update pass doesn't know the camera
    glUniform3f(glGetUniformLocation(prog, "u_viewPos"), 0, 0, 0);
    glUniform3f(glGetUniformLocation(prog, "u_forwardDir"), 0, 0, -1);
    glUniform1f(glGetUniformLocation(prog, "u_cullingMinAngle"), -2.f);
    updateCompute->run(workGroupsFor(
        maxParticles, updateCompute->QueryLinearWorkGroupSize()));
}

void ParticleSystem::draw(glm::mat4 projection, glm::mat4 invCameraTransform) {
    if (!enabled || allocatedParticles == 0)
        return;

    material->setMat4("uProjection", projection);
    material->setMat4("uView", invCameraTransform);
    material->bind();
    particles.bindSSBO(0);
    drawlist.bindSSBO(3);

    // Additive sparks, no depth writes so they don't occlude each other
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    glBindVertexArray(vao);
    indirectDrawBuffer.bind(GL_DRAW_INDIRECT_BUFFER);
    glDrawArraysIndirect(GL_TRIANGLE_STRIP, nullptr);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
};

} // namespace component
