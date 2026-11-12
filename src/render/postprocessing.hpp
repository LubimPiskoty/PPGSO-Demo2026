#pragma once

#include "../loaders/shader_loader.hpp"
#include "framebuffer.hpp"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

namespace render {

// The scene is drawn into a framebuffer, then every pass draws a fullscreen
// triangle reading the previous pass (uScreen) and the scene depth (uDepth).
// The result always ends up on the window, with no passes it's just a copy.
// The scene is drawn with MSAA and resolved before the passes.
//
// A thin G-buffer comes along: the depth prepass (Scene::depth_prepass)
// writes world-space normals beside the depth, the passes read them as
// uNormal (resolved on edges: normalize). Positions aren't stored, a pass
// rebuilds them from the depth and the camera (setCamera):
//
//   uniform sampler2D uDepth;
//   uniform mat4 uInvProjection; // view space from clip space
//   uniform mat4 uInvView;       // world from view space (the camera's)
//   vec3 viewPos(vec2 uv) {
//       float depth = texture(uDepth, uv).r; // 1: nothing drawn, the sky
//       vec4 p = uInvProjection * vec4(vec3(uv, depth) * 2.0 - 1.0, 1.0);
//       return p.xyz / p.w;
//   }
//   vec3 worldPos(vec2 uv) { return (uInvView * vec4(viewPos(uv), 1.0)).xyz; }
//
// gbuffer_debug.fs shows both. Each frame:
//   begin(); scene.depth_prepass(); shade(); scene.draw();
//   setCamera(...); end();
class PostProcessing {
  public:
    PostProcessing(int width, int height, int samples = 0) : samples(samples) {
        screen = loader::createShaderProgram("screen.vs", "screen.fs");
        glGenVertexArrays(1, &vao);
        resize(width, height);
    }

    void resize(int w, int h) {
        width = w;
        height = h;
        msaa = std::make_unique<Framebuffer>(w, h, true, true, samples, true);
        scene = std::make_unique<Framebuffer>(w, h, true, true, 0, true);
        for (auto &pass : passes)
            for (auto &fb : pass.fbs)
                fb = std::make_unique<Framebuffer>(w, h);
    }

    // Returns the program, so the caller can set its uniforms once.
    // A pass added off is skipped until setEnabled turns it on. repeats > 1
    // runs the shader that many times in a row, each reading the last, with
    // uPass = 0, 1, ...: a separable filter's horizontal and vertical halves.
    GLuint add(const std::string &fragmentPath, bool enabled = true,
               int repeats = 1) {
        GLuint program = loader::createShaderProgram("screen.vs", fragmentPath);
        Pass pass{program, {}, fragmentPath, enabled};
        for (int i = 0; i < std::max(repeats, 1); i++)
            pass.fbs.push_back(std::make_unique<Framebuffer>(width, height));
        passes.push_back(std::move(pass));
        return program;
    }

    // Passes by index, in the order added
    size_t passCount() const { return passes.size(); }
    GLuint program(size_t i) const { return passes[i].program; }
    // Index of the pass with that fragment shader, passCount() if none
    size_t find(const std::string &fragmentPath) const {
        for (size_t i = 0; i < passes.size(); i++)
            if (passes[i].shader == fragmentPath)
                return i;
        return passes.size();
    }
    // A texture of the pass's own (e.g. SSAO's noise), bound for every
    // run as `uniform` on units 4 and up. Not owned: the caller frees it.
    void addTexture(size_t i, const std::string &uniform, GLuint texture) {
        passes[i].textures.push_back({uniform, texture});
    }
    const std::string &passName(size_t i) const { return passes[i].shader; }
    bool isEnabled(size_t i) const { return passes[i].enabled; }
    void setEnabled(size_t i, bool enabled) { passes[i].enabled = enabled; }

    // Saves every buffer of the last frame as PNGs: <prefix>0_scene.png (the
    // scene, MSAA resolved), <prefix>0_depth.png (its depth map, see
    // Framebuffer::saveDepth; near and far are the camera's planes), then
    // <prefix>1_<pass>.png... one per pass in order, the last one is what
    // the window shows. Call after end().
    void save(const std::string &prefix, float near, float far) const {
        if (passes.size() == 0)
            scene->save(prefix + "0_scene.png");
        // scene->saveDepth(prefix + "0_depth.png", near, far);
        // scene->saveNormals(prefix + "0_normals.png");
        for (size_t i = 0; i < passes.size(); i++) {
            if (!passes[i].enabled)
                continue; // nothing drawn into it this frame
            std::string name = passes[i].shader;
            name = name.substr(0, name.find_last_of('.'));
            passes[i].fbs.back()->save(prefix + std::to_string(i + 1) + "_" +
                                       name + ".png");
        }
    }

