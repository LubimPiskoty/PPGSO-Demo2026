#pragma once

#include "GL/glew.h"
#include <cstdint>
#include <cstdio>
#include <glm/common.hpp>

namespace render {

// https://learnopengl.com/Advanced-OpenGL/Framebuffers
class Framebuffer {
  public:
    // color = false gives a depth-only buffer (e.g. for shadow maps)
    Framebuffer(uint16_t width = 1024, uint16_t height = 1024,
                bool depth = false, bool color = true)
        : width(width), height(height), depth(depth), color(color) {
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
    GLuint colorTex = 0, depthTex = 0;

    // Creates the texture and attaches it to the bound framebuffer
    void addTexture(GLuint *texture, bool is_depth = false) {
        const auto attachement =
            is_depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
        const GLint internalFormat = is_depth ? GL_DEPTH_COMPONENT24 : GL_RGB;
        const auto format = is_depth ? GL_DEPTH_COMPONENT : GL_RGB;
        const auto type = is_depth ? GL_FLOAT : GL_UNSIGNED_BYTE;

        glGenTextures(1, texture);
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
