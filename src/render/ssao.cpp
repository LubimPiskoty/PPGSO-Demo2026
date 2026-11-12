#include "ssao.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>
#include <vector>

namespace render {

Ssao::Ssao(PostProcessing &post) {
    const size_t pass = post.find("ssao.fs");
    if (pass == post.passCount())
        return;

    std::mt19937 rng(11);
    std::uniform_real_distribution<float> random(0.f, 1.f);

    // The kernel: points in the hemisphere around +z (tangent space, the
    // normal along z), at random lengths, pulled in toward the middle so
    // what's close counts more
    std::vector<glm::vec3> kernel;
    for (int i = 0; i < KERNEL_SIZE; i++) {
        glm::vec3 sample(random(rng) * 2.f - 1.f, random(rng) * 2.f - 1.f,
                         random(rng));
        sample = glm::normalize(sample) * random(rng);
        float scale = (float)i / KERNEL_SIZE;
        scale = glm::mix(0.1f, 1.f, scale * scale);
        kernel.push_back(sample * scale);
    }
    const GLuint program = post.program(pass);
    glUseProgram(program);
    glUniform3fv(glGetUniformLocation(program, "uSamples"), KERNEL_SIZE,
                 glm::value_ptr(kernel[0]));
    glUseProgram(0);

    // The noise: 4x4 random turns about the normal (z = 0), repeating over
    // the screen, so neighbouring pixels sample differently; the blur in
    // ssao.fs evens it out
    std::vector<glm::vec3> turns;
    for (int i = 0; i < 16; i++)
        turns.emplace_back(random(rng) * 2.f - 1.f, random(rng) * 2.f - 1.f,
                           0.f);
    glGenTextures(1, &noise);
    glBindTexture(GL_TEXTURE_2D, noise);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 4, 4, 0, GL_RGB, GL_FLOAT,
                 turns.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    post.addTexture(pass, "uNoise", noise);
}

Ssao::~Ssao() { glDeleteTextures(1, &noise); }

} // namespace render
