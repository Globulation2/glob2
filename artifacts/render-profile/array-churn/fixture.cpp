#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <GraphicContext.h>
#include <RenderBatch.h>
#include <Toolkit.h>
#include <array>
#include <cstdio>
#include <chrono>
#include <OpenGL/glext.h>
#include <stdexcept>
#include <vector>
    static void profileArrayChurn(GAGCore::GraphicContext &gfx)
    {
        auto *batch = gfx.getRenderBatch();
        if (!batch) throw std::runtime_error("Array churn requires legacy OpenGL");
        constexpr int side = 256;
        std::array<GLuint, 16> textures{};
        std::vector<Uint8> pixels(side * side * 4, 255);
        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        glGenTextures(textures.size(), textures.data());
        for (int cycle = 0; cycle <= 20; ++cycle)
        {
            unsigned promoted = 0;
            std::array<GAGCore::ArrayView, 16> views{};
            const auto start = std::chrono::steady_clock::now();
            for (const auto &texture : textures)
            {
                batch->textureChanged(texture);
                GLint previous = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
                glBindTexture(GL_TEXTURE_2D, texture);
                if (cycle == 0)
                {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                }
                else
                {
                    pixels.front() = Uint8(cycle);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                }
                glBindTexture(GL_TEXTURE_2D, previous);
                views[&texture - textures.data()] = batch->pack(texture);
                promoted += views[&texture - textures.data()].texture != 0;
            }
            std::printf("GL_ARRAY_CHURN cycle=%d promoted=%u sources=%zu\n", cycle, promoted, textures.size());
            glFinish();
            const auto stop = std::chrono::steady_clock::now();
            std::printf("CHURN_TIME cycle=%d ms=%.3f\n", cycle,
                std::chrono::duration<double, std::milli>(stop - start).count());
            GLint previousFramebuffer = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            for (std::size_t i = 0; i < views.size(); ++i)
            {
                if (views[i].texture)
                    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, views[i].texture, 0, int(views[i].layer));
                else
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    throw std::runtime_error("Array readback framebuffer incomplete");
                std::array<Uint8, 4> rgba{};
                glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                if (rgba != std::array<Uint8, 4>{Uint8(cycle ? cycle : 255),255,255,255})
                    throw std::runtime_error("Array churn changed source pixels");
            }
            glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
            batch->stats();
        }
        for (const auto texture : textures) batch->textureChanged(texture);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(textures.size(), textures.data());
    }
int main(){
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    GAGCore::Toolkit::init("Glob2");
    GAGCore::GraphicContext::setInitialRendererBackend(GAGCore::RendererBackend::LegacyGL);
    auto *gfx=GAGCore::Toolkit::initGraphic(320,240,GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::NOAUDIO,"Array churn");
    profileArrayChurn(*gfx);
    GAGCore::Toolkit::close();
}
