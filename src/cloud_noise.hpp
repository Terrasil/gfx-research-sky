#pragma once

#include <array>
#include <filesystem>
#include <gfx/research/shader.hpp>
#include <glad/gl.h>

class CloudNoise {
public:
    ~CloudNoise() { if (textures_[0]) glDeleteTextures(2, textures_.data()); }
    bool create(const std::filesystem::path& shaderPath) {
        const gfx::research::ShaderFile file{gfx::research::ShaderStage::Compute, shaderPath};
        gfx::research::Shader generator;
        if (!generator.load(std::span{&file, 1})) return false;
        glCreateTextures(GL_TEXTURE_3D, 2, textures_.data());
        for (int i=0; i<2; ++i) {
            glTextureStorage3D(textures_[i], 1, GL_RG8, 128, 128, 128);
            glTextureParameteri(textures_[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(textures_[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(textures_[i], GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTextureParameteri(textures_[i], GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTextureParameteri(textures_[i], GL_TEXTURE_WRAP_R, GL_REPEAT);
            glBindImageTexture(i, textures_[i], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RG8);
        }
        generator.bind();
        glDispatchCompute(16, 16, 16);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        // Finish one-time resource generation before any benchmark timestamps.
        glFinish();
        glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG8);
        glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG8);
        return true;
    }
    void bind() const { glBindTextureUnit(4, textures_[0]); glBindTextureUnit(5, textures_[1]); }
private:
    std::array<GLuint, 2> textures_{};
};
