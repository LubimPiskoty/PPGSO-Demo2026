#include "skybox.hpp"
#include <GL/glew.h>

#include "../loaders/shader_loader.hpp"
#include "../loaders/texture_loader.hpp"
#include "../scene/scene.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace component {

Skybox::Skybox(const std::string &textureFile) : textureFile(textureFile) {
  texture = loader::loadCubemapCross(textureFile);
  shader = loader::createShaderProgram("skybox.vs", "skybox.fs");
  createCube();
}

Skybox::~Skybox() {
  glDeleteTextures(1, &texture);
  glDeleteProgram(shader);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
}

void Skybox::draw(const std::shared_ptr<Camera> &camera) {
  auto camNode = camera->node.lock();
  if (!camNode)
    return;

  glm::mat4 view = glm::inverse(camNode->globalTransform());

  glDepthFunc(GL_LEQUAL);
  // Tested against the prepass's depth, not written: where nothing is drawn
  // the depth stays the cleared 1, the post passes read that as the sky
  glDepthMask(GL_FALSE);
  // Seen from inside the cube
  const bool cull = glIsEnabled(GL_CULL_FACE);
  glDisable(GL_CULL_FACE);
  glUseProgram(shader);
  glUniformMatrix4fv(glGetUniformLocation(shader, "uView"), 1, GL_FALSE,
                     glm::value_ptr(view));
  glUniformMatrix4fv(glGetUniformLocation(shader, "uProjection"), 1, GL_FALSE,
                     glm::value_ptr(camera->projection));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
  glUniform1i(glGetUniformLocation(shader, "uSkybox"), 0);
  glUniform3fv(glGetUniformLocation(shader, "uTint"), 1, glm::value_ptr(tint));

  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  if (cull)
    glEnable(GL_CULL_FACE);
}

void Skybox::createCube() {
  static const float vertices[] = {
      -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f,
      1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f,

      -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f,
      -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,

      1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f,

      -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,

      -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,

      -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f,
      1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f};

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
  glBindVertexArray(0);
}

  void Skybox::bindForReflections() {
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
      glActiveTexture(GL_TEXTURE0);
}

} // namespace component
