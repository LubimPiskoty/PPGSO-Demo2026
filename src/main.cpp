#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <iostream>
#include <sys/types.h>

#include "ecs/UserMovement.hpp"
#include "ecs/camera.hpp"
#include "ecs/mesh.hpp"
#include "input/input.hpp"
#include "loaders/model_loader.hpp"
#include "loaders/scene_loader.hpp"
#include "loaders/shader_loader.hpp"
#include "scene/scene.hpp"

#include "glm/gtc/matrix_transform.hpp"

#ifdef WITH_IMGUI
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#endif

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
        loader::createShaderProgram("default.vs", "texture.fs");
    auto crate_model = std::make_shared<loader::Model>(
        loader::loadModel("SM_PROP_crate_02.glb"));
    auto ground_model =
        std::make_shared<loader::Model>(loader::loadModel("grass_plane.glb"));
    scn::Scene scene = scn::Scene();

    auto crate_node = scn::Node::create("Melon crate");
    scene.root->add_child(crate_node);
    crate_node->add_component<ecs::Mesh>(crate_model, texture_shader);

    auto ground_node =
        scn::Node::create("Grass ground", glm::vec3(0.f, -0.2f, 0.f));
    scene.root->add_child(ground_node);
    ground_node->add_component<ecs::Mesh>(ground_model, texture_shader);
    ground_node->localScale = glm::vec3(.7f);

    auto camera_node = scn::Node::create("Camera", glm::vec3(2.f));
    scene.root->add_child(camera_node);
    auto camera = camera_node->add_component<ecs::Camera>(50.f, (float)fbWidth,
                                                          (float)fbHeight);
    camera->lookAt(glm::vec3(0));

    scene.activeCamera = camera;
    auto movement = camera_node->add_component<ecs::UserMovement>(0.2f, 0.1f);

    input::bind(window, movement.get());

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

#ifdef WITH_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
#endif

    double time;
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        time = glfwGetTime();
        scene.update(0.16);
        scene.draw();

        // if (time - last_print > 1.0) {
        //     scene.root->print(std::cout);
        //     last_print = time;
        // }

#ifdef WITH_IMGUI
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Scene");
        ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::TextUnformatted(scene.root->to_string().c_str());
        ImGui::Separator();
        if (ImGui::Button("Save scene"))
            loader::saveScene(&scene, "scene.json");
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

#ifdef WITH_IMGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif

    glfwTerminate();
    return 0;
}
