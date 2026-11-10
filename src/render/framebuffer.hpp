#pragma once

#include "GL/glew.h"
#include <cstdint>
#include <cstdio>
#include <glm/common.hpp>
#include <stb_image_write.h>
#include <string>
#include <vector>

namespace render {

// https://learnopengl.com/Advanced-OpenGL/Framebuffers
class Framebuffer {
  public:
    // color = false gives a depth-only buffer (e.g. for shadow maps).
    // samples > 0 gives multisampled textures, resolve them with blitTo.
    Framebuffer(uint16_t width = 1024, uint16_t height = 1024,
                bool depth = false, bool color = true, int samples = 0)
        : width(width), height(height), depth(depth), color(color),
          samples(samples) {
        GLint previous = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);

        glGenFramebuffers(1, &fbo);
        // Attachments go to the bound framebuffer, so bind ours first
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        if (color)
            addTexture(&colorTex);
        if (depth)
            addTexture(&depthTex, true);

        if (!color) {
            // No color attachment: without this the framebuffer is incomplete
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::puts("[Error] Framebuffer is not complete!");

        glBindFramebuffer(GL_FRAMEBUFFER, previous);
    }

    ~Framebuffer() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteTextures(1, &depthTex);
    }

    Framebuffer(const Framebuffer &) = delete;
    Framebuffer &operator=(const Framebuffer &) = delete;

    // Binds this buffer for read/draw and sets the viewport to its size
    void bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }
    // Back to the window. The viewport is left alone, restore it with the
    // overload below (or set it yourself).
    static void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    static void unbind(int windowWidth, int windowHeight) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowWidth, windowHeight);
    }

    // Copies (and resolves MSAA of) color and depth into `target`, which
    // must be the same size and not multisampled
    void blitTo(const Framebuffer &target) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                          GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);
    }

    // Writes the color texture to a PNG (8 bit, values clamped to 0..1).
    // Not for multisampled buffers, blitTo a plain one first.
    bool save(const std::string &path) const {
        if (!colorTex || samples > 0) {
            std::printf("[Error] Can't save %s: no plain color texture\n",
                        path.c_str());
            return false;
        }
        std::vector<unsigned char> pixels((size_t)width * height * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE,
                      pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        // OpenGL's rows start at the bottom, PNG's at the top
        stbi_flip_vertically_on_write(1);
        const bool ok = stbi_write_png(path.c_str(), width, height, 3,
                                       pixels.data(), width * 3) != 0;
        if (!ok)
            std::printf("[Error] Can't write %s\n", path.c_str());
        return ok;
    }

    // Writes the depth texture to a grey PNG: the distance from the camera
    // (near and far are the camera's planes), black at the camera, white at
    // the farthest thing drawn, so the picture uses the whole range whatever
    // the far plane is. Nothing drawn (the sky) is white too.
    // Not for multisampled buffers, blitTo a plain one first.
    bool saveDepth(const std::string &path, float near, float far) const {
        if (!depthTex || samples > 0) {
            std::printf("[Error] Can't save %s: no plain depth texture\n",
                        path.c_str());
            return false;
        }
        std::vector<float> depth((size_t)width * height);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                      depth.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        // Buffer depth (0..1, most of it used up close) -> metres
        float farthest = near;
        for (float &d : depth) {
            if (d >= 1.f) {
                d = -1.f; // nothing drawn here
                continue;
            }
            d = 2.f * near * far / (far + near - (2.f * d - 1.f) * (far - near));
            farthest = glm::max(farthest, d);
        }
        std::vector<unsigned char> pixels(depth.size());
        for (size_t i = 0; i < depth.size(); i++)
            pixels[i] = depth[i] < 0.f
                            ? 255
                            : (unsigned char)(255.f * (depth[i] - near) /
                                              glm::max(farthest - near, 1e-6f));
        stbi_flip_vertically_on_write(1);
        const bool ok = stbi_write_png(path.c_str(), width, height, 1,
                                       pixels.data(), width) != 0;
        if (!ok)
            std::printf("[Error] Can't write %s\n", path.c_str());
        return ok;
    }

    unsigned int getFBO() const { return fbo; }
    GLuint getColorTex() const { return colorTex; }
    // 0 if the buffer was created without a depth attachment
    GLuint getDepthTex() const { return depthTex; }
    uint16_t getWidth() const { return width; }
    uint16_t getHeight() const { return height; }

  private:
    GLuint fbo = 0;
    uint16_t width, height;
    bool depth, color;
    int samples;
    GLuint colorTex = 0, depthTex = 0;

    // Creates the texture and attaches it to the bound framebuffer
    void addTexture(GLuint *texture, bool is_depth = false) {
        const auto attachement =
            is_depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
        // 16-bit float color, so HDR values above 1 survive
        const GLint internalFormat =
            is_depth ? GL_DEPTH_COMPONENT24 : GL_RGB16F;
        const auto format = is_depth ? GL_DEPTH_COMPONENT : GL_RGB;
        const auto type = GL_FLOAT;

        glGenTextures(1, texture);

        if (samples > 0) {
            // Multisampled textures have no filtering or wrapping
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, *texture);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples,
                                    internalFormat, width, height, GL_TRUE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachement,
                                   GL_TEXTURE_2D_MULTISAMPLE, *texture, 0);
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
            return;
        }

        glBindTexture(GL_TEXTURE_2D, *texture);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format,
                     type, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (is_depth) {
            // Outside the buffer counts as the far plane, i.e. not in shadow.
            // Edge clamping would smear the border texels over the scene.
            const float border[] = {1.f, 1.f, 1.f, 1.f};
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_BORDER);
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        }

        // Attach to the bound framebuffer
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachement, GL_TEXTURE_2D,
                               *texture, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
};
} // namespace render
