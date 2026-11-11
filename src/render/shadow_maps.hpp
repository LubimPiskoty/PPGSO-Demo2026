#pragma once

#include "GL/glew.h"
#include "glm/mat4x4.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace render {

// Depth maps of all shadow casting lights, in two GL_TEXTURE_2D_ARRAYs: the
// suns' big maps, and a small one for the spot lights and the point lights'
// cube faces (6 layers each), so many of those cost little memory. Each has
// a layer per map and a framebuffer pointed at one layer per pass. Owned by
// the Scene, so the GL objects die with it.
class ShadowMaps {
  public:
    enum Bank { Sun = 0, Small = 1 };

    // Max layers in each array. Must match MAX_SUN_SHADOWS and
    // MAX_SMALL_SHADOWS in lit.fs.
    static constexpr size_t MAX_SUN_LAYERS = 4;
    static constexpr size_t MAX_SMALL_LAYERS = 120;
    static constexpr size_t MAX_LAYERS = MAX_SUN_LAYERS + MAX_SMALL_LAYERS;
    // Map sizes in texels, unless the environment sets them:
    // SHADOW_RES=<suns' maps> SHADOW_SMALL_RES=<spot / point maps>
#ifdef NDEBUG
    static constexpr int DEFAULT_SUN_RESOLUTION = 1 << 13;   // 8k
    static constexpr int DEFAULT_SMALL_RESOLUTION = 1 << 11; // 16 MB a layer
#else
    // Debug is slow enough
    static constexpr int DEFAULT_SUN_RESOLUTION = 1 << 11;
    static constexpr int DEFAULT_SMALL_RESOLUTION = 1 << 10;
#endif
    static int sunResolution() {
        static const int res =
            fromEnv("SHADOW_RES", DEFAULT_SUN_RESOLUTION);
        return res;
    }
    static int smallResolution() {
        static const int res =
            fromEnv("SHADOW_SMALL_RES", DEFAULT_SMALL_RESOLUTION);
        return res;
    }
    // Frames the light count has to stay lower before layers are freed, so
    // toggling a light on/off doesn't reallocate the array every time
    static constexpr int SHRINK_DELAY = 120;
    // Texture units the lit shader reads the maps from (0-3 material, 4 sky,
    // 6 emissive)
    static constexpr int SUN_TEXTURE_UNIT = 5;
    static constexpr int SMALL_TEXTURE_UNIT = 7;

    ShadowMaps() = default;
    ~ShadowMaps() {
        for (auto &array : arrays)
            array.release();
        // Not in release(): that also runs on every reallocation
        glDeleteBuffers(1, &ubo);
        glDeleteSamplers(1, &sampler);
    }

    // Owns GL objects, so neither copyable nor movable: the Scene holds it
    // through a unique_ptr, which is what makes the Scene itself movable
    ShadowMaps(const ShadowMaps &) = delete;
    ShadowMaps &operator=(const ShadowMaps &) = delete;

    static Bank bankOf(bool directional) { return directional ? Sun : Small; }
    static size_t maxLayers(Bank bank) {
        return bank == Sun ? MAX_SUN_LAYERS : MAX_SMALL_LAYERS;
    }

    // Call once per frame before rendering, with the layers each array
    // needs. Only touches GL when an array has to grow/shrink (it can't be
    // resized in place). Returns true for an array it did, which wipes every
    // layer of it.
    bool sync(Bank bank, size_t count) {
        return arrays[bank].sync(std::min(count, maxLayers(bank)),
                                 bank == Sun ? sunResolution()
                                             : smallResolution());
    }

    // Renders into a layer of an array: binds the framebuffer, attaches the
    // layer as its depth buffer and sets the viewport. Undo with
    // Framebuffer::unbind.
    void bindLayer(Bank bank, size_t layer) {
        const Array &array = arrays[bank];
        glBindFramebuffer(GL_FRAMEBUFFER, array.fbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  array.tex, 0, (GLint)layer);
        glViewport(0, 0, array.resolution, array.resolution);
    }

