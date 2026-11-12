#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>

#include "debug/debug_ui.hpp"
#include "debug/profiler.hpp"
#include "input/input.hpp"
#include "render/framebuffer.hpp"
#include "render/postprocessing.hpp"
#include "render/dof.hpp"
#include "render/ssao.hpp"
#include "scenes/ball_jar.hpp"
#include "scenes/demo_scene/demo_scene.hpp"
#include "scenes/particle_test.hpp"
#include "scenes/physics_test.hpp"
#include "scenes/scene_manager.hpp"
#include "scenes/shadow_test.hpp"
#include "scenes/smithy/smithy_scene.hpp"

// The scenes, the post-processing and the frame loop, until the window
// closes
static void run(GLFWwindow *window, int fbWidth, int fbHeight) {
#ifdef NDEBUG
    render::PostProcessing post(fbWidth, fbHeight, 8);
#else
    // No MSAA in Debug, it costs a lot of GPU time
    render::PostProcessing post(fbWidth, fbHeight, 0);
#endif
    // The post-processing layers, in the order they run, each toggled by
    // its number key (1 the first). Added off are left out until turned on.
    struct Layer {
        const char *shader;
        bool on;
        int repeats = 1; // runs of the shader (PostProcessing::add)
    };
    static constexpr Layer LAYERS[] = {
        // Ambient occlusion (render::Ssao), then darkened by it
        {"ssao.fs", true, 2},
        // {"blur.fs", false, 2}, // Gaussian: horizontal, then vertical
        // {"inverse.fs", false},
        // Depth of field: blurred distances, then a disc kernel; tilt-shift
        // in the timelapse (render::Dof)
        {"dof.fs", true, 4},
        {"bloom.fs", true},
        // The thin G-buffer in quarters: scene, normals, positions, distance
        {"gbuffer_debug.fs", false},
        // Last: everything before works on linear light (colour textures
        // are sRGB, decoded when sampled), this encodes it for the screen
        {"gama.fs", true},
    };
    for (const Layer &layer : LAYERS)
        post.add(layer.shader, layer.on, layer.repeats);
    // Darkens the frame by the scene's fade (the demo's cuts): always on,
    // last, not a layer
    const GLuint fadePass = post.add("fade_transition.fs");
    // ssao.fs's sample kernel and noise
    const render::Ssao ssao(post);
    // dof.fs's focus, eased, and its tilt-shift
    render::Dof dof(post);

    scenes::SceneManager scenes;
    scenes.add("Demo scene", scenes::demoScene);
    // Shot 4 of the demo, the demo scene cuts to it by this name
    scenes.add("Smithy interior", scenes::smithyScene);
    scenes.add("Shadow test", scenes::shadowTest);
    scenes.add("Particle test", scenes::particle_test);
    scenes.setViewport((float)fbWidth, (float)fbHeight);
    scenes.load(0);

    // F12 saves every buffer of the next frame (the scene, its depth map
    // and each post-processing pass) into capture/
    bool captureRequested = false;
    // Space skips 5 s ahead
    constexpr float SKIP = 5.f;
    float skip = 0.f;
    // 1-9 toggle the post-processing layers (LAYERS above, pass i the
    // i-th layer)
    // Captures this function's locals: removed when it returns
    const int keys =
        input::onKey([&captureRequested, &skip, &post](int key, int action) {
            if (key == GLFW_KEY_F12 && action == GLFW_PRESS)
                captureRequested = true;
            if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
                skip += SKIP;
            const size_t layer = (size_t)(key - GLFW_KEY_1);
            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 &&
                action == GLFW_PRESS && layer < std::size(LAYERS)) {
                post.setEnabled(layer, !post.isEnabled(layer));
                std::cout << "Post " << layer + 1 << " " << post.passName(layer)
                          << (post.isEnabled(layer) ? ": on" : ": off")
                          << std::endl;
            }
        });
    int lastFbWidth = fbWidth, lastFbHeight = fbHeight;

    double time = glfwGetTime();
    double physicsAccumulator = 0.0;
    while (!glfwWindowShouldClose(window)) {
        profiler::beginFrame();

        // Handle window resize
        int curW, curH;
        glfwGetFramebufferSize(window, &curW, &curH);
        if (curW > 0 && curH > 0 &&
            (curW != lastFbWidth || curH != lastFbHeight)) {
            lastFbWidth = curW;
            lastFbHeight = curH;
            glViewport(0, 0, curW, curH);
            scenes.setViewport((float)curW, (float)curH);
            post.resize(curW, curH);
        }

        // Scene switches/reloads requested by the UI last frame happen here,
        // never in the middle of an update or draw
        scenes.applyPending();
        scn::Scene &scene = scenes.scene();

        // Calculate delta time
        double last_time = time;
        time = glfwGetTime();
        // Clamp so a long stall (window drag, breakpoint) doesn't teleport
        // everything in one update
        float dt = std::min((float)(time - last_time), 0.1f);
        // A skip runs the time in 0.1 s updates (and its physics) before
        // this frame's, nothing drawn in between
        for (; skip > 0.f; skip -= 0.1f) {
            const float step = std::min(skip, 0.1f);
            scene.update(step);
            for (double t = 0.0; t < step; t += scene.physics_context.fixedDt)
                scene.physics_step();
        }

        // Update and draw the scene
        {
            profiler::Scope _(profiler::Stage::Update);
            scene.update(dt); // applies the timescale itself
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
            scene.shadow_pass();
            post.begin();
            scene.depth_prepass();
            post.shade();
            scene.draw();
            if (auto camera = scene.activeCamera.lock())
                post.setCamera(camera->projection,
                               camera->node.lock()->globalTransform());
            dof.update(dt, *scene.tiltShift);
            glUseProgram(fadePass);
            glUniform1f(glGetUniformLocation(fadePass, "transition"),
                        *scene.fade);
            post.end();
        }

        if (captureRequested) {
            captureRequested = false;
            std::filesystem::create_directories("capture");
            // capture/<date>_<time>_<frame>_..., so several in one second
            // don't overwrite each other
            static int captured = 0;
            char stamp[32];
            const std::time_t now = std::time(nullptr);
            std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S",
                          std::localtime(&now));
            const std::string prefix = "capture/" + std::string(stamp) + "_" +
                                       std::to_string(captured++) + "_";
            // The depth map needs the camera's planes
            float near = 0.1f, far = 100.f;
            if (auto camera = scene.activeCamera.lock()) {
                near = camera->getNear();
                far = camera->getFar();
            }
            post.save(prefix, near, far);
            std::cout << "Saved the frame's buffers to " << prefix << "*.png"
                      << std::endl;
        }

        debug_ui::draw(scenes);

        profiler::endFrame();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    input::unsubscribe(keys);
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    GLFWwindow *window = glfwCreateWindow(
        1600, 900, "Village Demo by Piskotka and David PPGSO-ZS2026", nullptr,
        nullptr);
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
    // Back faces culled, Material::doubleSided turns it off per draw
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    debug_ui::init(window);
    profiler::init();

    // Everything holding GL objects lives in run(), so it's freed while the
    // context still exists
    run(window, fbWidth, fbHeight);

    profiler::shutdown();
    debug_ui::shutdown();

    glfwTerminate();
    return 0;
}
