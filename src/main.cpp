#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <random>
#include <sys/types.h>
#include <vector>

#include "debug/debug_ui.hpp"
#include "ecs/UserMovement.hpp"
#include "ecs/camera.hpp"
#include "ecs/mesh.hpp"
#include "input/input.hpp"
#include "loaders/model_loader.hpp"
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

    // Use the actual framebuffer size (can differ from the window size
    // passed to glfwCreateWindow on HiDPI displays) so the viewport and the
    // camera's projection aspect ratio always agree.
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    auto texture_shader =
        std::make_shared<render::Shader>("default.vs", "texture.fs");
    auto texture_shader_instanced = std::make_shared<render::Shader>(
        "default_instanced.vs", "texture.fs", true);
    // Both models carry their own albedo texture, so one material is enough
    auto textured_material =
        std::make_shared<render::Material>("Textured", texture_shader);
    auto textured_material_instanced = std::make_shared<render::Material>(
        "TexturedInstanced", texture_shader_instanced);

    auto crate_model = std::make_shared<render::Model>(
        loader::loadModel("SM_PROP_crate_02.glb"));
    // Prototype grid ground: one bold grid square = 1 unit, fine lines every
    // 0.25. The texture has 2x2 bold squares, so one repeat spans 2 units.
    auto ground_model =
        std::make_shared<render::Model>(loader::createPlane(5.f, 1.f));
    auto grid_material =
        std::make_shared<render::Material>("Grid", texture_shader);
    grid_material->setTexture(render::TextureSlot::Albedo,
                              loader::loadTexture("grid_dark.png"));
    auto grass_model_instanced = std::make_shared<render::Model>(
        loader::loadModel("stylized_grass.glb"));

    std::vector<glm::mat4> grass_tranforms = std::vector<glm::mat4>();
    float scale = 1000.f;
    float bounds = 23000.f;
    // Fixed seed so the grass looks the same every run
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> yaw(0.f, glm::radians(360.f));

    for (float i = -bounds; i < bounds; i += scale)
        for (float j = -bounds; j < bounds; j += scale) {
            // Translate first, then rotate, so each blade spins in place
            // instead of the rotation swinging its grid offset around
            glm::mat4 t =
                glm::translate(glm::scale(glm::mat4(1.f), glm::vec3(.0001f)),
                               glm::vec3(i, 0, j));
            grass_tranforms.push_back(
                glm::rotate(t, yaw(rng), glm::vec3(0.f, 1.f, 0.f)));
        }
    grass_model_instanced->makeInstanced(grass_tranforms);

    // Create scene
    scn::Scene scene = scn::Scene();

    // Add crate
    auto crate_node = scn::Node::create("Melon crate");
    scene.root->add_child(crate_node);
    crate_node->add_component<ecs::Mesh>(crate_model, textured_material);

    // Add ground
    auto ground_node = scn::Node::create("Grid ground");
    scene.root->add_child(ground_node);
    ground_node->add_component<ecs::Mesh>(ground_model, grid_material);

    // Add instanced grass
    auto grass_node = scn::Node::create("Grass");
    grass_node->add_component<ecs::Mesh>(grass_model_instanced,
                                         textured_material_instanced);
    scene.root->add_child(grass_node);

    // Add camera
    auto camera_node = scn::Node::create("Camera", glm::vec3(2.f));
    scene.root->add_child(camera_node);
    auto camera = camera_node->add_component<ecs::Camera>(50.f, (float)fbWidth,
                                                          (float)fbHeight);
    camera->lookAt(glm::vec3(0));

    scene.activeCamera = camera;
    auto movement = camera_node->add_component<ecs::UserMovement>(1.f, 0.1f);

    input::bind(window, movement.get());

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    debug_ui::init(window);

    double time = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Calculate delta time
        double last_time = time;
        time = glfwGetTime();
        // Clamp so a long stall (window drag, breakpoint) doesn't teleport
        // everything in one update
        double dt = std::min(time - last_time, 0.1);

        // Update and draw the scene
        scene.update(dt);
        scene.draw();

        // if (time - last_print > 1.0) {
        //     scene.root->print(std::cout);
        //     last_print = time;
        // }

        debug_ui::draw(scene);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    debug_ui::shutdown();

    glfwTerminate();
    return 0;
}
