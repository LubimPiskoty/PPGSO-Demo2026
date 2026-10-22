#ifdef WITH_IMGUI
// GLEW has to come before anything that pulls in GL/gl.h (GLFW)
#include <GL/glew.h>
#endif
#include "debug_ui.hpp"

#ifdef WITH_IMGUI
#include "../components/UserMovement.hpp"
#include "../components/camera.hpp"
#include "../scene/scene.hpp"
#include "../scenes/scene_manager.hpp"
#include "profiler.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <algorithm>
#include <cstdio>
#endif

namespace debug_ui {

#ifdef WITH_IMGUI

void init(GLFWwindow *window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

static unsigned int depthPreviewTex = 0;
static int depthPreviewW = 0, depthPreviewH = 0;
static bool depthPreviewPerspective = false;

// The depth texture is turned into a grayscale image by a small pass into
// vizTex, which ImGui then shows. Created on first use.
static GLuint vizProgram = 0, vizVao = 0, vizFbo = 0, vizTex = 0;
static int vizW = 0, vizH = 0;

static const char *kVizVert = R"(#version 330 core
out vec2 uv;
void main() {
    // One triangle covering the screen
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

static const char *kVizFrag = R"(#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform bool uPerspective;
uniform float uNear;
uniform float uFar;
uniform float uRange; // distance that maps to white
void main() {
    float d = texture(uDepth, uv).r;
    float v = d;
    if (uPerspective) {
        // Back to view-space distance, then scale into the visible range
        float ndc = d * 2.0 - 1.0;
        float dist = 2.0 * uNear * uFar / (uFar + uNear - ndc * (uFar - uNear));
        v = dist / uRange;
    }
    FragColor = vec4(vec3(clamp(v, 0.0, 1.0)), 1.0);
})";

static GLuint compileViz(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Error] depth preview shader: %s\n", log);
    }
    return shader;
}

static void initViz() {
    GLuint vs = compileViz(GL_VERTEX_SHADER, kVizVert);
    GLuint fs = compileViz(GL_FRAGMENT_SHADER, kVizFrag);
    vizProgram = glCreateProgram();
    glAttachShader(vizProgram, vs);
    glAttachShader(vizProgram, fs);
    glLinkProgram(vizProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGenVertexArrays(1, &vizVao);
    glGenFramebuffers(1, &vizFbo);
}

// (Re)creates the output image when the depth texture's size changes
static void resizeViz() {
    if (vizW == depthPreviewW && vizH == depthPreviewH)
        return;
    if (vizTex == 0)
        glGenTextures(1, &vizTex);
    glBindTexture(GL_TEXTURE_2D, vizTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, depthPreviewW, depthPreviewH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, vizFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           vizTex, 0);
    vizW = depthPreviewW;
    vizH = depthPreviewH;
}

// Draws the depth texture into vizTex, leaving the GL state as it found it
static void renderViz(float nearPlane, float farPlane, float range) {
    if (vizProgram == 0)
        initViz();

    GLint viewport[4], fbo, program, vao, activeTex, tex;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    const bool depthTest = glIsEnabled(GL_DEPTH_TEST);
    const bool cull = glIsEnabled(GL_CULL_FACE);
    const bool blend = glIsEnabled(GL_BLEND);

    resizeViz();
    glBindFramebuffer(GL_FRAMEBUFFER, vizFbo);
    glViewport(0, 0, depthPreviewW, depthPreviewH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glUseProgram(vizProgram);
    glBindTexture(GL_TEXTURE_2D, depthPreviewTex);
    glUniform1i(glGetUniformLocation(vizProgram, "uDepth"), 0);
    glUniform1i(glGetUniformLocation(vizProgram, "uPerspective"),
                depthPreviewPerspective);
    glUniform1f(glGetUniformLocation(vizProgram, "uNear"), nearPlane);
    glUniform1f(glGetUniformLocation(vizProgram, "uFar"), farPlane);
    glUniform1f(glGetUniformLocation(vizProgram, "uRange"), range);
    glBindVertexArray(vizVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(vao);
    glUseProgram(program);
    glBindTexture(GL_TEXTURE_2D, tex);
    glActiveTexture(activeTex);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (depthTest)
        glEnable(GL_DEPTH_TEST);
    if (cull)
        glEnable(GL_CULL_FACE);
    if (blend)
        glEnable(GL_BLEND);
}

// Shadow map of a chosen light, drawn as grayscale
static void lightPreviewWindow(scn::Scene &scene) {
    ImGui::Begin("Light preview");
    const auto &maps = scene.getShadowMaps();
    const auto &lights = scene.getShadowLights();
    if (maps.size() == 0) {
        ImGui::TextUnformatted("No light casts shadows");
        ImGui::End();
        return;
    }

    // Which light's map to show (maps can shrink when lights are removed)
    static int index = 0;
    index = std::min(index, (int)maps.size() - 1);
    if (maps.size() > 1)
        ImGui::SliderInt("Light", &index, 0, (int)maps.size() - 1);

    float nearPlane = 0.1f, farPlane = 100.f;
    if (index < (int)lights.size())
        if (auto light = lights[index].lock()) {
            nearPlane = light->shadowNear;
            farPlane = light->shadowFar;
            if (auto node = light->node.lock())
                ImGui::Text("%s", node->name.c_str());
        }

    const auto &map = maps.at(index);
    depthPreviewTex = map.getDepthTex();
    depthPreviewW = map.getWidth();
    depthPreviewH = map.getHeight();

    // Distance (in world units) that shows as white
    static float range = 50.f;
    ImGui::Checkbox("Perspective", &depthPreviewPerspective);
    if (depthPreviewPerspective) {
        ImGui::SliderFloat("Range", &range, nearPlane * 2.f, farPlane, "%.1f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::Text("near %.3f  far %.1f", nearPlane, farPlane);
    }
    ImGui::Text("%d x %d", depthPreviewW, depthPreviewH);

    renderViz(nearPlane, farPlane, range);

    // Fit the window width, keep the aspect ratio
    const float width = ImGui::GetContentRegionAvail().x;
    const float aspect =
        depthPreviewH > 0 ? (float)depthPreviewH / depthPreviewW : 1.f;
    // Flip V: GL textures start bottom-left, ImGui top-left
    ImGui::Image((ImTextureID)(intptr_t)vizTex, ImVec2(width, width * aspect),
                 ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
}

// Scene graph inspector
static void sceneWindow(scn::Scene &scene) {
    ImGui::Begin("Scene");
    ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);

    // Controls for a user-movable camera (scenes with fixed cameras have
    // no UserMovement, so this section is hidden there)
    if (auto camera = scene.activeCamera.lock())
        if (auto camNode = camera->node.lock())
            if (auto movement =
                    camNode->get_component<component::UserMovement>()) {
                ImGui::SeparatorText("Camera");
                ImGui::SliderFloat("Speed", &movement->speed, 1.f, 100.f,
                                   "%.1f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Sensitivity", &movement->sensitivity,
                                   0.01f, 1.f, "%.2f",
                                   ImGuiSliderFlags_Logarithmic);
            }

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
    lightPreviewWindow(scenes.scene());

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
