#pragma once

#include "camera.hpp"
#include "ecs.hpp"
#include <memory>
#include <string>

namespace component {

    class Skybox : public ComponentBase<Skybox> {
    public:
        explicit Skybox(const std::string &textureFile);

        const char *type_name() const override {
            return "Skybox";
        }

        std::string info() const override {
            return textureFile;
        }

        void draw(const std::shared_ptr<Camera> &camera);

        void bindForReflections(); // bind texture for reflection

    private:
        std::string textureFile;
        unsigned int texture = 0;   // GLuint
        unsigned int shader = 0;
        unsigned int vao = 0;
        unsigned int vbo = 0;

        void createCube();
    };

} // namespace component