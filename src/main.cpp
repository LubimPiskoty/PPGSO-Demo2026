#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <random>
#include <sys/types.h>
#include <vector>

#include "components/UserMovement.hpp"
#include "components/animation.hpp"
#include "components/camera.hpp"
#include "components/mesh.hpp"
#include "debug/debug_ui.hpp"
#include "debug/profiler.hpp"
#include "input/input.hpp"
#include "loaders/model_loader.hpp"
#include "loaders/scene_loader.hpp"
#include "loaders/shader_loader.hpp"
#include "loaders/texture_loader.hpp"
#include "render/material.hpp"
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
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    GLFWwindow *window =
        glfwCreateWindow(1600, 900, "Hello Triangle", nullptr, nullptr);
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

    // Use the actual framebuffer size (can differ from the window size
    // passed to glfwCreateWindow on HiDPI displays) so the viewport and the
    // camera's projection aspect ratio always agree.
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    input::init(window);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    debug_ui::init(window);
    profiler::init();

    scn::Scene scene = loader::loadScene("scene.json");
    if (auto camera = scene.activeCamera.lock())
        camera->setSize((float)fbWidth, (float)fbHeight);

    double time = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        profiler::beginFrame();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Calculate delta time
        double last_time = time;
        time = glfwGetTime();
        // Clamp so a long stall (window drag, breakpoint) doesn't teleport
        // everything in one update
        double dt = std::min(time - last_time, 0.1);

        // Update and draw the scene
        {
            profiler::Scope _(profiler::Stage::Update);
            scene.update(dt);
        }
        {
            profiler::Scope _(profiler::Stage::Scene);
            scene.draw();
        }

        // if (time - last_print > 1.0) {
        //     scene.root->print(std::cout);
        //     last_print = time;
        // }

        {
            profiler::Scope _(profiler::Stage::UI);
            debug_ui::draw(scene);
        }

        profiler::endFrame();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    profiler::shutdown();
    debug_ui::shutdown();

    glfwTerminate();
    return 0;
}
