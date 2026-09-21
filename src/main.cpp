#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "loaders/model_loader.hpp"
#include "loaders/shader_loader.hpp"
#include "scene/scene.hpp"

#include "glm/gtc/matrix_transform.hpp"

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window =
        glfwCreateWindow(800, 600, "Hello Triangle", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // glewExperimental = GL_TRUE; // required for core profile contexts
    GLenum glewStatus = glewInit();
    // Under Wayland glewInit reports NO_GLX_DISPLAY even though all GL
    // function pointers were loaded fine, so that one is not fatal.
    if (glewStatus != GLEW_OK && glewStatus != GLEW_ERROR_NO_GLX_DISPLAY) {
        std::cerr << "Failed to initialize GLEW: "
                  << glewGetErrorString(glewStatus) << std::endl;
        return -1;
    }

    auto texture_shader = createShaderProgram("default.vs", "texture.fs");
    auto crate_model = loadModel("SM_PROP_crate_02.glb");

    scn::Scene scene = scn::Scene();

    auto crate_node = scn::Node::create("Melon crate", glm::vec3(0.f));
    scene.tree->add_child(crate_node);
    // Rotate it towards camera
    crate_node->localTransform =
        glm::rotate(crate_node->localTransform, glm::pi<float>() * 5.f / 6.f,
                    glm::vec3(1, 0, 0));

    crate_node->localTransform =
        glm::rotate(crate_node->localTransform,
                    glm::half_pi<float>() * 3.f / 2.f, glm::vec3(0, 1, 0));

    auto camera_node = scn::Node::create("Camera", glm::vec3(0.f, 0.f, -4.f));
    scene.tree->add_child(camera_node);
    // Make camera projection matrix
    auto proj_mat = glm::perspectiveFov(50.0, 800.0, 600.0, 0.1, 1000.0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(texture_shader);
        uTexture("uTexture", 0);
        uMat4("uView", camera_node->globalTransform());
        uMat4("uModel", crate_node->globalTransform());
        uMat4("uProjection", proj_mat);
        crate_model.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
