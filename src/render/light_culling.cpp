#include "light_culling.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace render {

namespace {
// Start of the lists' buffer, as TileLights in forward_plus.comp and lit.fs
struct TileHeader {
    glm::ivec4 info; // tiles across, tile size, LIGHTS_PER_TILE,
                     // where the transparent lists start
};
} // namespace

LightCulling::LightCulling()
    : shader(std::make_unique<ComputeShader>("forward_plus.comp")) {}

void LightCulling::run(GLuint depthTexture, int width, int height,
                       const glm::mat4 &view, const glm::mat4 &projection) {
    const int tileSize = shader->QueryLinearWorkGroupSize();
    const glm::ivec2 count((width + tileSize - 1) / tileSize,
                           (height + tileSize - 1) / tileSize);
    // Per tile two lists: a count and LIGHTS_PER_TILE indices each
    const int listInts = count.x * count.y * (LIGHTS_PER_TILE + 1);
    if (count.x * count.y != tileCount) {
        tileCount = count.x * count.y;
        tiles.setData(sizeof(TileHeader) + 2 * (size_t)listInts * sizeof(int),
                      nullptr);
    }
    const TileHeader header{{count.x, tileSize, LIGHTS_PER_TILE, listInts}};
    tiles.bind();
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(header), &header);
    tiles.bindSSBO(TILE_SSBO_BINDING);

    // The planes from a perspective projection's matrix
    const float near = projection[3][2] / (projection[2][2] - 1.f);
    const float far = projection[3][2] / (projection[2][2] + 1.f);
    const glm::mat4 invProjection = glm::inverse(projection);

    shader->bind();
    const GLuint p = shader->program;
    // Unit 8: the same texture resolveDepth binds there as uSceneDepth
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(p, "uDepth"), 8);
    glUniformMatrix4fv(glGetUniformLocation(p, "uView"), 1, GL_FALSE,
                       glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(p, "uInvProjection"), 1, GL_FALSE,
                       glm::value_ptr(invProjection));
    glUniform2i(glGetUniformLocation(p, "uScreenSize"), width, height);
    glUniform1f(glGetUniformLocation(p, "uNear"), near);
    glUniform1f(glGetUniformLocation(p, "uFar"), far);

    glDispatchCompute(count.x, count.y, 1);
    // lit.fs reads the lists next
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

} // namespace render
