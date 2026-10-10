// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ScopedEnvironment.h"
#include "GraphicContextPrivate.h"
#include <AssetLoader.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <chrono>

using namespace GAGCore;
namespace {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
Surface pixels(int w, int h, int alphaMode)
{
    Surface result(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    REQUIRE(result != nullptr);
    Uint32 random = 42;
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        reinterpret_cast<Uint32*>(static_cast<Uint8*>(result->pixels)+y*result->pitch)[x] =
            alphaMode == 0 ? random & 0xffffff : alphaMode == 1 ? random | 0xff000000 : random;
    }
    return result;
}
struct TestWindow {
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    explicit TestWindow(bool gl) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("GPU texture preparation test", 256, 256,
            SDL_WINDOW_HIDDEN | (gl ? SDL_WINDOW_OPENGL : 0));
        REQUIRE(window != nullptr);
        if (gl) { context = SDL_GL_CreateContext(window); REQUIRE(context != nullptr); }
    }
    ~TestWindow() {
        if (context) { releaseNativeMipmaps(context); SDL_GL_DestroyContext(context); }
        SDL_DestroyWindow(window); SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};
void portablePreparation(bool cpu)
{
    glob2test::ScopedEnvironment disable("GLOB2_DISABLE_GPU_TEXTURE_PREPARATION", cpu ? "1" : "");
    // getenv presence enables the override; an empty value must also work as
    // an unset override so one fixture can exercise both paths.
    TestWindow window(false);
    auto backend = makeSDLRenderBackend(window.window, 256, 256);
    auto *renderer = SDL_GetRenderer(window.window);
    REQUIRE(renderer != nullptr);
    auto *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, 256, 256);
    REQUIRE(target != nullptr);
    REQUIRE(SDL_SetRenderTarget(renderer, target));
    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0));
    REQUIRE(SDL_RenderClear(renderer));
    auto source = pixels(256, 256, 2);
    for (int a=0; a<256; ++a) for (int c=0; c<256; ++c)
        reinterpret_cast<Uint32*>(static_cast<Uint8*>(source->pixels)+a*source->pitch)[c] =
            (Uint32(a)<<24) | (Uint32(c)<<16) | (Uint32(255-c)<<8) | Uint32(c);
    SDL_Rect clip{2,3,7,11};
    REQUIRE(SDL_SetRenderClipRect(renderer, &clip));
    REQUIRE(SDL_SetRenderScale(renderer, 1.25f, 1.5f));
    REQUIRE(SDL_SetRenderDrawColor(renderer, 17, 31, 43, 71));
    backend->prepareTexture(source.get(), source.get(), 1, true);
    CHECK(SDL_GetRenderTarget(renderer) == target);
    SDL_Rect actual; REQUIRE(SDL_GetRenderClipRect(renderer, &actual));
    CHECK(std::memcmp(&clip, &actual, sizeof(clip)) == 0);
    float sx, sy; REQUIRE(SDL_GetRenderScale(renderer, &sx, &sy)); CHECK(sx == 1.25f); CHECK(sy == 1.5f);
    Uint8 r,g,b,a; REQUIRE(SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a));
    CHECK(r == 17); CHECK(g == 31); CHECK(b == 43); CHECK(a == 71);
    REQUIRE(SDL_SetRenderClipRect(renderer, nullptr)); REQUIRE(SDL_SetRenderScale(renderer, 1, 1));
    const SDL_Rect src{0,0,256,256}; const SDL_FRect dst{0,0,256,256};
    backend->blitLinear(source.get(),source.get(),1,src,dst,255);
    Surface output(backend->capture(),SDL_DestroySurface); REQUIRE(output != nullptr);
    for (int alpha=0; alpha<256; ++alpha) for (int channel=0; channel<256; ++channel) {
        Uint32 value; std::memcpy(&value,static_cast<Uint8*>(output->pixels)+alpha*output->pitch+channel*4,4);
        SDL_GetRGBA(value,SDL_GetPixelFormatDetails(output->format),nullptr,&r,&g,&b,&a);
        if (r != (channel*alpha+127)/255 || g != ((255-channel)*alpha+127)/255 ||
            b != (channel*alpha+127)/255 || a != alpha) {
            FAIL("Premultiplication changed a channel/alpha combination"); break;
        }
    }
    // Queued draws must see the old revision even when the same texture is updated.
    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0)); REQUIRE(SDL_RenderClear(renderer));
    SDL_FRect left{0,0,128,256}, right{128,0,128,256};
    backend->blitLinear(source.get(),source.get(),1,src,left,255);
    REQUIRE(SDL_FillSurfaceRect(source.get(),nullptr,0xff00ff00));
    backend->blitLinear(source.get(),source.get(),2,src,right,255);
    output.reset(backend->capture()); REQUIRE(output != nullptr);
    Uint32 value; std::memcpy(&value,static_cast<Uint8*>(output->pixels)+128*output->pitch+192*4,4);
    SDL_GetRGBA(value,SDL_GetPixelFormatDetails(output->format),nullptr,&r,&g,&b,&a);
    CHECK(r == 0); CHECK(g == 255); CHECK(b == 0); CHECK(a == 255);
    std::memcpy(&value,static_cast<Uint8*>(output->pixels)+128*output->pitch+64*4,4);
    SDL_GetRGBA(value,SDL_GetPixelFormatDetails(output->format),nullptr,&r,&g,&b,&a);
    CHECK(r > 0); CHECK(g < 255); CHECK(a == 128);
    backend->reset(); backend->prepareTexture(source.get(),source.get(),2,true);
    REQUIRE(SDL_SetRenderTarget(renderer,nullptr)); SDL_DestroyTexture(target);
}
}
TEST_SUITE("GpuTexturePreparation") {
TEST_CASE("SDL premultiplication preserves pixels state and queued revisions [display]") { portablePreparation(false); }
TEST_CASE("SDL CPU fallback preserves pixels state and queued revisions [display]") { portablePreparation(true); }
TEST_CASE("SDL linear upload timing [display][benchmark]") {
    std::printf("method,size,mean_prepare_ms,mean_complete_ms,iterations\n");
    for (int size : {128,256,512,1024}) for (bool gpu : {false,true}) {
        glob2test::ScopedEnvironment disable("GLOB2_DISABLE_GPU_TEXTURE_PREPARATION",gpu?"":"1");
        TestWindow window(false);
        auto backend=makeSDLRenderBackend(window.window,256,256);
        auto source=pixels(size,size,2);
        double submit=0, complete=0;
        constexpr int iterations=32;
        for(int n=-4;n<iterations;++n) {
            const auto start=std::chrono::steady_clock::now();
            backend->prepareTexture(source.get(),source.get(),std::uint64_t(n+5),true);
            const auto queued=std::chrono::steady_clock::now();
            // Reading a rendered pixel includes command execution, rather than
            // comparing only asynchronous submission with CPU work.
            const SDL_Rect src{0,0,size,size}; const SDL_FRect dst{0,0,256,256};
            backend->blitLinear(source.get(),source.get(),std::uint64_t(n+5),src,dst,255);
            Surface capture(backend->capture(),SDL_DestroySurface); REQUIRE(capture != nullptr);
            const auto done=std::chrono::steady_clock::now();
            if(n>=0) {
                submit+=std::chrono::duration<double,std::milli>(queued-start).count();
                complete+=std::chrono::duration<double,std::milli>(done-start).count();
            }
        }
        std::printf("%s,%d,%.4f,%.4f,%d\n",gpu?"gpu":"cpu",size,submit/iterations,complete/iterations,iterations);
    }
}
#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
TEST_CASE("native alpha weighted mips match every CPU level and preserve GL state [display]") {
    TestWindow window(true);
    if (std::atoi(reinterpret_cast<const char*>(glGetString(GL_VERSION))) < 3) {
        auto source=pixels(4,4,2); GLuint texture; glGenTextures(1,&texture);
        CHECK_FALSE(prepareNativeMipmaps(texture,source.get())); glDeleteTextures(1,&texture); return;
    }
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    GLuint texture, other, pbo; glGenTextures(1,&texture); glGenTextures(1,&other); glGenBuffers(1,&pbo);
    for (auto size : {std::pair{1,1},{1,256},{256,1},{256,256},{1024,512}}) for (int mode : {0,1,2}) {
        auto source=pixels(size.first,size.second,mode);
        AssetImage reference(SDL_DuplicateSurface(source.get())); reference.prepareUpload(true);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glBindTexture(GL_TEXTURE_2D,other); glActiveTexture(GL_TEXTURE1);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo); glBufferData(GL_PIXEL_UNPACK_BUFFER,64,nullptr,GL_STREAM_DRAW);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,13); glPixelStorei(GL_UNPACK_SKIP_ROWS,2);
        glViewport(3,7,23,31); glEnable(GL_SCISSOR_TEST); glScissor(9,11,2,3);
        glEnable(GL_BLEND); glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
        REQUIRE(prepareNativeMipmaps(texture,source.get()));
        GLint value, viewport[4]; glGetIntegerv(GL_ACTIVE_TEXTURE,&value); CHECK(value == GL_TEXTURE1);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value); CHECK(value == GLint(pbo));
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value); CHECK(value == 13);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&value); CHECK(value == 2);
        glGetIntegerv(GL_VIEWPORT,viewport); CHECK(viewport[0] == 3); CHECK(viewport[1] == 7);
        CHECK(viewport[2] == 23); CHECK(viewport[3] == 31); CHECK(glIsEnabled(GL_SCISSOR_TEST)); CHECK(glIsEnabled(GL_BLEND));
        GLboolean mask[4]; glGetBooleanv(GL_COLOR_WRITEMASK,mask); CHECK_FALSE(mask[0]); CHECK(mask[1]); CHECK_FALSE(mask[2]); CHECK(mask[3]);
        glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&value); CHECK(value == GLint(other));
        glBindTexture(GL_TEXTURE_2D,texture);
        for (size_t level=0; level<reference.mips.size(); ++level) {
            const auto &mip=reference.mips[level]; std::vector<unsigned char> actual(mip.pixels.size());
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            glGetTexImage(GL_TEXTURE_2D,int(level),GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
#else
            glGetTexImage(GL_TEXTURE_2D,int(level),GL_BGRA,GL_UNSIGNED_BYTE,actual.data());
#endif
            CHECK(actual == mip.pixels);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glDeleteBuffers(1,&pbo); glDeleteTextures(1,&texture); glDeleteTextures(1,&other);
}
TEST_CASE("native mip fallback preserves deletion pending programs and separate framebuffers [display]") {
    TestWindow window(true);
    auto source=pixels(4,4,2);
    GLuint texture; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    if (!prepareNativeMipmaps(texture,source.get())) { glDeleteTextures(1,&texture); return; }
    const char *text="#version 130\nvoid main(){gl_Position=gl_Vertex;}";
    GLuint shader=glCreateShader(GL_VERTEX_SHADER); glShaderSource(shader,1,&text,nullptr); glCompileShader(shader);
    GLuint program=glCreateProgram(); glAttachShader(program,shader); glLinkProgram(program);
    GLint linked=0; glGetProgramiv(program,GL_LINK_STATUS,&linked); REQUIRE(linked);
    glUseProgram(program); glDeleteProgram(program); glDeleteShader(shader);
    CHECK_FALSE(prepareNativeMipmaps(texture,source.get()));
    GLint current=0; glGetIntegerv(GL_CURRENT_PROGRAM,&current); CHECK(current == GLint(program));
    glUseProgram(0); CHECK_FALSE(glIsProgram(program));
    GLuint framebuffers[2]; glGenFramebuffersEXT(2,framebuffers);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER,framebuffers[0]); glDrawBuffer(GL_NONE);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER,framebuffers[1]); glReadBuffer(GL_NONE);
    REQUIRE(prepareNativeMipmaps(texture,source.get()));
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&current); CHECK(current == GLint(framebuffers[0]));
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&current); CHECK(current == GLint(framebuffers[1]));
    glGetIntegerv(GL_DRAW_BUFFER,&current); CHECK(current == GL_NONE);
    glGetIntegerv(GL_READ_BUFFER,&current); CHECK(current == GL_NONE);
    glBindFramebufferEXT(GL_FRAMEBUFFER,0); glDeleteFramebuffersEXT(2,framebuffers);
    { glob2test::ScopedEnvironment disable("GLOB2_DISABLE_GPU_TEXTURE_PREPARATION","1");
      CHECK_FALSE(prepareNativeMipmaps(texture,source.get())); }
    auto odd=pixels(7,5,2); CHECK_FALSE(prepareNativeMipmaps(texture,odd.get()));
    CHECK(glGetError() == GL_NO_ERROR); glDeleteTextures(1,&texture);
}
TEST_CASE("native mip upload timing [display][benchmark]") {
    TestWindow window(true);
    GLuint texture; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    auto warm=pixels(4,4,2);
    if (!prepareNativeMipmaps(texture,warm.get())) { glDeleteTextures(1,&texture); return; }
    std::printf("method,size,mean_submit_ms,mean_complete_ms,iterations\n");
    for (int size : {128,256,512,1024}) for (bool gpu : {false,true}) {
        auto source=pixels(size,size,2);
        double submit=0, complete=0;
        constexpr int iterations=32;
        for (int n=-4; n<iterations; ++n) {
            glFinish(); const auto start=std::chrono::steady_clock::now();
            if (gpu) REQUIRE(prepareNativeMipmaps(texture,source.get()));
            else {
                AssetImage image(SDL_DuplicateSurface(source.get())); image.prepareUpload(true);
                for (size_t level=0; level<image.mips.size(); ++level) {
                    const auto &mip=image.mips[level];
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                    const GLenum format=GL_RGBA;
#else
                    const GLenum format=GL_BGRA;
#endif
                    glTexImage2D(GL_TEXTURE_2D,int(level),GL_RGBA8,mip.width,mip.height,0,format,GL_UNSIGNED_BYTE,mip.pixels.data());
                }
            }
            const auto queued=std::chrono::steady_clock::now(); glFinish();
            const auto done=std::chrono::steady_clock::now();
            if(n>=0) {
                submit+=std::chrono::duration<double,std::milli>(queued-start).count();
                complete+=std::chrono::duration<double,std::milli>(done-start).count();
            }
        }
        std::printf("%s,%d,%.4f,%.4f,%d\n",gpu?"gpu":"cpu",size,submit/iterations,complete/iterations,iterations);
    }
    glDeleteTextures(1,&texture);
}

#endif
}
