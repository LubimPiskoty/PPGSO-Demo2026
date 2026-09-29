#include "profiler.hpp"

#ifdef WITH_IMGUI
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>

#include "imgui.h"
#endif

namespace profiler {

#ifdef WITH_IMGUI

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kStages = static_cast<int>(Stage::Count);
constexpr const char *kStageNames[kStages] = {"Update", "Scene", "UI"};

// How many frames of GPU queries are in flight before we read one back
constexpr int kLatency = 4;
// Samples kept for averages and the frame time graph
constexpr int kHistory = 240;
// Averages shown as text use only the most recent samples so they react
// quickly but don't flicker every frame
constexpr int kAverageOver = 60;

// Ring buffer of timings in milliseconds
struct History {
    std::array<float, kHistory> values{};
    int next = 0;
    int count = 0;

    void push(float ms) {
        values[next] = ms;
        next = (next + 1) % kHistory;
        count = std::min(count + 1, kHistory);
    }

    float average() const {
        int n = std::min(count, kAverageOver);
        if (n == 0)
            return 0.f;
        float sum = 0.f;
        for (int i = 1; i <= n; i++)
            sum += values[(next - i + kHistory) % kHistory];
        return sum / n;
    }

    float max() const {
        int n = std::min(count, kAverageOver);
        float m = 0.f;
        for (int i = 1; i <= n; i++)
            m = std::max(m, values[(next - i + kHistory) % kHistory]);
        return m;
    }
};

// One frame's worth of timestamp queries: frame begin/end + each stage's
// begin/end
struct QuerySet {
    GLuint frame[2] = {};
    GLuint stage[kStages][2] = {};
    bool stageUsed[kStages] = {};
    bool issued = false;
};

bool gpuSupported = false;
std::array<QuerySet, kLatency> queries;
int current = 0;

Clock::time_point frameStart;
Clock::time_point lastFrameStart;
bool haveLastFrame = false;
Clock::time_point stageStart[kStages];
float stageCpuMs[kStages] = {};

History frameTime; // wall time between frames, includes vsync wait
History cpuFrame;  // time the CPU spent between beginFrame and endFrame
History cpuStage[kStages];
History gpuFrame;
History gpuStage[kStages];

bool vsync = true;

float msSince(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start)
        .count();
}

float nsToMs(GLuint64 begin, GLuint64 end) {
    return static_cast<float>(end - begin) / 1e6f;
}

// Read back the oldest query set if the GPU is done with it. If it's still
// not ready after kLatency frames we just drop that sample rather than stall.
void collect(QuerySet &set) {
    if (!set.issued)
        return;
    set.issued = false;

    GLint available = 0;
    glGetQueryObjectiv(set.frame[1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available)
        return;

    GLuint64 begin, end;
    glGetQueryObjectui64v(set.frame[0], GL_QUERY_RESULT, &begin);
    glGetQueryObjectui64v(set.frame[1], GL_QUERY_RESULT, &end);
    gpuFrame.push(nsToMs(begin, end));

    for (int s = 0; s < kStages; s++) {
        if (!set.stageUsed[s])
            continue;
        glGetQueryObjectui64v(set.stage[s][0], GL_QUERY_RESULT, &begin);
        glGetQueryObjectui64v(set.stage[s][1], GL_QUERY_RESULT, &end);
        gpuStage[s].push(nsToMs(begin, end));
    }
}

void timingRow(const char *name, const History &cpu, const History *gpu) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(name);
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", cpu.average());
    ImGui::TableNextColumn();
    ImGui::Text("%.2f", cpu.max());
    ImGui::TableNextColumn();
    if (gpu && gpu->count > 0)
        ImGui::Text("%.2f", gpu->average());
    else
        ImGui::TextDisabled("-");
    ImGui::TableNextColumn();
    if (gpu && gpu->count > 0)
        ImGui::Text("%.2f", gpu->max());
    else
        ImGui::TextDisabled("-");
}

} // namespace

