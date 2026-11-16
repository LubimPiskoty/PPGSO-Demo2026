#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <stb_image_write.h>
#include <string>
#include <vector>

#include "debug/debug_ui.hpp"
#include "debug/profiler.hpp"
#include "input/input.hpp"
#include "render/framebuffer.hpp"
#include "render/postprocessing.hpp"
#include "render/ssao.hpp"
#include "scenes/ball_jar.hpp"
#include "scenes/demo_scene/demo_scene.hpp"
#include "scenes/particle_test.hpp"
#include "scenes/physics_test.hpp"
#include "scenes/scene_manager.hpp"
#include "scenes/shadow_test.hpp"
#include "scenes/smithy/smithy_scene.hpp"

// Render mode: every frame at a fixed time step saved as a PNG, for a video
// (ffmpeg -framerate 30 -i frames/%05d.png -pix_fmt yuv420p demo.mp4):
//   semestralka --render frames [--size 1920x1080] [--fps 30] [--seconds 120]
//               [--start 40]   (skips ahead first, like Space)
//               [--msaa 8]     (samples, 0 none; else the build's: 4, Debug 0)
// Drawn at that size whatever the window's (the window shows a corner of
// it), as fast as it goes, without the debug UI. Without --seconds it runs
// until the window is closed.
struct RenderMode {
    std::string dir; // empty: the normal, real time app
    int width = 1920, height = 1080;
    int fps = 30;
    float seconds = 0.f; // 0: until the window closes
    float start = 0.f;   // s into the demo the first frame is
    int msaa = -1;       // MSAA samples, 0 none; below 0 the build's (4 / 0)
    bool on() const { return !dir.empty(); }
};

static RenderMode parseArgs(int argc, char **argv) {
    RenderMode mode;
    for (int i = 1; i + 1 < argc; i++) {
        const std::string arg = argv[i], value = argv[i + 1];
        if (arg == "--render")
            mode.dir = value;
        else if (arg == "--size")
            std::sscanf(value.c_str(), "%dx%d", &mode.width, &mode.height);
        else if (arg == "--fps")
            mode.fps = std::max(std::atoi(value.c_str()), 1);
        else if (arg == "--seconds")
            mode.seconds = (float)std::atof(value.c_str());
        else if (arg == "--start")
            mode.start = (float)std::atof(value.c_str());
        else if (arg == "--msaa")
            mode.msaa = std::max(std::atoi(value.c_str()), 0);
        else
            continue;
        i++;
    }
    return mode;
}

// The finished frame (the post-processing's last pass) into a PNG
static void saveFrame(const render::PostProcessing &post,
                      const std::string &path) {
    const int w = post.getWidth(), h = post.getHeight();
    std::vector<unsigned char> rgb((size_t)w * h * 3);
    glBindTexture(GL_TEXTURE_2D, post.finalTexture());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // Already tonemapped and encoded for the screen: 0..1, clamped to bytes
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_flip_vertically_on_write(1); // GL's rows go bottom up
    if (!stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3))
        std::cerr << "Couldn't write " << path << std::endl;
}

// The scenes, the post-processing and the frame loop, until the window
// closes (or the render mode's length is done)
static void run(GLFWwindow *window, int fbWidth, int fbHeight,
                const RenderMode &rendering) {
    if (rendering.on()) {
        fbWidth = rendering.width;
        fbHeight = rendering.height;
        std::filesystem::create_directories(rendering.dir);
    }
#ifdef NDEBUG
    int samples = 4;
#else
    // No MSAA in Debug, it costs a lot of GPU time
    int samples = 0;
#endif
    // The render mode's --msaa, if given
    if (rendering.on() && rendering.msaa >= 0) {
        GLint most = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &most);
        samples = std::min(rendering.msaa, (int)most);
        if (samples != rendering.msaa)
            std::cout << "MSAA " << rendering.msaa << " not supported, "
                      << samples << " samples" << std::endl;
    }
    render::PostProcessing post(fbWidth, fbHeight, samples);
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
        // {"blur.fs", true, 2}, // Gaussian: horizontal, then vertical
        // {"inverse.fs", false},
        {"bloom.fs", true},
        // Distance fog, coloured by the sky behind (the skybox, unit 4)
        {"fog.fs", true},
        // The thin G-buffer in quarters: scene, normals, positions, distance
        {"gbuffer_debug.fs", false},
        // Last: everything before works on linear light (colour textures
        // are sRGB, decoded when sampled), this encodes it for the screen
        {"agx_tonemapper.fs", true},
        // {"gama.fs", true},
    };
    for (const Layer &layer : LAYERS)
        post.add(layer.shader, layer.on, layer.repeats);
    // Darkens the frame by the scene's fade (the demo's cuts): always on,
    // last, not a layer
    const GLuint fadePass = post.add("fade_transition.fs");
    // ssao.fs's sample kernel and noise
    const render::Ssao ssao(post);
    // fog.fs reads the skybox (Skybox::bindForReflections, unit 4) and its
    // tint, set each frame
    const size_t fogLayer = post.find("fog.fs");
    if (fogLayer < post.passCount()) {
        glUseProgram(post.program(fogLayer));
        glUniform1i(glGetUniformLocation(post.program(fogLayer), "uSkybox"), 4);
        glUseProgram(0);
    }

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
    float skip = rendering.start;
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
    int frame = 0; // render mode: frames saved
    const int frames = (int)std::lround(rendering.seconds * rendering.fps);
    while (!glfwWindowShouldClose(window)) {
        profiler::beginFrame();

        // Handle window resize (the render mode keeps its own size)
        int curW, curH;
        glfwGetFramebufferSize(window, &curW, &curH);
        if (!rendering.on() && curW > 0 && curH > 0 &&
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
        // Render mode: the video's time, however long the frame took
        if (rendering.on())
            dt = 1.f / (float)rendering.fps;
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
            post.resolveDepth();
            scene.drawTransparent();
            if (auto camera = scene.activeCamera.lock())
                post.setCamera(camera->projection,
                               camera->node.lock()->globalTransform());
            if (fogLayer < post.passCount()) {
                // No skybox: no fog (a tint of 0 marks it)
                glm::vec3 tint(0.f);
                if (auto sky = scene.skybox.lock()) {
                    sky->bindForReflections();
                    tint = sky->tint;
                }
                glUseProgram(post.program(fogLayer));
                glUniform3f(
                    glGetUniformLocation(post.program(fogLayer), "uSkyTint"),
                    tint.r, tint.g, tint.b);
            }
            glUseProgram(fadePass);
            glUniform1f(glGetUniformLocation(fadePass, "transition"),
                        *scene.fade);
            post.end();
        }

        if (rendering.on()) {
            char name[16];
            std::snprintf(name, sizeof name, "%05d.png", frame++);
            saveFrame(post, rendering.dir + "/" + name);
            if (frame % rendering.fps == 0)
                std::cout << "Rendered " << frame / rendering.fps << " s ("
                          << frame << " frames)" << std::endl;
            if (frames > 0 && frame >= frames)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
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

        if (!rendering.on())
            debug_ui::draw(scenes);

        profiler::endFrame();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    input::unsubscribe(keys);
}

int main(int argc, char **argv) {
    const RenderMode rendering = parseArgs(argc, argv);
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
    // Vsync, physics has its own fixed timestep. Not when rendering to
    // files: as fast as it goes.
    glfwSwapInterval(rendering.on() ? 0 : 1);

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
    run(window, fbWidth, fbHeight, rendering);

    profiler::shutdown();
    debug_ui::shutdown();

    glfwTerminate();
    return 0;
}
