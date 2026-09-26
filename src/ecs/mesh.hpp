#pragma once

#include "../render/material.hpp"
#include "../render/model.hpp"
#include "../scene/scene.hpp"
#include "camera.hpp"
#include "ecs.hpp"
#include <cstdio>
#include <glm/matrix.hpp>
#include <memory>
#include <string>

namespace ecs {
class Mesh : public ecs::ComponentBase<Mesh> {

  public:
    std::shared_ptr<render::Model> model;
    std::shared_ptr<render::Material> material;

    Mesh(std::shared_ptr<render::Model> model,
         std::shared_ptr<render::Material> material)
        : material(material), model(model) {
        checkInstancedCompatibility();
    }

    const char *type_name() const override {
        return "Mesh";
    }

    std::string info() const override {
        std::string s = model->filename;
        if (model->isInstanced())
            s += ", " + std::to_string(model->getInstanceCount()) +
                 " instances";
        return s;
    }

    void draw(std::shared_ptr<ecs::Camera> camera) {
        if (checkInstancedCompatibility())
            return;

        material->setMat4("uProjection", camera->projection);
        material->setMat4("uView",
                          glm::inverse(camera->node.lock()->globalTransform()));
        material->setMat4("uModel", this->node.lock()->globalTransform());
        material->bind();

        model->draw();
    };

  private:
    bool checkInstancedCompatibility() {
        if (material->getShader()->isInstanced() != model->isInstanced()) {
            std::puts("[Error] Incompatible shader and model instance mode!");
            return true;
        }
        return false;
    }
};

} // namespace ecs
