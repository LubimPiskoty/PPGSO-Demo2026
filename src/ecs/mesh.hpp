#pragma once

#include "../render/material.hpp"
#include "../render/model.hpp"
#include "../scene/scene.hpp"
#include "camera.hpp"
#include "ecs.hpp"
#include <glm/matrix.hpp>
#include <memory>

namespace ecs {
class Mesh : public ecs::ComponentBase<Mesh> {

  public:
    std::shared_ptr<render::Model> model;
    std::shared_ptr<render::Material> material;

    Mesh(std::shared_ptr<render::Model> model,
         std::shared_ptr<render::Material> material)
        : material(material), model(model) {}

    const char *type_name() const override {
        return "Mesh";
    }

    void draw(std::shared_ptr<ecs::Camera> camera) {
        material->setMat4("uProjection", camera->projection);
        material->setMat4("uView",
                          glm::inverse(camera->node.lock()->globalTransform()));
        material->setMat4("uModel", this->node.lock()->globalTransform());
        material->bind();

        model->draw();
    };
};
} // namespace ecs
