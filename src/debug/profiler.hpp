#pragma once

// CPU + GPU frame profiler feeding the ImGui debug overlay. Like debug_ui it
// only does anything in Debug builds (WITH_IMGUI), otherwise every function
// is a no-op, so callers don't need their own #ifdefs.
//
// GPU times come from GL timestamp queries. Results are read back a few
// frames late (ring of query sets) so the CPU never stalls waiting on them.
namespace profiler {

enum class Stage { Update, Physics, Scene, Count };

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

// ---- Physics breakdown (CPU only) ----
// The physics stage runs several fixed steps per frame, so phase times and
// counters are summed over the frame instead of measured once.
enum class PhysicsPhase { Integrate, Cache, Broad, Narrow, Solve, Count };
enum class PhysicsCounter { Steps, Bodies, Pairs, Contacts, Count };

void beginPhase(PhysicsPhase phase);
void endPhase(PhysicsPhase phase);
void addCount(PhysicsCounter counter, int value);

struct PhaseScope {
    PhysicsPhase phase;
    explicit PhaseScope(PhysicsPhase phase) : phase(phase) {
        beginPhase(phase);
    }
    ~PhaseScope() { endPhase(phase); }
};

// Draws the "Profiler" ImGui window; call between ImGui::NewFrame/Render
void drawWindow();

} // namespace profiler
