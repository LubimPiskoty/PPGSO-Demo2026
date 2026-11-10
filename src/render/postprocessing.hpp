#pragma once

#include "../loaders/shader_loader.hpp"
#include "framebuffer.hpp"
#include <memory>
#include <string>
#include <vector>

namespace render {

// The scene is drawn into a framebuffer, then every pass draws a fullscreen
// triangle reading the previous pass (uScreen) and the scene depth (uDepth).
// The result always ends up on the window, with no passes it's just a copy.
// The scene is drawn with MSAA and resolved before the passes.
class PostProcessing {
  public:
    PostProcessing(int width, int height, int samples = 0)
        : samples(samples) {
        screen = loader::createShaderProgram("screen.vs", "screen.fs");
        glGenVertexArrays(1, &vao);
        resize(width, height);
    }

    void resize(int w, int h) {
        width = w;
        height = h;
        msaa = std::make_unique<Framebuffer>(w, h, true, true, samples);
        scene = std::make_unique<Framebuffer>(w, h, true);
        for (auto &pass : passes)
            pass.fb = std::make_unique<Framebuffer>(w, h);
    }

    // Returns the program, so the caller can set its uniforms once
    GLuint add(const std::string &fragmentPath) {
        GLuint program = loader::createShaderProgram("screen.vs", fragmentPath);
        passes.push_back({program, std::make_unique<Framebuffer>(width, height),
                          fragmentPath});
        return program;
    }

    // Saves every buffer of the last frame as PNGs: <prefix>0_scene.png (the
    // scene, MSAA resolved), <prefix>0_depth.png (its depth map, see
    // Framebuffer::saveDepth; near and far are the camera's planes), then
    // <prefix>1_<pass>.png... one per pass in order, the last one is what
    // the window shows. Call after end().
    void save(const std::string &prefix, float near, float far) const {
        scene->save(prefix + "0_scene.png");
        scene->saveDepth(prefix + "0_depth.png", near, far);
        for (size_t i = 0; i < passes.size(); i++) {
            std::string name = passes[i].shader;
            name = name.substr(0, name.find_last_of('.'));
            passes[i].fb->save(prefix + std::to_string(i + 1) + "_" + name +
                               ".png");
        }
    }

    void begin() {
        msaa->bind();
        glClearColor(0.1f, 0.1f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void end() {
        msaa->blitTo(*scene);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(vao);
        GLuint input = scene->getColorTex();
        for (auto &pass : passes) {
            pass.fb->bind();
            draw(pass.program, input);
            input = pass.fb->getColorTex();
        }
        Framebuffer::unbind(width, height);
        draw(screen, input);
        glEnable(GL_DEPTH_TEST);
    }

  private:
    struct Pass {
        GLuint program;
        std::unique_ptr<Framebuffer> fb;
        std::string shader; // fragment shader file, names the saved image
    };

    int width, height, samples;
    GLuint screen, vao;
    std::unique_ptr<Framebuffer> msaa, scene;
    std::vector<Pass> passes;

    void draw(GLuint program, GLuint input) {
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, scene->getDepthTex());
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(program, "uScreen"), 0);
        glUniform1i(glGetUniformLocation(program, "uDepth"), 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
};

} // namespace render
