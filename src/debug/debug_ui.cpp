#include "debug_ui.hpp"

#ifdef WITH_IMGUI
#include "../scene/scene.hpp"
#include "../scenes/scene_manager.hpp"
#include "profiler.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#endif

namespace debug_ui {

#ifdef WITH_IMGUI

void init(GLFWwindow *window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

// Scene graph inspector
static void sceneWindow(scn::Scene &scene) {
    ImGui::Begin("Scene");
    ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
    ImGui::Separator();
    ImGui::TextUnformatted(scene.root->to_string().c_str());
    ImGui::End();
}

// Scene picker/reload and physics controls
static void physicsWindow(scenes::SceneManager &scenes) {
    scn::Scene &scene = scenes.scene();
    ImGui::Begin("Physics");

    const auto &entries = scenes.entries();
    if (ImGui::BeginCombo("Scene",
                          entries[scenes.currentIndex()].name.c_str())) {
        for (std::size_t i = 0; i < entries.size(); i++) {
            bool selected = i == scenes.currentIndex();
            if (ImGui::Selectable(entries[i].name.c_str(), selected))
                scenes.requestLoad(i);
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Reload scene"))
        scenes.requestReload();

    ImGui::Separator();
    ImGui::SliderFloat("Timescale", &scene.physics_context.timeScale, 0.f,
                       2.f);
    if (ImGui::Button("Reset timescale"))
        scene.physics_context.timeScale = 1.f;
    ImGui::End();
}

void draw(scenes::SceneManager &scenes) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    sceneWindow(scenes.scene());
    physicsWindow(scenes);
    profiler::drawWindow();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

#else

void init(GLFWwindow *) {}
void draw(scenes::SceneManager &) {}
void shutdown() {}

#endif

} // namespace debug_ui