    // Binds the scene's buffers, cleared, for the depth prepass: depth and
    // normals only
    void begin() {
        msaa->bind();
        msaa->drawTo(true, true);
        const float background[4] = {0.1f, 0.1f, 0.1f, 1.f};
        const float noNormal[4] = {0.f, 0.f, 0.f, 0.f}; // the sky
        glClearBufferfv(GL_COLOR, 0, background);
        glClearBufferfv(GL_COLOR, 1, noNormal);
        glClear(GL_DEPTH_BUFFER_BIT);
        msaa->drawTo(false, true);
    }

    // After the prepass: the lit pass draws the color, the normals stay
    void shade() { msaa->drawTo(true, false); }

    // Distance from the camera, m, of what's drawn at uv (0..1, from the
    // bottom left), from the last frame's depth (resolved in end()); the far
    // plane where nothing is. Reads back from the GPU: a few points a frame.
    float distanceAt(glm::vec2 uv) const {
        GLint previous = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, scene->getFBO());
        float depth = 1.f;
        glReadPixels(glm::clamp((int)(uv.x * width), 0, width - 1),
                     glm::clamp((int)(uv.y * height), 0, height - 1), 1, 1,
                     GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previous);
        const glm::vec4 p =
            invProjection * glm::vec4(glm::vec3(uv, depth) * 2.f - 1.f, 1.f);
        return glm::length(glm::vec3(p) / p.w);
    }

    // The camera the frame was drawn from, for the passes to rebuild
    // positions: its projection and its world transform (the inverse view)
    void setCamera(const glm::mat4 &projection, const glm::mat4 &world) {
        this->projection = projection;
        invProjection = glm::inverse(projection);
        invView = world;
    }

    void end() {
        msaa->drawTo(true, true);
        msaa->blitTo(*scene);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(vao);
        GLuint input = scene->getColorTex();
        for (auto &pass : passes) {
            if (!pass.enabled)
                continue;
            // What the layer started from, for its later runs (SSAO darkens
            // it by the occlusion its first run worked out)
            const GLuint layerInput = input;
            for (size_t r = 0; r < pass.fbs.size(); r++) {
                pass.fbs[r]->bind();
                draw(pass.program, input, (int)r, layerInput, &pass);
                input = pass.fbs[r]->getColorTex();
            }
        }
        Framebuffer::unbind(width, height);
        draw(screen, input, 0, input, nullptr);
        glEnable(GL_DEPTH_TEST);
    }

  private:
    struct Pass {
        GLuint program;
        // One per run of the shader (repeats), the last one is its result
        std::vector<std::unique_ptr<Framebuffer>> fbs;
        std::string shader; // fragment shader file, names the saved image
        bool enabled;
        // addTexture's: uniform name and texture
        std::vector<std::pair<std::string, GLuint>> textures;
    };

    int width, height, samples;
    glm::mat4 projection{1.f}, invProjection{1.f}, invView{1.f};
    GLuint screen, vao;
    std::unique_ptr<Framebuffer> msaa, scene;
    std::vector<Pass> passes;

    // uScreen the previous run's result, uLayerInput the image the layer
    // started from, uPass which run of the layer this is; then the pass's
    // own textures (addTexture)
    void draw(GLuint program, GLuint input, int run, GLuint layerInput,
              const Pass *pass) {
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uPass"), run);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, layerInput);
        glUniform1i(glGetUniformLocation(program, "uLayerInput"), 3);
        if (pass)
            for (size_t i = 0; i < pass->textures.size(); i++) {
                glActiveTexture(GL_TEXTURE4 + (GLenum)i);
                glBindTexture(GL_TEXTURE_2D, pass->textures[i].second);
                glUniform1i(glGetUniformLocation(
                                program, pass->textures[i].first.c_str()),
                            4 + (int)i);
            }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, scene->getDepthTex());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, scene->getNormalTex());
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(program, "uScreen"), 0);
        glUniform1i(glGetUniformLocation(program, "uDepth"), 1);
        glUniform1i(glGetUniformLocation(program, "uNormal"), 2);
        glUniformMatrix4fv(glGetUniformLocation(program, "uInvProjection"), 1,
                           GL_FALSE, glm::value_ptr(invProjection));
        glUniformMatrix4fv(glGetUniformLocation(program, "uInvView"), 1,
                           GL_FALSE, glm::value_ptr(invView));
        glUniformMatrix4fv(glGetUniformLocation(program, "uProjection"), 1,
                           GL_FALSE, glm::value_ptr(projection));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
};

} // namespace render
