#include "loaders/model_loader.hpp"
#include "loaders/shader_loader.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window =
        glfwCreateWindow(800, 600, "Hello Triangle", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // enable vsync

    if (GLenum err = glewInit();
        err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY) {
        // GLEW_ERROR_NO_GLX_DISPLAY is a known false positive on Wayland/EGL
        // contexts (GLEW probes for a GLX display unconditionally); the
        // extension pointers are already loaded correctly by this point.
        std::cerr << "Failed to initialize GLEW: " << glewGetErrorString(err)
                  << std::endl;
        return -1;
    }
    glGetError(); // clear the spurious error glewInit() may leave behind

    glEnable(GL_DEPTH_TEST);

    // Load shaders
    auto shader = createShaderProgram("default.vs", "texture.fs");
    // Load models
    auto crate = loadModel("SM_PROP_crate_02.glb");
    // Create camera
    glm::mat4 camere_proj = glm::infinitePerspective(50.f, 1.f, 0.1f);
    glm::mat4 camera_pos = glm::mat4(1.f);
    camera_pos = glm::translate(camera_pos, glm::vec3(0, 0.12, -5));
    glm::mat4 model_mat =
        glm::rotate(glm::scale(glm::mat4(1.f), glm::vec3(1.f, -1.f, 1.f)),
                    glm::quarter_pi<float>(), glm::vec3(1, 0, 0));

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        GLint umodel = glGetUniformLocation(shader, "uModel");
        GLint uview = glGetUniformLocation(shader, "uView");
        GLint uprojection = glGetUniformLocation(shader, "uProjection");
        GLint utexture = glGetUniformLocation(shader, "uTexture");
        glUseProgram(shader);

        // Time
        float time = glfwGetTime();

        model_mat =
            glm::rotate(model_mat, (float)1e-2, glm::vec3(0.f, 1.f, 0.f));
        // Update uniforms
        glUniformMatrix4fv(umodel, 1, false, &model_mat[0][0]);
        glUniformMatrix4fv(uview, 1, false, &camera_pos[0][0]);
        glUniformMatrix4fv(uprojection, 1, false, &camere_proj[0][0]);
        glUniform1i(utexture, 0);

        crate.draw();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
