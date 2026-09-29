#pragma once

// CPU + GPU frame profiler feeding the ImGui debug overlay. Like debug_ui it
// only does anything in Debug builds (WITH_IMGUI), otherwise every function
// is a no-op, so callers don't need their own #ifdefs.
//
// GPU times come from GL timestamp queries. Results are read back a few
// frames late (ring of query sets) so the CPU never stalls waiting on them.
namespace profiler {

enum class Stage { Update, Scene, UI, Count };

// Call once after the GL context is current
void init();
void shutdown();

// Bracket the whole frame: beginFrame at the top of the main loop,
// endFrame right before glfwSwapBuffers
void beginFrame();
void endFrame();

// Bracket a stage inside the frame. Stages must not overlap.
void begin(Stage stage);
void end(Stage stage);

struct Scope {
    Stage stage;
    explicit Scope(Stage stage) : stage(stage) { begin(stage); }
    ~Scope() { end(stage); }
};

// Draws the "Profiler" ImGui window; call between ImGui::NewFrame/Render
void drawWindow();

} // namespace profiler
