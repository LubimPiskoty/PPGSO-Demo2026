#pragma once

#include "../render/material.hpp"
#include "../render/model.hpp"
#include "../scene/scene.hpp"
#include "camera.hpp"
#include "ecs.hpp"
#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
#include <memory>
#include <string>

namespace component {
class Mesh : public component::ComponentBase<Mesh> {

  public:
    std::shared_ptr<render::Model> model;
    std::shared_ptr<render::Material> material;

    Mesh(std::shared_ptr<render::Model> model,
         std::shared_ptr<render::Material> material)
        : material(material), model(model) {}

    const char *type_name() const override { return "Mesh"; }

    std::string info() const override {
        std::string s = model->filename;
        if (model->isInstanced())
            s +=
                ", " + std::to_string(model->getInstanceCount()) + " instances";
        return s;
    }

    // Normal pass: sets the camera on the material, binds it and draws
    void draw(glm::mat4 projection, glm::mat4 invCameraTransform) override {
        if (checkInstancedCompatibility())
            return;

        material->setMat4("uProjection", projection);
        material->setMat4("uView", invCameraTransform);
        material->setMat4("uModel", this->node.lock()->globalTransform());
        material->bind();

        model->draw();
    };

    // Shadow pass: no material, just positions through the given depth shader
    void drawDepth(glm::mat4 projection, glm::mat4 lightView,
                   const render::Shader &depthShader) {
        glUseProgram(depthShader.program);
        setMat4(depthShader.program, "uProjection", projection);
        setMat4(depthShader.program, "uView", lightView);
        setMat4(depthShader.program, "uModel",
                this->node.lock()->globalTransform());
        model->draw();
    }

  private:
    static void setMat4(GLuint program, const char *name, const glm::mat4 &m) {
        glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE,
                           glm::value_ptr(m));
    }

    bool checkInstancedCompatibility() {
        if (material->getShader()->isInstanced() != model->isInstanced()) {
            std::puts("[Error] Incompatible shader and model instance mode!");
            return true;
        }
        return false;
    }
};

} // namespace component
