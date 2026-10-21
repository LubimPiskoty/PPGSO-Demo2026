#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>

#include "debug/debug_ui.hpp"
#include "debug/profiler.hpp"
#include "input/input.hpp"
#include "loaders/scene_loader.hpp"
#include "render/framebuffer.hpp"
#include "scenes/ball_jar.hpp"
#include "scenes/barrel_run.hpp"
#include "scenes/light_test.hpp"
#include "scenes/particle_test.hpp"
#include "scenes/physics_test.hpp"
#include "scenes/scene_manager.hpp"

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_SAMPLES, 8);

    GLFWwindow *window =
        glfwCreateWindow(1600, 900, "Hello Triangle", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    // Vsync, physics has its own fixed timestep
    glfwSwapInterval(1);

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
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glDepthFunc(GL_LESS);
    glEnable(GL_MULTISAMPLE);

    debug_ui::init(window);
    profiler::init();

    scenes::SceneManager scenes;
    scenes.add("Light test", scenes::lightTest);
    // scenes.add("Physics: bounce", scenes::physicsBounce);
    // scenes.add("Physics: slide", scenes::physicsSlide);
    // scenes.add("Physics: impact", scenes::physicsImpact);
    // scenes.add("Physics: break", scenes::physicsBreak);
    // scenes.add("Ball jar", scenes::ballJar);
    scenes.add("Particle test", scenes::particle_test);
    scenes.add("Barrel run", scenes::barrelRun);
    scenes.add("scene.json", [] { return loader::loadScene("scene.json"); });
    scenes.add("diorama.json",
               [] { return loader::loadScene("diorama.json"); });
    // scenes.add("village_scene.json",
    //            [] { return loader::loadScene("village_scene.json"); });
    scenes.setViewport((float)fbWidth, (float)fbHeight);
    scenes.load(0);

    render::Framebuffer fb(1024, 1024, true, false);
    debug_ui::setDepthPreview(fb.getDepthTex(), fb.getWidth(), fb.getHeight());

    double time = glfwGetTime();
    double physicsAccumulator = 0.0;
    while (!glfwWindowShouldClose(window)) {
        profiler::beginFrame();

        // Scene switches/reloads requested by the UI last frame happen here,
        // never in the middle of an update or draw
        scenes.applyPending();
        scn::Scene &scene = scenes.scene();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Calculate delta time
        double last_time = time;
        time = glfwGetTime();
        // Clamp so a long stall (window drag, breakpoint) doesn't teleport
        // everything in one update
        float dt = std::min((float)(time - last_time), 0.1f);

        // Update and draw the scene
        {
            profiler::Scope _(profiler::Stage::Update);
            scene.update(dt * scene.physics_context.timeScale);
        }
        {
            profiler::Scope _(profiler::Stage::Physics);
            // Fixed timestep, capped so slow frames don't spiral
            constexpr int kMaxPhysicsSteps = 12;
            const double fixedDt = scene.physics_context.fixedDt;
            physicsAccumulator =
                std::min(physicsAccumulator + dt, kMaxPhysicsSteps * fixedDt);
            while (physicsAccumulator >= fixedDt) {
                scene.physics_step();
                physicsAccumulator -= fixedDt;
            }
        }
        {
            profiler::Scope _(profiler::Stage::Scene);
            scene.draw();
        }

        fb.bind();
        glClear(GL_DEPTH_BUFFER_BIT);
        scene.draw();
        fb.unbind(fbWidth, fbHeight);
        debug_ui::draw(scenes);

        profiler::endFrame();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    profiler::shutdown();
    debug_ui::shutdown();

    glfwTerminate();
    return 0;
}
