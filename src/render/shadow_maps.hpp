#pragma once

#include "GL/glew.h"
#include "glm/mat4x4.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace render {

// Depth maps of all shadow casting lights: one GL_TEXTURE_2D_ARRAY with a
// layer per light and a single framebuffer that is pointed at one layer per
// pass. Owned by the Scene, so the GL objects die with it.
class ShadowMaps {
  public:
    // Max layers: a point light takes 6, every other light 1
    static constexpr size_t MAX_SHADOW_LIGHTS = 12;
    static constexpr uint16_t RESOLUTION = 1 << 12; // 4096
    // Point light faces only use the lower left POINT_RESOLUTION square of
    // their layer (half the width and height = quarter of the pixels). Must
    // match POINT_SHADOW_SCALE in lit.fs (1 / POINT_RESOLUTION_DIV).
    static constexpr int POINT_RESOLUTION_DIV = 2;
    static constexpr uint16_t POINT_RESOLUTION =
        RESOLUTION / POINT_RESOLUTION_DIV;
    // Frames the light count has to stay lower before layers are freed, so
    // toggling a light on/off doesn't reallocate the array every time
    static constexpr int SHRINK_DELAY = 120;
    // Texture unit the lit shader reads the maps from (0-3 material, 4 sky)
    static constexpr int TEXTURE_UNIT = 5;

    ShadowMaps() = default;
    ~ShadowMaps() {
        release();
        // Not in release(): that also runs on every reallocation
        glDeleteBuffers(1, &ubo);
        glDeleteSamplers(1, &sampler);
    }

    // Owns GL objects, so neither copyable nor movable: the Scene holds it
    // through a unique_ptr, which is what makes the Scene itself movable
    ShadowMaps(const ShadowMaps &) = delete;
    ShadowMaps &operator=(const ShadowMaps &) = delete;

    // Call once per frame before rendering. Only touches GL when the number
    // of layers has to grow/shrink (an array can't be resized in place).
    void sync(size_t count) {
        count = std::min(count, MAX_SHADOW_LIGHTS);

        if (count > layers) {
            allocate(count);
            framesBelow = 0;
        } else if (count < layers) {
            if (++framesBelow >= SHRINK_DELAY) {
                allocate(count);
                framesBelow = 0;
            }
        } else {
            framesBelow = 0;
        }
    }

    // Renders into layer i: binds the framebuffer, attaches the layer as its
    // depth buffer and sets the viewport (and scissor, so a clear only touches
    // the used square). Undo with endLayers + Framebuffer::unbind.
    void bindLayer(size_t i, bool point = false) {
        const GLsizei res = point ? POINT_RESOLUTION : RESOLUTION;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTex,
                                  0, (GLint)i);
        glViewport(0, 0, res, res);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, res, res);
    }

    // Call after the last bindLayer
    void endLayers() { glDisable(GL_SCISSOR_TEST); }

    // Binds the array to TEXTURE_UNIT for the lit shader. Goes through a
    // sampler object with depth comparison on (hardware PCF), so the raw
    // texture stays readable for the debug preview.
    void bindForSampling() {
        if (sampler == 0) {
            glGenSamplers(1, &sampler);
            glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            // Outside the map counts as the far plane, i.e. not in shadow
            const float border[] = {1.f, 1.f, 1.f, 1.f};
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, border);
            glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE,
                                GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
        glActiveTexture(GL_TEXTURE0 + TEXTURE_UNIT);
        glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex);
        glBindSampler(TEXTURE_UNIT, sampler);
        glActiveTexture(GL_TEXTURE0);
    }

    // The array texture, 0 while there are no layers
    GLuint getDepthArray() const { return depthTex; }
    // Number of layers (= shadow casting lights that have a map)
    size_t size() const { return layers; }
    uint16_t resolution() const { return RESOLUTION; }

    // Uploads one light space matrix per layer and binds the block to slot 2.
    // Call every frame after the shadow pass, the sun's matrix moves.
    void uploadGPU(const std::vector<glm::mat4> &shadowMat) {
        if (ubo == 0) { // Generate UBO
            glGenBuffers(1, &ubo);
            glBindBuffer(GL_UNIFORM_BUFFER, ubo);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(ShadowBlock), nullptr,
                         GL_DYNAMIC_DRAW);
        }

        ShadowBlock block{};
        const size_t n = std::min(shadowMat.size(), MAX_SHADOW_LIGHTS);
        for (size_t i = 0; i < n; i++)
            block.shadow[i] = shadowMat[i];

        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ShadowBlock), &block);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, ubo);
    }

  private:
    GLuint fbo = 0, depthTex = 0, ubo = 0, sampler = 0;
    size_t layers = 0;
    int framesBelow = 0;

    void release() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &depthTex);
        glDeleteBuffers(1, &ubo);
        fbo = depthTex = ubo = 0;
        layers = 0;
    }

    // (Re)creates the array with the given number of layers
    void allocate(size_t count) {
        release();
        if (count == 0)
            return;

        GLint previous = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);

        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, depthTex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, RESOLUTION,
                     RESOLUTION, (GLsizei)count, 0, GL_DEPTH_COMPONENT,
                     GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Outside the map counts as the far plane, i.e. not in shadow
        const float border[] = {1.f, 1.f, 1.f, 1.f};
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_BORDER);
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        // Depth only: no color buffer to draw to or read from
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTex,
                                  0, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::puts("[Error] Shadow map framebuffer is not complete!");

        glBindFramebuffer(GL_FRAMEBUFFER, previous);
        layers = count;
    }

    struct ShadowBlock {
        glm::mat4 shadow[MAX_SHADOW_LIGHTS];
    };
};

} // namespace render
