#include "debug_ui.hpp"

#ifdef WITH_IMGUI
#include "../loaders/scene_loader.hpp"
#include "../scene/scene.hpp"
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

static void sceneWindow(scn::Scene &scene) {
    ImGui::Begin("Scene");
    ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
    ImGui::Separator();
    ImGui::TextUnformatted(scene.root->to_string().c_str());
    ImGui::Separator();
    if (ImGui::Button("Save scene"))
        loader::saveScene(&scene, "scene.json");
    ImGui::End();
}

void draw(scn::Scene &scene) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    sceneWindow(scene);

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
void draw(scn::Scene &) {}
void shutdown() {}

#endif

} // namespace debug_ui