    // Binds both arrays for the lit shader. Through a sampler object with
    // depth comparison on (hardware PCF), so the raw textures stay readable
    // for the debug preview.
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
        for (Bank bank : {Sun, Small}) {
            const int unit = bank == Sun ? SUN_TEXTURE_UNIT : SMALL_TEXTURE_UNIT;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, arrays[bank].tex);
            glBindSampler(unit, sampler);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    // The array texture, 0 while it has no layers
    GLuint getDepthArray(Bank bank) const { return arrays[bank].tex; }
    int resolution(Bank bank) const {
        return bank == Sun ? sunResolution() : smallResolution();
    }

    // Uploads the light space matrix of each layer of both arrays and binds
    // the block to slot 2. Call every frame after the shadow pass.
    void uploadGPU(const std::vector<glm::mat4> &sun,
                   const std::vector<glm::mat4> &small) {
        if (ubo == 0) { // Generate UBO
            glGenBuffers(1, &ubo);
            glBindBuffer(GL_UNIFORM_BUFFER, ubo);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(ShadowBlock), nullptr,
                         GL_DYNAMIC_DRAW);
        }

        ShadowBlock block{};
        for (size_t i = 0; i < std::min(sun.size(), MAX_SUN_LAYERS); i++)
            block.sun[i] = sun[i];
        for (size_t i = 0; i < std::min(small.size(), MAX_SMALL_LAYERS); i++)
            block.small[i] = small[i];

        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ShadowBlock), &block);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, ubo);
    }

  private:
    // A size from an environment variable: a power of two from 64 up to the
    // GPU's largest texture, else the default
    static int fromEnv(const char *name, int fallback) {
        const char *value = std::getenv(name);
        if (!value)
            return fallback;
        const int res = std::atoi(value);
        GLint max = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
        if (res < 64 || (res & (res - 1)) != 0 || res > max) {
            std::fprintf(stderr,
                         "%s=%s: not a power of two from 64 to %d, using %d\n",
                         name, value, max, fallback);
            return fallback;
        }
        std::printf("%s: %d\n", name, res);
        std::fflush(stdout);
        return res;
    }

    // One depth array and the framebuffer that renders into its layers
    struct Array {
        GLuint fbo = 0, tex = 0;
        size_t layers = 0;
        int resolution = 0;
        int framesBelow = 0;

        bool sync(size_t count, int res) {
            if (count > layers) {
                allocate(count, res);
                framesBelow = 0;
                return true;
            }
            if (count < layers) {
                if (++framesBelow >= SHRINK_DELAY) {
                    allocate(count, res);
                    framesBelow = 0;
                    return true;
                }
                return false;
            }
            framesBelow = 0;
            return false;
        }

        void release() {
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            fbo = tex = 0;
            layers = 0;
        }

        // (Re)creates the array with the given number of layers
        void allocate(size_t count, int res) {
            release();
            resolution = res;
            if (count == 0)
                return;

            GLint previous = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);

            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, res, res,
                         (GLsizei)count, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                         nullptr);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER,
                            GL_LINEAR);
            // Outside the map counts as the far plane, i.e. not in shadow
            const float border[] = {1.f, 1.f, 1.f, 1.f};
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_BORDER);
            glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR,
                             border);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            // Depth only: no color buffer to draw to or read from
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex,
                                      0, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
                GL_FRAMEBUFFER_COMPLETE)
                std::puts("[Error] Shadow map framebuffer is not complete!");

            glBindFramebuffer(GL_FRAMEBUFFER, previous);
            layers = count;
        }
    };

    Array arrays[2];
    GLuint ubo = 0, sampler = 0;

    // Matches ShadowBlock in lit.fs
    struct ShadowBlock {
        glm::mat4 sun[MAX_SUN_LAYERS];
        glm::mat4 small[MAX_SMALL_LAYERS];
    };
};

} // namespace render