void init() {
    // Timestamp queries are core since GL 3.3, but check the loaded pointer
    // anyway since glewInit can partially fail under Wayland
    gpuSupported = glQueryCounter != nullptr;
    if (gpuSupported) {
        for (auto &set : queries) {
            glGenQueries(2, set.frame);
            glGenQueries(kStages * 2, &set.stage[0][0]);
        }
    }
    glfwSwapInterval(vsync ? 1 : 0);
}

void shutdown() {
    if (!gpuSupported)
        return;
    for (auto &set : queries) {
        glDeleteQueries(2, set.frame);
        glDeleteQueries(kStages * 2, &set.stage[0][0]);
    }
}

void beginFrame() {
    frameStart = Clock::now();
    if (haveLastFrame)
        frameTime.push(std::chrono::duration<float, std::milli>(
                           frameStart - lastFrameStart)
                           .count());
    lastFrameStart = frameStart;
    haveLastFrame = true;

    for (int s = 0; s < kStages; s++)
        stageCpuMs[s] = -1.f;

    if (gpuSupported) {
        QuerySet &set = queries[current];
        collect(set);
        std::fill(std::begin(set.stageUsed), std::end(set.stageUsed), false);
        glQueryCounter(set.frame[0], GL_TIMESTAMP);
    }
}

void endFrame() {
    cpuFrame.push(msSince(frameStart));
    for (int s = 0; s < kStages; s++)
        if (stageCpuMs[s] >= 0.f)
            cpuStage[s].push(stageCpuMs[s]);

    if (gpuSupported) {
        QuerySet &set = queries[current];
        glQueryCounter(set.frame[1], GL_TIMESTAMP);
        set.issued = true;
        current = (current + 1) % kLatency;
    }
}

void begin(Stage stage) {
    int s = static_cast<int>(stage);
    stageStart[s] = Clock::now();
    if (gpuSupported)
        glQueryCounter(queries[current].stage[s][0], GL_TIMESTAMP);
}

void end(Stage stage) {
    int s = static_cast<int>(stage);
    stageCpuMs[s] = msSince(stageStart[s]);
    if (gpuSupported) {
        QuerySet &set = queries[current];
        glQueryCounter(set.stage[s][1], GL_TIMESTAMP);
        set.stageUsed[s] = true;
    }
}

void drawWindow() {
    ImGui::Begin("Profiler");

    float frameMs = frameTime.average();
    ImGui::Text("Frame: %.2f ms (%.0f fps)", frameMs,
                frameMs > 0.f ? 1000.f / frameMs : 0.f);
    if (ImGui::Checkbox("VSync", &vsync))
        glfwSwapInterval(vsync ? 1 : 0);

    // Frame time graph, scaled so a 60 fps frame sits in the middle
    float graphMax = std::max(33.3f, frameTime.max() * 1.1f);
    char overlay[32];
    snprintf(overlay, sizeof(overlay), "max %.2f ms", frameTime.max());
    ImGui::PlotLines("##frametime", frameTime.values.data(), frameTime.count,
                     frameTime.count == kHistory ? frameTime.next : 0, overlay,
                     0.f, graphMax, ImVec2(-1.f, 60.f));

    ImGui::SeparatorText("Timings (ms)");
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("timings", 5, flags)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("CPU avg");
        ImGui::TableSetupColumn("CPU max");
        ImGui::TableSetupColumn("GPU avg");
        ImGui::TableSetupColumn("GPU max");
        ImGui::TableHeadersRow();

        for (int s = 0; s < kStages; s++)
            timingRow(kStageNames[s], cpuStage[s], &gpuStage[s]);
        timingRow("Total", cpuFrame, &gpuFrame);

        ImGui::EndTable();
    }

    if (!gpuSupported)
        ImGui::TextDisabled("GPU timer queries not available");
    ImGui::TextDisabled("avg/max over last %d frames", kAverageOver);

    ImGui::End();
}

#else

void init() {}
void shutdown() {}
void beginFrame() {}
void endFrame() {}
void begin(Stage) {}
void end(Stage) {}
void drawWindow() {}

#endif

} // namespace profiler
