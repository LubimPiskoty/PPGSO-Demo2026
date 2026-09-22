#pragma once

#include "../loaders/model_loader.hpp"
#include "../loaders/shader_loader.hpp"
#include "../scene/scene.hpp"
#include "camera.hpp"
#include "ecs.hpp"
#include <glm/matrix.hpp>
#include <memory>

namespace ecs {
class Mesh : public ecs::ComponentBase<Mesh> {
    std::weak_ptr<loader::Model> model;
    GLuint shader;

  public:
    Mesh(const std::weak_ptr<loader::Model> model, GLuint shader) {
        this->model = model;
        this->shader = shader;
    }

    const char *type_name() const override { return "Mesh"; }

    void draw(std::shared_ptr<ecs::Camera> camera) {
        glUseProgram(this->shader);
        uTexture("uTexture", 0);
        uMat4("uProjection", camera->projection);
        uMat4("uView", glm::inverse(camera->node.lock()->globalTransform()));
        uMat4("uModel", this->node.lock()->globalTransform());
        this->model.lock()->draw();
    };
};
} // namespace ecs
