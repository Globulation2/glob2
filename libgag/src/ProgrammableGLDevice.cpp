// SPDX-License-Identifier: GPL-3.0-or-later
#include <ProgrammableGLDevice.h>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#if defined(HAVE_OPENGL) || defined(GLOB2_PROGRAMMABLE_GL) || defined(__EMSCRIPTEN__)
#include <RendererShaderLayouts.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <stdexcept>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The typed GLES entry point carries an i64 timeout. The pinned SDK's
// GetProcAddress returns a legacy four-i32 import for this particular function.
extern "C" GLenum APIENTRY glClientWaitSync(GLsync, GLbitfield, GLuint64);
#ifdef HAVE_OPENGL
#include <ApplicationHost.h>
#endif
// WebGL has getBufferSubData rather than mapped CPU buffers. Copy through an
// ordinary ArrayBuffer: WebGL implementations need not accept shared Wasm views.
EM_JS(void, copyProgrammableGLReadback, (void *destination, int bytes), {
    const data = new Uint8Array(bytes);
    GLctx.getBufferSubData(0x88EB, 0, data);
    HEAPU8.set(data, destination);
});
#endif

namespace GAGCore
{
namespace
{
#define GL_FUNCTIONS(F) \
 F(void, glGetIntegerv, (GLenum, GLint *)) \
 F(GLenum, glGetError, ()) \
 F(void, glGenTextures, (GLsizei, GLuint *)) \
 F(void, glDeleteTextures, (GLsizei, const GLuint *)) \
 F(void, glBindTexture, (GLenum, GLuint)) \
 F(void, glActiveTexture, (GLenum)) \
 F(void, glTexParameteri, (GLenum, GLenum, GLint)) \
 F(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
 F(void, glPixelStorei, (GLenum, GLint)) \
 F(void, glGenBuffers, (GLsizei, GLuint *)) \
 F(void, glDeleteBuffers, (GLsizei, const GLuint *)) \
 F(void, glBindBuffer, (GLenum, GLuint)) \
 F(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
 F(void, glGenVertexArrays, (GLsizei, GLuint *)) \
 F(void, glDeleteVertexArrays, (GLsizei, const GLuint *)) \
 F(void, glBindVertexArray, (GLuint)) \
 F(void, glEnableVertexAttribArray, (GLuint)) \
 F(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
 F(GLuint, glCreateShader, (GLenum)) \
 F(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
 F(void, glCompileShader, (GLuint)) \
 F(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
 F(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
 F(void, glDeleteShader, (GLuint)) \
 F(GLuint, glCreateProgram, ()) \
 F(void, glAttachShader, (GLuint, GLuint)) \
 F(void, glLinkProgram, (GLuint)) \
 F(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
 F(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
 F(void, glDeleteProgram, (GLuint)) \
 F(void, glUseProgram, (GLuint)) \
 F(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
 F(void, glUniform1i, (GLint, GLint)) \
 F(void, glUniform1f, (GLint, GLfloat)) \
 F(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
 F(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
 F(void, glUniform4ui, (GLint, GLuint, GLuint, GLuint, GLuint)) \
 F(void, glUniform4iv, (GLint, GLsizei, const GLint *)) \
 F(void, glGenFramebuffers, (GLsizei, GLuint *)) \
 F(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) \
 F(void, glBindFramebuffer, (GLenum, GLuint)) \
 F(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
 F(GLenum, glCheckFramebufferStatus, (GLenum)) \
 F(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
 F(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
 F(void, glScissor, (GLint, GLint, GLsizei, GLsizei)) \
 F(void, glEnable, (GLenum)) \
 F(void, glDisable, (GLenum)) \
 F(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
 F(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
 F(void, glClear, (GLbitfield)) \
 F(void, glDepthFunc, (GLenum)) \
 F(void, glDepthMask, (GLboolean)) \
 F(void, glCullFace, (GLenum)) \
 F(void, glFrontFace, (GLenum)) \
 F(void, glGenRenderbuffers, (GLsizei, GLuint *)) \
 F(void, glDeleteRenderbuffers, (GLsizei, const GLuint *)) \
 F(void, glBindRenderbuffer, (GLenum, GLuint)) \
 F(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
 F(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
 F(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
 F(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
 F(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *)) \
 F(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
 F(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
 F(GLsync, glFenceSync, (GLenum, GLbitfield)) \
 F(GLenum, glClientWaitSync, (GLsync, GLbitfield, GLuint64)) \
 F(void, glDeleteSync, (GLsync)) \
 F(void, glFlush, ())
struct GLApi
{
#define DECLARE(result, name, args) result (APIENTRY *name) args = nullptr;
    GL_FUNCTIONS(DECLARE)
#undef DECLARE
#ifndef __EMSCRIPTEN__
    void *(APIENTRY *glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield) = nullptr;
    GLboolean (APIENTRY *glUnmapBuffer)(GLenum) = nullptr;
#endif
    void (APIENTRY *glClearDepthf)(GLfloat) = nullptr;
#ifndef __EMSCRIPTEN__
    void (APIENTRY *glClearDepth)(GLdouble) = nullptr;
#endif
    void load()
    {
        glClearDepthf = reinterpret_cast<decltype(glClearDepthf)>(SDL_GL_GetProcAddress("glClearDepthf"));
#ifndef __EMSCRIPTEN__
        glClearDepth = reinterpret_cast<decltype(glClearDepth)>(SDL_GL_GetProcAddress("glClearDepth"));
#endif
#define LOAD(result, name, args) name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress(#name)); if (!name) throw std::runtime_error("Missing GL entry point " #name);
        GL_FUNCTIONS(LOAD)
#undef LOAD
#ifdef __EMSCRIPTEN__
        // Preserve the GLES signature through a typed symbol reference instead
        // of reinterpreting the SDK's split-timeout import as a GLuint64 call.
        glClientWaitSync = &::glClientWaitSync;
#endif
#ifndef __EMSCRIPTEN__
        glMapBufferRange = reinterpret_cast<decltype(glMapBufferRange)>(SDL_GL_GetProcAddress("glMapBufferRange"));
        glUnmapBuffer = reinterpret_cast<decltype(glUnmapBuffer)>(SDL_GL_GetProcAddress("glUnmapBuffer"));
        if (!glMapBufferRange || !glUnmapBuffer) throw std::runtime_error("Missing GL readback entry points");
#endif
    }
};
#undef GL_FUNCTIONS
std::string readShader(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read renderer shader: " + path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
class ProgrammableGLDevice final : public RenderDevice
{
    struct Allocation
    {
        std::weak_ptr<const TextureRevision> snapshot;
        GLuint texture = 0, framebuffer = 0, depth = 0;
        bool premultiplied=false;
        std::size_t bytes = 0, depthBytes = 0, category = 0;
        std::uint64_t lastUse = 0;
    };
    struct CoverageAllocation {
        GLuint weights=0,selectors=0;
        std::size_t bytes=0;
        std::uint64_t lastUse=0;
        std::weak_ptr<const CoverageData> source;
    };
    std::map<std::tuple<std::uint64_t,std::uint64_t,std::uint32_t,std::uint32_t>,CoverageAllocation> masks;
    struct ScratchAllocation {GLuint texture=0,framebuffer=0;std::size_t bytes=0;std::uint64_t lastUse=0;};
    std::map<unsigned,ScratchAllocation> scratchTargets;
    std::size_t scratchBytes=0;
    std::shared_ptr<const CoverageData> uniformCoverage;
    struct ComposeProgram {GLuint program=0;GLint viewport=-1,params=-1,geometry=-1,fringe=-1;};
    ComposeProgram composeProgram,reduceProgram;
    bool supportsTerrain=false;
    struct MeshAllocation { std::weak_ptr<const GeometryRevision> geometry; GLuint vertices = 0, indices = 0, vao = 0;
        std::uint64_t lastUse = 0; std::size_t bytes = 0; };
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint64_t>, MeshAllocation> meshes;
    std::size_t meshBytes = 0;
    struct MaterialProgram { GLuint program = 0; GLint projection = -1, uvTransform = -1, sun = -1, image = -1; };
    MaterialProgram worldProgram, cloudProgram;
    GLuint pointProgram = 0;
    GLint pointViewport = -1, pointAlpha = -1;
    bool supportsWorld = false, supportsSkins = false;
    struct SkinProgram { GLuint program=0; GLint shell=-1,furLength=-1,shellDepth=-1,region=-1,bones=-1,view=-1,normalView=-1,paint=-1,material=-1; };
    SkinProgram skinProgram, rigProgram;
    GLuint screenDepth = 0;
    struct Pending { RenderSubmission submission; GLsync fence; };
    struct Readback { CaptureRequest request; GLuint buffer = 0; GLsync fence = nullptr;
        std::chrono::steady_clock::time_point admitted=std::chrono::steady_clock::now(); };
    SDL_Window *window;
    SDL_GLContext context = nullptr;
    GLApi gl;
    GLuint program = 0, vao = 0, vertexBuffer = 0, whiteTexture = 0, screenTexture = 0, screenFramebuffer = 0, committedTexture = 0, committedFramebuffer = 0;
    GLint viewportUniform = -1, imageUniform = -1, texturedUniform = -1, alphaUniform = -1, boundsUniform = -1;
    int width = 0, height = 0, maxTexture = 0;
    std::uint64_t epoch = 1, issued = 0, completed = 0;
    bool frameOpen = false;
    using TextureKey = std::tuple<std::uint32_t, std::uint32_t, std::uint64_t>;
    static TextureKey key(const TextureSnapshot &snapshot)
    { return {snapshot->handle.slot, snapshot->handle.generation, snapshot->contentRevision}; }
    std::map<std::pair<TextureKey, bool>, Allocation> textures;
    std::map<TextureKey, Allocation> targets;
    std::deque<Pending> pending;
    std::deque<Readback> readbacks;
    RenderTelemetry counts;
    static constexpr std::size_t TextureBudget = 256 * 1024 * 1024;
    static constexpr std::array<std::size_t,4> ClassLimits{64*1024*1024,64*1024*1024,128*1024*1024,32*1024*1024};
    std::array<std::size_t,4> classBytes{};
    std::size_t textureBytes = 0, vertexBytes = 0, captureBytes = 0;

    void current()
    {
        if (!SDL_GL_MakeCurrent(window, context)) throw std::runtime_error(SDL_GetError());
    }
    void check(const char *operation)
    {
        const GLenum error = gl.glGetError();
        if (error != GL_NO_ERROR) throw std::runtime_error(std::string(operation) + " GL error " + std::to_string(error));
    }
    GLuint shader(GLenum kind, const std::string &source)
    {
        const GLuint shader = gl.glCreateShader(kind);
        const char *data = source.c_str();
        gl.glShaderSource(shader, 1, &data, nullptr); gl.glCompileShader(shader);
        GLint ok; gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096]{}; gl.glGetShaderInfoLog(shader, sizeof(log), nullptr, log); gl.glDeleteShader(shader);
            throw std::runtime_error(std::string("Renderer shader compilation: ") + log);
        }
        return shader;
    }
    void destroy(Allocation &allocation)
    {
        if (allocation.depth) gl.glDeleteRenderbuffers(1, &allocation.depth);
        if (allocation.framebuffer) gl.glDeleteFramebuffers(1, &allocation.framebuffer);
        if (allocation.texture) gl.glDeleteTextures(1, &allocation.texture);
        textureBytes -= allocation.bytes; classBytes[allocation.category] -= allocation.bytes-allocation.depthBytes;
    }
    void destroy(CoverageAllocation &value)
    {
        gl.glDeleteTextures(1,&value.weights);gl.glDeleteTextures(1,&value.selectors);
        textureBytes-=value.bytes;classBytes[std::size_t(TextureDescriptor::BudgetClass::TerrainMask)]-=value.bytes;
    }
    void destroy(ScratchAllocation &value)
    {
        gl.glDeleteTextures(1,&value.texture);gl.glDeleteFramebuffers(1,&value.framebuffer);
        textureBytes-=value.bytes;scratchBytes-=value.bytes;
    }
    void poll()
    {
        while (!pending.empty()) {
            const GLenum result = gl.glClientWaitSync(pending.front().fence, 0, 0);
            if (result == GL_TIMEOUT_EXPIRED) break;
            if (result == GL_WAIT_FAILED) throw std::runtime_error("Renderer submission fence failed");
            completed = pending.front().submission.identity;
            gl.glDeleteSync(pending.front().fence); pending.pop_front();
        }
    }
    void trim(std::size_t admission = 0, std::size_t category = 0, bool colorAdmission = true, bool releaseCompleted = false)
    {
        const auto eligible = [this](const Allocation &value) { return value.lastUse <= completed; };
        for (auto it = textures.begin(); it != textures.end();) {
            if (eligible(it->second) && (releaseCompleted || it->second.snapshot.expired() || textureBytes + admission > TextureBudget-captureBytes || (colorAdmission && it->second.category == category && admission > ClassLimits[category] - classBytes[category]))) {
                destroy(it->second); it = textures.erase(it);
            } else ++it;
        }
        for (auto it = targets.begin(); it != targets.end();) {
            // Rendered target content cannot be reconstructed from CPU pixels;
            // retain it while its logical revision is referenced externally.
            if (eligible(it->second) && it->second.snapshot.expired()) {
                destroy(it->second); it = targets.erase(it);
            } else ++it;
        }
        for(auto it=masks.begin();it!=masks.end();) {
            const bool pressure=admission>TextureBudget-textureBytes-captureBytes ||
                (colorAdmission && category==std::size_t(TextureDescriptor::BudgetClass::TerrainMask) && admission>ClassLimits[category]-classBytes[category]);
            if(it->second.lastUse<=completed && (releaseCompleted || it->second.source.expired() || pressure)){destroy(it->second);it=masks.erase(it);}else ++it;
        }
        for(auto it=scratchTargets.begin();it!=scratchTargets.end();) {
            if(it->second.lastUse<=completed && (releaseCompleted || admission>TextureBudget-textureBytes-captureBytes)){destroy(it->second);it=scratchTargets.erase(it);}else ++it;
        }
        if (admission > TextureBudget - textureBytes - captureBytes || (colorAdmission && admission > ClassLimits[category] - classBytes[category])) throw RenderAdmissionDeferred("GL texture budget exhausted");
        counts.resourceBytes = textureBytes;
    }
    void screen()
    {
        int w, h; SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w <= 0 || h <= 0) throw std::runtime_error("Renderer window has no drawable pixels");
        if (w == width && h == height) return;
        if (w > maxTexture || h > maxTexture || std::uint64_t(w) * h * 4 > 128 * 1024 * 1024)
            throw std::runtime_error("Renderer output exceeds texture or presentation memory limit");
        if (screenDepth) { gl.glDeleteRenderbuffers(1, &screenDepth); screenDepth = 0; }
        if (screenFramebuffer) gl.glDeleteFramebuffers(1, &screenFramebuffer);
        if (screenTexture) gl.glDeleteTextures(1, &screenTexture);
        if (committedFramebuffer) gl.glDeleteFramebuffers(1,&committedFramebuffer);
        if (committedTexture) gl.glDeleteTextures(1,&committedTexture);
        width = w; height = h;
        gl.glGenTextures(1, &screenTexture); gl.glBindTexture(GL_TEXTURE_2D, screenTexture);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.glGenFramebuffers(1, &screenFramebuffer); gl.glBindFramebuffer(GL_FRAMEBUFFER, screenFramebuffer);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexture, 0);
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Renderer output framebuffer incomplete");
        gl.glDisable(GL_SCISSOR_TEST); gl.glClearColor(0, 0, 0, 1); gl.glClear(GL_COLOR_BUFFER_BIT);
        gl.glGenTextures(1,&committedTexture);gl.glBindTexture(GL_TEXTURE_2D,committedTexture);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        gl.glGenFramebuffers(1,&committedFramebuffer);gl.glBindFramebuffer(GL_FRAMEBUFFER,committedFramebuffer);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,committedTexture,0);
        if(gl.glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Completed output framebuffer incomplete");
        gl.glClear(GL_COLOR_BUFFER_BIT);
        check("Create output");
    }
    Allocation allocate(TextureSnapshot snapshot, bool premultiplied, bool target)
    {
        const auto &descriptor = snapshot->descriptor;
        if (descriptor.width <= 0 || descriptor.height <= 0 || descriptor.width > maxTexture || descriptor.height > maxTexture)
            throw std::invalid_argument("Invalid GL texture dimensions");
        auto required = std::uint64_t(descriptor.width) * descriptor.height * 4;
        for (const auto &level : snapshot->mips) required += level.rgba.size();
        if (target && descriptor.mips != TextureDescriptor::Mips::Disabled) throw std::invalid_argument("Authored mip texture cannot be a render target");
        const auto category = std::size_t(descriptor.budgetClass);
        if (category >= classBytes.size()) throw std::invalid_argument("Invalid GL texture budget class");
        if (required > TextureBudget || required > ClassLimits[category]) throw std::length_error("GL texture exceeds resource budget");
        const std::size_t bytes = std::size_t(required);
        trim(bytes, category);
        auto converted = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
            SDL_ConvertSurface(snapshot->pixels.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!converted) throw std::runtime_error(SDL_GetError());
        if (!SDL_LockSurface(converted.get())) throw std::runtime_error(SDL_GetError());
        std::vector<Uint8> packed(std::size_t(descriptor.width)*descriptor.height*4);
        for (int y = 0; y < descriptor.height; ++y) {
            const auto *row = static_cast<const Uint8 *>(converted->pixels) + y * converted->pitch;
            auto *destination = packed.data() + std::size_t(target ? descriptor.height - y - 1 : y) * descriptor.width * 4;
            std::memcpy(destination, row, std::size_t(descriptor.width) * 4);
            if (premultiplied && descriptor.alpha!=TextureDescriptor::Alpha::Premultiplied)
                for (int x = 0; x < descriptor.width; ++x)
                    for (int channel = 0; channel < 3; ++channel)
                        destination[x * 4 + channel] = Uint8((unsigned(destination[x * 4 + channel]) * destination[x * 4 + 3] + 127) / 255);
        }
        SDL_UnlockSurface(converted.get());
        Allocation allocation; allocation.snapshot = snapshot; allocation.bytes = bytes; allocation.category = category; allocation.premultiplied=premultiplied || descriptor.alpha==TextureDescriptor::Alpha::Premultiplied;
        gl.glGenTextures(1, &allocation.texture); gl.glBindTexture(GL_TEXTURE_2D, allocation.texture);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, descriptor.addressU == TextureDescriptor::Address::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, descriptor.addressV == TextureDescriptor::Address::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, descriptor.width, descriptor.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, packed.data());
        unsigned mipIndex = 1;
        for (const auto &level : snapshot->mips) {
            std::vector<Uint8> rgba(level.rgba.size()); std::memcpy(rgba.data(), level.rgba.data(), rgba.size());
            if (premultiplied && descriptor.alpha != TextureDescriptor::Alpha::Premultiplied)
                for (std::size_t pixel = 0; pixel < rgba.size(); pixel += 4)
                    for (unsigned channel = 0; channel < 3; ++channel)
                        rgba[pixel+channel] = Uint8((unsigned(rgba[pixel+channel])*rgba[pixel+3]+127)/255);
            gl.glTexImage2D(GL_TEXTURE_2D, mipIndex++, GL_RGBA8, level.width, level.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(mipIndex-1));
        if (target) {
            gl.glGenFramebuffers(1, &allocation.framebuffer); gl.glBindFramebuffer(GL_FRAMEBUFFER, allocation.framebuffer);
            gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, allocation.texture, 0);
            if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                gl.glDeleteFramebuffers(1, &allocation.framebuffer); gl.glDeleteTextures(1, &allocation.texture);
                throw std::runtime_error("Renderer offscreen framebuffer incomplete");
            }
        }
        textureBytes += bytes; classBytes[category] += bytes; counts.uploadBytes += bytes; counts.resourceBytes = textureBytes;
        try { check("Upload texture"); }
        catch (...) { destroy(allocation); counts.resourceBytes = textureBytes; throw; }
        return allocation;
    }
    Allocation &target(TextureSnapshot snapshot)
    {
        auto found = targets.find(key(snapshot));
        if (found == targets.end()) found = targets.emplace(key(snapshot), allocate(snapshot, snapshot->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied, true)).first;
        found->second.lastUse = issued + 1; return found->second;
    }
    Allocation &texture(TextureSnapshot snapshot, bool linear)
    {
        if (auto found = targets.find(key(snapshot)); found != targets.end()) {
            found->second.lastUse = issued + 1; return found->second;
        }
        const auto textureKey = std::make_pair(key(snapshot), linear);
        auto found = textures.find(textureKey);
        if (found == textures.end()) found = textures.emplace(textureKey, allocate(snapshot, linear, false)).first;
        found->second.lastUse = issued + 1; return found->second;
    }
    SDL_Rect clip(const DrawPacket &packet, int w, int h, float logicalW, float logicalH)
    {
        SDL_Rect result{0, 0, w, h};
        const float nativeX = float(w) / logicalW, nativeY = float(h) / logicalH;
        const auto intersect = [&](SDL_Rect rect) {
            const int x = int(std::floor(rect.x * nativeX)), y = int(std::floor(rect.y * nativeY));
            SDL_Rect native{x, y, int(std::ceil((rect.x + rect.w) * nativeX)) - x, int(std::ceil((rect.y + rect.h) * nativeY)) - y};
            if (!SDL_GetRectIntersection(&result, &native, &result)) result = {0, 0, 0, 0};
        };
        if (packet.transform.bounds) intersect(*packet.transform.bounds);
        if (packet.clip) {
            const auto &source = *packet.clip;
            const auto &t = packet.transform;
            const int x = int(std::floor(source.x * t.scale + t.x)), y = int(std::floor(source.y * t.scale + t.y));
            intersect({x, y, int(std::ceil((source.x + source.w) * t.scale + t.x)) - x,
                int(std::ceil((source.y + source.h) * t.scale + t.y)) - y});
        }
        return detail::intersectTargetScissor(result, packet.targetScissor);
    }
    void appendVertices(std::vector<SDL_Vertex> &vertices, const DrawPacket &packet, bool flip, bool premultiplied)
    {
        const std::size_t begin = vertices.size();
        if (packet.kind == DrawPacket::Kind::Fill || packet.kind == DrawPacket::Kind::Blit) {
            const auto &r = packet.destination;
            SDL_FColor color{packet.color.r / 255.f, packet.color.g / 255.f, packet.color.b / 255.f, packet.color.a / 255.f};
            float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
            if (packet.kind == DrawPacket::Kind::Blit) {
                const auto &d = packet.texture->descriptor;
                u0 = float(packet.source.x) / d.width; v0 = float(packet.source.y) / d.height;
                u1 = float(packet.source.x + packet.source.w) / d.width; v1 = float(packet.source.y + packet.source.h) / d.height;
            }
            const SDL_Vertex a{{r.x, r.y}, color, {u0, v0}}, b{{r.x + r.w, r.y}, color, {u1, v0}},
                c{{r.x + r.w, r.y + r.h}, color, {u1, v1}}, d{{r.x, r.y + r.h}, color, {u0, v1}};
            vertices.insert(vertices.end(), {a, b, c, a, c, d});
        } else vertices.insert(vertices.end(), packet.vertices.begin(), packet.vertices.end());
        for (std::size_t i = begin; i < vertices.size(); ++i) {
            auto &vertex = vertices[i];
            if (packet.kind != DrawPacket::Kind::ScreenTriangles) {
                vertex.position.x = vertex.position.x * packet.transform.scale + packet.transform.x;
                vertex.position.y = vertex.position.y * packet.transform.scale + packet.transform.y;
            }
            if (flip) vertex.tex_coord.y = 1 - vertex.tex_coord.y;
            if (premultiplied) {
                const float alpha = vertex.color.a * packet.alpha / 255.f;
                vertex.color.r *= alpha; vertex.color.g *= alpha; vertex.color.b *= alpha;
            }
        }
    }
    void draw(const DrawPacket &packet, std::span<const SDL_Vertex> vertices, float logicalW, float logicalH)
    {
        GLuint framebuffer = screenFramebuffer;
        int w = width, h = height;
        if (packet.target) { auto &destination = target(packet.target); framebuffer = destination.framebuffer;
            w = packet.target->descriptor.width; h = packet.target->descriptor.height; }
        GLuint source = 0; bool premultiplied = false;
        if (packet.texture) {
            auto &allocation = texture(packet.texture, packet.linear); source = allocation.texture;
            premultiplied = allocation.premultiplied;
        }
        if (packet.targetLogicalWidth > 0) logicalW = packet.targetLogicalWidth;
        if (packet.targetLogicalHeight > 0) logicalH = packet.targetLogicalHeight;
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        const SDL_Rect viewport = packet.viewport.value_or(SDL_Rect{0, 0, w, h});
        gl.glViewport(viewport.x, h - viewport.y - viewport.h, viewport.w, viewport.h);
        const auto bounds = clip(packet, w, h, logicalW, logicalH);
        gl.glEnable(GL_SCISSOR_TEST); gl.glScissor(bounds.x, h - bounds.y - bounds.h, bounds.w, bounds.h);
        state(packet, premultiplied);
        GLuint selectedProgram = packet.material == DrawPacket::Material::SmoothPoint ? pointProgram : program;
        if (!selectedProgram) throw std::invalid_argument("Unsupported point material");
        gl.glUseProgram(selectedProgram);
        if (selectedProgram != program) {
            gl.glUniform2f(pointViewport, float(logicalW), float(logicalH));
            gl.glUniform1f(pointAlpha, packet.alpha / 255.f);
        } else { gl.glUniform2f(viewportUniform, float(logicalW), float(logicalH));
        gl.glUniform1i(imageUniform, 0); gl.glUniform1f(texturedUniform, source ? 1.f : 0.f);
        gl.glUniform1f(alphaUniform, packet.alpha / 255.f);
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (packet.linear && packet.kind == DrawPacket::Kind::Blit) {
            const auto &descriptor = packet.texture->descriptor;
            u0 = (packet.source.x + .5f) / descriptor.width;
            u1 = (packet.source.x + packet.source.w - .5f) / descriptor.width;
            v0 = (packet.source.y + .5f) / descriptor.height;
            v1 = (packet.source.y + packet.source.h - .5f) / descriptor.height;
            if (targets.contains(key(packet.texture))) { const float lower = 1 - v1; v1 = 1 - v0; v0 = lower; }
        }
        gl.glUniform4f(boundsUniform, u0, v0, u1, v1);
        }
        gl.glActiveTexture(GL_TEXTURE0); gl.glBindTexture(GL_TEXTURE_2D, source ? source : whiteTexture);
        if (source) {
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, packet.linear ? (packet.texture && packet.texture->descriptor.mips == TextureDescriptor::Mips::Generated ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR) : GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, packet.linear ? GL_LINEAR : GL_NEAREST);
        }
        gl.glBindVertexArray(vao); gl.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        vertexBytes = vertices.size_bytes();
        gl.glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertexBytes), vertices.data(), GL_STREAM_DRAW);
        gl.glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size())); ++counts.batches;
    }
    MaterialProgram materialProgram(const std::string &vertexFile, const std::string &fragmentFile)
    {
        auto vs = shader(GL_VERTEX_SHADER, readShader(vertexFile)); GLuint fs = 0;
        try { fs = shader(GL_FRAGMENT_SHADER, readShader(fragmentFile)); } catch (...) { gl.glDeleteShader(vs); throw; }
        MaterialProgram result; result.program = gl.glCreateProgram(); gl.glAttachShader(result.program, vs); gl.glAttachShader(result.program, fs);
        gl.glLinkProgram(result.program); gl.glDeleteShader(vs); gl.glDeleteShader(fs);
        GLint ok; gl.glGetProgramiv(result.program, GL_LINK_STATUS, &ok);
        if (!ok) { gl.glDeleteProgram(result.program); throw std::runtime_error("World material link failed"); }
        result.projection = gl.glGetUniformLocation(result.program, "projection");
        result.uvTransform = gl.glGetUniformLocation(result.program, "uvTransform");
        result.sun = gl.glGetUniformLocation(result.program, "sunSpecular");
        result.image = gl.glGetUniformLocation(result.program, "world"); return result;
    }
    void state(const DrawPacket &packet, bool premultiplied)
    {
        const auto blend = packet.blend;
        if (blend == DrawPacket::Blend::Opaque || (blend == DrawPacket::Blend::Automatic && packet.opaque && packet.alpha == 255)) gl.glDisable(GL_BLEND);
        else {
            gl.glEnable(GL_BLEND);
            const bool one = blend == DrawPacket::Blend::Premultiplied || (blend == DrawPacket::Blend::Automatic && premultiplied);
            gl.glBlendFuncSeparate(one ? GL_ONE : GL_SRC_ALPHA, blend == DrawPacket::Blend::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA,
                GL_ONE, blend == DrawPacket::Blend::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        }
        if (packet.depth == DrawPacket::Depth::Disabled) gl.glDisable(GL_DEPTH_TEST);
        else { gl.glEnable(GL_DEPTH_TEST); gl.glDepthFunc(packet.depth == DrawPacket::Depth::Less ? GL_LESS : GL_LEQUAL); }
        gl.glDepthMask(packet.depthWrite ? GL_TRUE : GL_FALSE);
        if (packet.cull == DrawPacket::Cull::Disabled) gl.glDisable(GL_CULL_FACE);
        else { gl.glEnable(GL_CULL_FACE); gl.glCullFace(GL_BACK); gl.glFrontFace(GL_CCW); }
    }
    void depthAttachment(GLuint framebuffer, GLuint &depth, int w, int h, Allocation *allocation=nullptr)
    {
        if (depth) return;
        const std::size_t bytes=std::size_t(w)*h*4;
        // Atlas class limits describe authored color storage; depth is a
        // bounded rendering scratch cost charged to the aggregate GPU budget.
        if(allocation){
            if(bytes+allocation->bytes>TextureBudget)throw std::length_error("Depth target exceeds aggregate budget");
            trim(bytes,allocation->category,false);
        }
        gl.glGenRenderbuffers(1, &depth); gl.glBindRenderbuffer(GL_RENDERBUFFER, depth);
        gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gl.glDeleteRenderbuffers(1,&depth); depth=0; throw std::runtime_error("World depth attachment unavailable");
        }
        if(allocation){ allocation->bytes+=bytes; allocation->depthBytes+=bytes; textureBytes+=bytes; }
    }
    void validateCompose(const DrawPacket &packet) const
    {
        if(!supportsTerrain || !packet.weightedCompose || !packet.target || packet.clip || packet.targetScissor || packet.viewport || packet.depth!=DrawPacket::Depth::Disabled || packet.depthWrite)throw std::invalid_argument("Terrain composition unavailable or missing target");
        const auto &data=*packet.weightedCompose;const auto size=data.compositionSize,divisor=data.reductionDivisor;
        if(!size || size%32 || size>unsigned(maxTexture) || !divisor || divisor>32 || (divisor&(divisor-1)) || size%divisor || !data.paletteCount || data.paletteCount>4)
            throw std::invalid_argument("Invalid terrain composition dimensions or palette");
        const auto &rect=data.targetRect;const auto &target=packet.target->descriptor;
        if((target.alpha!=TextureDescriptor::Alpha::Straight && target.alpha!=TextureDescriptor::Alpha::Premultiplied) || rect.x<0 || rect.y<0 || rect.w!=int(size/divisor) || rect.h!=rect.w || rect.w>target.width || rect.h>target.height || rect.x>target.width-rect.w || rect.y>target.height-rect.h)
            throw std::invalid_argument("Terrain composition requires bounded explicit-alpha target pixels");
        for(unsigned i=0;i<data.paletteCount;++i)if(data.materials[i]){
            const auto &source=data.materials[i]->descriptor;
            if(source.alpha==TextureDescriptor::Alpha::Premultiplied || source.width<=0 || source.height<=0 || targets.contains(key(data.materials[i])) || key(data.materials[i])==key(packet.target))
                throw std::invalid_argument("Terrain composition requires immutable straight material pixels");
        }
        if(data.paletteCount>1){
            const auto &mask=data.coverage;
            if(!mask || !mask->identity || mask->width!=size || mask->height!=size || mask->samples.size()!=std::uint64_t(size)*size)
                throw std::invalid_argument("Terrain composition requires matching immutable coverage");
            for(const auto &sample:mask->samples){
                const auto sum=unsigned(sample.others[0])+sample.others[1]+sample.others[2];
                if(sum>65536 || (sample.slots&3)>=data.paletteCount || ((sample.slots>>2)&3)>=data.paletteCount || ((sample.slots>>4)&3)>=data.paletteCount || sample.slots>=64)
                    throw std::invalid_argument("Invalid terrain coverage weights or palette selectors");
            }
        }
    }
    void terrainQuad(const ComposeProgram &material,GLuint framebuffer,unsigned width,unsigned height,const SDL_Rect &rect)
    {
        gl.glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);gl.glViewport(0,0,width,height);
        gl.glEnable(GL_SCISSOR_TEST);gl.glScissor(rect.x,int(height)-rect.y-rect.h,rect.w,rect.h);
        gl.glDisable(GL_BLEND);gl.glDisable(GL_DEPTH_TEST);gl.glDepthMask(GL_FALSE);gl.glDisable(GL_CULL_FACE);
        gl.glUseProgram(material.program);gl.glUniform2f(material.viewport,float(width),float(height));
        const float x=rect.x,y=rect.y,r=x+rect.w,b=y+rect.h;const SDL_FColor white{1,1,1,1};
        const std::array<SDL_Vertex,6> vertices{{{{x,y},white,{0,0}},{{r,y},white,{1,0}},{{x,b},white,{0,1}},
            {{x,b},white,{0,1}},{{r,y},white,{1,0}},{{r,b},white,{1,1}}}};
        gl.glBindVertexArray(vao);gl.glBindBuffer(GL_ARRAY_BUFFER,vertexBuffer);vertexBytes=sizeof(vertices);
        gl.glBufferData(GL_ARRAY_BUFFER,vertexBytes,vertices.data(),GL_STREAM_DRAW);check("Prepare terrain quad");
        gl.glDrawArrays(GL_TRIANGLES,0,6);check("Draw terrain quad");++counts.batches;
    }
    CoverageAllocation &coverage(std::shared_ptr<const CoverageData> source)
    {
        if(!source){
            if(!uniformCoverage){auto value=std::make_shared<CoverageData>();value->identity=~std::uint64_t(0);value->width=value->height=1;value->samples.resize(1);uniformCoverage=value;}
            source=uniformCoverage;
        }
        const auto identity=std::tuple{source->identity,source->revision,source->width,source->height};
        if(auto found=masks.find(identity);found!=masks.end()){found->second.lastUse=issued+1;return found->second;}
        const auto pixels=std::uint64_t(source->width)*source->height;
        if(!source->identity || !pixels || source->width>unsigned(maxTexture) || source->height>unsigned(maxTexture) || source->samples.size()!=pixels || pixels>32*1024*1024/9)
            throw std::invalid_argument("Invalid terrain coverage dimensions or budget");
        const std::size_t bytes=std::size_t(pixels)*9;
        trim(bytes,std::size_t(TextureDescriptor::BudgetClass::TerrainMask));
        std::vector<std::uint16_t> weights(std::size_t(pixels)*4);std::vector<Uint8> selectors(pixels);
        for(std::size_t i=0;i<pixels;++i){const auto &sample=source->samples[i];std::copy(sample.others.begin(),sample.others.end(),weights.begin()+i*4);weights[i*4+3]=sample.margin;selectors[i]=sample.slots;}
        CoverageAllocation result;result.bytes=bytes;result.source=source;result.lastUse=issued+1;
        gl.glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        auto upload=[&](GLuint &image,GLint format,GLenum channels,GLenum type,const void *data){
            gl.glGenTextures(1,&image);gl.glBindTexture(GL_TEXTURE_2D,image);
            gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            gl.glTexImage2D(GL_TEXTURE_2D,0,format,source->width,source->height,0,channels,type,data);
        };
        upload(result.weights,GL_RGBA16UI,GL_RGBA_INTEGER,GL_UNSIGNED_SHORT,weights.data());upload(result.selectors,GL_R8UI,GL_RED_INTEGER,GL_UNSIGNED_BYTE,selectors.data());
        textureBytes+=bytes;classBytes[std::size_t(TextureDescriptor::BudgetClass::TerrainMask)]+=bytes;counts.uploadBytes+=bytes;
        try{check("Upload terrain coverage");}catch(...){destroy(result);throw;}
        return masks.emplace(identity,result).first->second;
    }
    ScratchAllocation &compositionScratch(unsigned size)
    {
        if(auto found=scratchTargets.find(size);found!=scratchTargets.end()){found->second.lastUse=issued+1;return found->second;}
        const auto required=std::uint64_t(size)*size*4;
        if(!size || size>unsigned(maxTexture) || required>32*1024*1024)throw std::length_error("Terrain reduction scratch exceeds budget");
        const auto bytes=std::size_t(required);
        for(auto it=scratchTargets.begin();it!=scratchTargets.end() && bytes>32*1024*1024-scratchBytes;){
            if(it->second.lastUse<=completed){destroy(it->second);it=scratchTargets.erase(it);}else ++it;
        }
        if(bytes>32*1024*1024-scratchBytes)throw RenderAdmissionDeferred("Terrain reduction scratch temporarily full");
        trim(bytes,0,false);ScratchAllocation value;value.bytes=bytes;value.lastUse=issued+1;
        gl.glGenTextures(1,&value.texture);gl.glBindTexture(GL_TEXTURE_2D,value.texture);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        gl.glGenFramebuffers(1,&value.framebuffer);gl.glBindFramebuffer(GL_FRAMEBUFFER,value.framebuffer);gl.glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,value.texture,0);
        textureBytes+=bytes;scratchBytes+=bytes;
        try{if(gl.glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Terrain scratch framebuffer incomplete");check("Allocate terrain scratch");}catch(...){destroy(value);throw;}
        return scratchTargets.emplace(size,value).first->second;
    }
    ComposeProgram terrainProgram(const std::string &vertexFile,const std::string &fragmentFile,bool reduction)
    {
        ComposeProgram result;result.program=materialProgram(vertexFile,fragmentFile).program;
        result.viewport=gl.glGetUniformLocation(result.program,"viewport");result.params=gl.glGetUniformLocation(result.program,"params");
        result.geometry=gl.glGetUniformLocation(result.program,"seamGeometry");result.fringe=gl.glGetUniformLocation(result.program,"seamFringe");
        gl.glUseProgram(result.program);
        if(reduction)gl.glUniform1i(gl.glGetUniformLocation(result.program,"rawImage"),0);
        else{
            for(unsigned slot=0;slot<4;++slot)gl.glUniform1i(gl.glGetUniformLocation(result.program,("material"+std::to_string(slot)).c_str()),slot);
            gl.glUniform1i(gl.glGetUniformLocation(result.program,"coverageWeights"),4);gl.glUniform1i(gl.glGetUniformLocation(result.program,"coverageSelectors"),5);
        }
        return result;
    }
    void drawCompose(const DrawPacket &packet)
    {
        const auto &data=*packet.weightedCompose;const bool reduction=data.reductionDivisor>1;
        const bool pma=packet.target->descriptor.alpha==TextureDescriptor::Alpha::Premultiplied;
        auto &destination=target(packet.target);
        std::array<GLuint,4> images{};GLuint present=0;
        for(unsigned slot=0;slot<data.paletteCount;++slot)if(data.materials[slot]){images[slot]=texture(data.materials[slot],false).texture;present|=1u<<slot;}
        auto &mask=coverage(data.paletteCount>1?data.coverage:nullptr);
        ScratchAllocation *scratch=reduction?&compositionScratch(data.compositionSize):nullptr;
        gl.glUseProgram(composeProgram.program);
        gl.glUniform4ui(composeProgram.params,data.compositionSize,data.paletteCount,present,(data.paletteCount>1?1u:0u)|(!reduction && pma?2u:0u));
        std::array<GLint,16> geometry{},fringe{};
        for(unsigned slot=0;slot<4;++slot){const auto &seam=data.seams[slot];
            geometry[slot*4]=seam.height;geometry[slot*4+1]=seam.cast;geometry[slot*4+2]=seam.castWidth;geometry[slot*4+3]=seam.fringe;
            fringe[slot*4]=seam.fringeWidth;for(unsigned channel=0;channel<3;++channel)fringe[slot*4+channel+1]=seam.fringeColor[channel];}
        gl.glUniform4iv(composeProgram.geometry,4,geometry.data());gl.glUniform4iv(composeProgram.fringe,4,fringe.data());
        check("Set terrain composition uniforms");
        for(unsigned slot=0;slot<4;++slot){gl.glActiveTexture(GL_TEXTURE0+slot);gl.glBindTexture(GL_TEXTURE_2D,images[slot]?images[slot]:whiteTexture);}
        gl.glActiveTexture(GL_TEXTURE4);gl.glBindTexture(GL_TEXTURE_2D,mask.weights);gl.glActiveTexture(GL_TEXTURE5);gl.glBindTexture(GL_TEXTURE_2D,mask.selectors);
        if(scratch){
            const SDL_Rect full{0,0,int(data.compositionSize),int(data.compositionSize)};
            terrainQuad(composeProgram,scratch->framebuffer,data.compositionSize,data.compositionSize,full);
            gl.glUseProgram(reduceProgram.program);gl.glUniform4ui(reduceProgram.params,data.reductionDivisor|(pma?65536u:0u),0,0,data.compositionSize);
            gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,scratch->texture);
            terrainQuad(reduceProgram,destination.framebuffer,packet.target->descriptor.width,packet.target->descriptor.height,data.targetRect);
        }else terrainQuad(composeProgram,destination.framebuffer,packet.target->descriptor.width,packet.target->descriptor.height,data.targetRect);
        gl.glActiveTexture(GL_TEXTURE0);check("Compose terrain material pixels");
    }
    void destroyMesh(MeshAllocation &value)
    {
        gl.glDeleteVertexArrays(1, &value.vao); gl.glDeleteBuffers(1, &value.vertices);
        if (value.indices) gl.glDeleteBuffers(1, &value.indices);
        meshBytes -= value.bytes;
    }
    MeshAllocation &mesh(const GeometrySnapshot &geometry)
    {
        if (!geometry || (geometry->layout != GeometryLayout::World48 && geometry->layout != GeometryLayout::Skin40 && geometry->layout != GeometryLayout::SkinRig72)) throw std::invalid_argument("Invalid world geometry layout");
        const auto identity = std::make_tuple(geometry->handle.slot, geometry->handle.generation, geometry->contentRevision);
        auto found = meshes.find(identity);
        if (found == meshes.end()) {
            for (auto it = meshes.begin(); it != meshes.end();) {
                if (it->second.lastUse <= completed && it->second.geometry.expired()) { destroyMesh(it->second); it = meshes.erase(it); }
                else ++it;
            }
            if (geometry->bytes > 64 * 1024 * 1024 - meshBytes) throw RenderAdmissionDeferred("World geometry upload budget exhausted");
            MeshAllocation value; value.geometry = geometry; value.bytes = geometry->bytes;
            gl.glGenVertexArrays(1, &value.vao); gl.glBindVertexArray(value.vao);
            gl.glGenBuffers(1, &value.vertices); gl.glBindBuffer(GL_ARRAY_BUFFER, value.vertices);
            gl.glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(geometry->vertices.size()), geometry->vertices.data(), GL_STATIC_DRAW);
            const auto attributes = geometry->layout == GeometryLayout::World48 ? std::span<const RendererShaderLayouts::Attribute>(RendererShaderLayouts::world_attributes) :
                geometry->layout == GeometryLayout::Skin40 ? std::span<const RendererShaderLayouts::Attribute>(RendererShaderLayouts::skin_attributes) : std::span<const RendererShaderLayouts::Attribute>(RendererShaderLayouts::skin_rig_attributes);
            for (const auto &attribute : attributes) {
                gl.glEnableVertexAttribArray(attribute.location);
                gl.glVertexAttribPointer(attribute.location, GLint(attribute.components), GL_FLOAT, GL_FALSE,
                    geometry->stride, reinterpret_cast<const void *>(std::size_t(attribute.offset)));
            }
            if (!geometry->indices.empty()) {
                gl.glGenBuffers(1, &value.indices); gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, value.indices);
                gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(geometry->indices.size() * sizeof(std::uint32_t)), geometry->indices.data(), GL_STATIC_DRAW);
            }
            meshBytes += value.bytes; counts.uploadBytes += value.bytes; found = meshes.emplace(identity, value).first;
        }
        found->second.lastUse = issued + 1; return found->second;
    }
    SkinProgram loadSkin(const std::string &vertexFile,const std::string &fragmentFile)
    {
        SkinProgram result; result.program=materialProgram(vertexFile,fragmentFile).program;
        auto uniform=[&](const char *name){ return gl.glGetUniformLocation(result.program,name); };
        result.shell=uniform("shell"); result.furLength=uniform("furLength"); result.shellDepth=uniform("shellDepth");
        result.region=uniform("region"); result.bones=uniform("bones[0]"); result.view=uniform("view");
        result.normalView=uniform("normalView"); result.paint=uniform("paint"); result.material=uniform("material"); return result;
    }
    void drawSkin(const DrawPacket &packet, MeshAllocation &allocation)
    {
        const bool rig=packet.material==DrawPacket::Material::SkinRig;
        if (!packet.skin || !packet.texture || !packet.secondTexture ||
            packet.geometry->layout != (rig ? GeometryLayout::SkinRig72 : GeometryLayout::Skin40))
            throw std::invalid_argument("Invalid skin packet resources");
        const auto &skin=*packet.skin;
        if (!std::isfinite(skin.shell) || !std::isfinite(skin.furLength) || !std::isfinite(skin.shellDepth) ||
            !std::isfinite(skin.region[0]) || !std::isfinite(skin.region[1]))
            throw std::invalid_argument("Invalid skin material uniforms");
        const auto &program=rig ? rigProgram : skinProgram;
        state(packet,false); gl.glUseProgram(program.program);
        gl.glUniform1f(program.shell,skin.shell); gl.glUniform1f(program.furLength,skin.furLength); gl.glUniform1f(program.shellDepth,skin.shellDepth);
        gl.glUniform2f(program.region,skin.region[0],skin.region[1]);
        if (rig) {
            if (!skin.pose) throw std::invalid_argument("Missing skin rig pose");
            for (const auto &values : {std::span<const float>(skin.pose->bones), std::span<const float>(skin.pose->view), std::span<const float>(skin.pose->normalView)})
                for (float value:values) if (!std::isfinite(value)) throw std::invalid_argument("Non-finite skin rig pose");
            gl.glUniformMatrix4fv(program.bones,32,GL_FALSE,skin.pose->bones.data());
            gl.glUniformMatrix4fv(program.view,1,GL_FALSE,skin.pose->view.data());
            gl.glUniformMatrix3fv(program.normalView,1,GL_FALSE,skin.pose->normalView.data());
        }
        const auto paint=texture(packet.texture,false).texture, material=texture(packet.secondTexture,false).texture;
        gl.glUniform1i(program.paint,0); gl.glActiveTexture(GL_TEXTURE0); gl.glBindTexture(GL_TEXTURE_2D,paint);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.glUniform1i(program.material,1); gl.glActiveTexture(GL_TEXTURE1); gl.glBindTexture(GL_TEXTURE_2D,material);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.glBindVertexArray(allocation.vao);
        if(packet.indexed) gl.glDrawElements(GL_TRIANGLES,GLsizei(packet.count),GL_UNSIGNED_INT,reinterpret_cast<const void *>(std::size_t(packet.first)*4));
        else gl.glDrawArrays(GL_TRIANGLES,GLint(packet.first),GLsizei(packet.count));
        ++counts.batches;
    }
    void drawWorld(const DrawPacket &packet, float logicalW, float logicalH)
    {
        GLuint framebuffer = screenFramebuffer; GLuint *depth = &screenDepth; Allocation *targetAllocation=nullptr; int w = width, h = height;
        if (packet.target) { auto &allocation = target(packet.target); framebuffer = allocation.framebuffer; depth = &allocation.depth; targetAllocation=&allocation;
            w = packet.target->descriptor.width; h = packet.target->descriptor.height; }
        if (packet.targetLogicalWidth > 0) logicalW = packet.targetLogicalWidth;
        if (packet.targetLogicalHeight > 0) logicalH = packet.targetLogicalHeight;
        if (packet.clearDepth || packet.depth != DrawPacket::Depth::Disabled) depthAttachment(framebuffer, *depth, w, h, targetAllocation);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        const auto viewport = packet.viewport.value_or(SDL_Rect{0, 0, w, h});
        gl.glViewport(viewport.x, h - viewport.y - viewport.h, viewport.w, viewport.h);
        const auto bounds = clip(packet, w, h, logicalW, logicalH);
        gl.glEnable(GL_SCISSOR_TEST); gl.glScissor(bounds.x, h - bounds.y - bounds.h, bounds.w, bounds.h);
        if (packet.kind == DrawPacket::Kind::Clear) {
            GLbitfield flags = 0;
            if (packet.clearColor) { gl.glClearColor(packet.clearValue.r, packet.clearValue.g, packet.clearValue.b, packet.clearValue.a); flags |= GL_COLOR_BUFFER_BIT; }
            if (packet.clearDepth) {
                gl.glDepthMask(GL_TRUE);
                if (gl.glClearDepthf) gl.glClearDepthf(packet.clearDepthValue);
#ifndef __EMSCRIPTEN__
                else gl.glClearDepth(packet.clearDepthValue);
#endif
                flags |= GL_DEPTH_BUFFER_BIT;
            }
            gl.glClear(flags); return;
        }
        const bool skinMaterial=packet.material==DrawPacket::Material::Skin || packet.material==DrawPacket::Material::SkinRig;
        if(!skinMaterial && (packet.material!=DrawPacket::Material::WorldLit && packet.material!=DrawPacket::Material::Cloud))
            throw std::invalid_argument("Unsupported mesh material");
        if(!skinMaterial && (!packet.geometry || packet.geometry->layout!=GeometryLayout::World48))
            throw std::invalid_argument("World material requires World48 geometry");
        auto &allocation = mesh(packet.geometry);
        const auto available = packet.indexed ? packet.geometry->indices.size() : packet.geometry->vertices.size() / packet.geometry->stride;
        if (!packet.count || packet.first > available || packet.count > available - packet.first || packet.count % 3 ||
            packet.count > RenderFrame::VertexCapacity * (packet.indexed ? 3 : 1)) throw std::invalid_argument("World geometry range exceeds capacity");
        const auto &material = packet.material == DrawPacket::Material::Cloud ? cloudProgram : worldProgram;
        GLuint image = packet.texture ? texture(packet.texture, false).texture : whiteTexture;
        if (packet.material == DrawPacket::Material::Skin || packet.material == DrawPacket::Material::SkinRig) {
            drawSkin(packet, allocation); return;
        }
        state(packet, false); gl.glUseProgram(material.program);
        gl.glUniformMatrix4fv(material.projection, 1, GL_FALSE, packet.world.projection.data());
        auto uv = packet.world.uvTransform;
        // CPU uploads are top-left, while rendered GL targets have bottom-left texels.
        if (packet.texture && targets.contains(key(packet.texture))) { uv[1] = -uv[1]; uv[3] = 1 - uv[3]; }
        gl.glUniform4f(material.uvTransform, uv[0], uv[1], uv[2], uv[3]);
        gl.glUniform4f(material.sun, packet.world.sunSpecular[0], packet.world.sunSpecular[1], packet.world.sunSpecular[2], packet.world.sunSpecular[3]);
        gl.glUniform1i(material.image, 0); gl.glActiveTexture(GL_TEXTURE0); gl.glBindTexture(GL_TEXTURE_2D, image);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, packet.linear ? GL_LINEAR : GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, packet.linear ? GL_LINEAR : GL_NEAREST);
        gl.glBindVertexArray(allocation.vao);
        if (packet.indexed) gl.glDrawElements(GL_TRIANGLES, GLsizei(packet.count), GL_UNSIGNED_INT, reinterpret_cast<const void *>(std::size_t(packet.first) * 4));
        else gl.glDrawArrays(GL_TRIANGLES, GLint(packet.first), GLsizei(packet.count));
        ++counts.batches;
    }
    void cleanup() noexcept
    {
        if (!context) return;
        if (!SDL_GL_MakeCurrent(window, context)) { SDL_GL_DestroyContext(context); context = nullptr; return; }
        if (gl.glDeleteSync) {
            for (auto &value : pending) gl.glDeleteSync(value.fence);
            for (auto &value : readbacks) { gl.glDeleteSync(value.fence); gl.glDeleteBuffers(1, &value.buffer); }
            for (auto &[key, value] : masks) destroy(value);
            for (auto &[key, value] : scratchTargets) destroy(value);
            if(composeProgram.program)gl.glDeleteProgram(composeProgram.program);
            if(reduceProgram.program)gl.glDeleteProgram(reduceProgram.program);
            for (auto &[key, value] : meshes) destroyMesh(value);
            if (worldProgram.program) gl.glDeleteProgram(worldProgram.program);
            if (cloudProgram.program) gl.glDeleteProgram(cloudProgram.program);
            if (pointProgram) gl.glDeleteProgram(pointProgram);
        if (skinProgram.program) gl.glDeleteProgram(skinProgram.program);
        if (rigProgram.program) gl.glDeleteProgram(rigProgram.program);
            if (screenDepth) gl.glDeleteRenderbuffers(1, &screenDepth);
            for (auto &[key, value] : textures) destroy(value);
            for (auto &[key, value] : targets) destroy(value);
            if (screenFramebuffer) gl.glDeleteFramebuffers(1, &screenFramebuffer);
            if (screenTexture) gl.glDeleteTextures(1, &screenTexture);
            if(committedFramebuffer)gl.glDeleteFramebuffers(1,&committedFramebuffer);
            if(committedTexture)gl.glDeleteTextures(1,&committedTexture);
            if (whiteTexture) gl.glDeleteTextures(1, &whiteTexture);
            if (vertexBuffer) gl.glDeleteBuffers(1, &vertexBuffer);
            if (vao) gl.glDeleteVertexArrays(1, &vao);
            if (program) gl.glDeleteProgram(program);
        }
        SDL_GL_DestroyContext(context); context = nullptr;
    }
    void flipRows(SDL_Surface *surface)
    {
        std::vector<Uint8> row(std::size_t(surface->w) * 4);
        for (int y = 0; y < surface->h / 2; ++y) {
            auto *a = static_cast<Uint8 *>(surface->pixels) + y * surface->pitch;
            auto *b = static_cast<Uint8 *>(surface->pixels) + (surface->h - y - 1) * surface->pitch;
            std::memcpy(row.data(), a, row.size()); std::memcpy(a, b, row.size()); std::memcpy(b, row.data(), row.size());
        }
    }
  public:
    ProgrammableGLDevice(SDL_Window *window, const std::string &directory) : window(window)
    {
        if (!window) throw std::invalid_argument("GL rendering requires an OPENGL window");
        context = SDL_GL_CreateContext(window);
        if (!context) throw std::runtime_error(SDL_GetError());
        try {
            current();
#if defined(__EMSCRIPTEN__) && defined(HAVE_OPENGL)
            // The rollout build includes legacy GL hooks. SDL3 bypasses their
            // Browser context callbacks, so initialize each newly owned canvas
            // before invoking wrapped GL entry points. Programmable materials
            // use six real texture units; the legacy renderer's one-unit tuning
            // remains unchanged outside this initialization.
            const int previousUnits=EM_ASM_INT({
                const previous=Module['GL_MAX_TEXTURE_IMAGE_UNITS'];
                Module['GL_MAX_TEXTURE_IMAGE_UNITS']=Math.min(6,GLctx.getParameter(GLctx.MAX_TEXTURE_IMAGE_UNITS));
                return previous===undefined?-1:previous;
            });
            try {ApplicationHost::initializeOpenGLContext();}
            catch(...) {
                EM_ASM({if($0<0)delete Module['GL_MAX_TEXTURE_IMAGE_UNITS'];else Module['GL_MAX_TEXTURE_IMAGE_UNITS']=$0;},previousUnits);
                throw;
            }
            EM_ASM({if($0<0)delete Module['GL_MAX_TEXTURE_IMAGE_UNITS'];else Module['GL_MAX_TEXTURE_IMAGE_UNITS']=$0;},previousUnits);
#endif
            gl.load(); gl.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
            int profile; SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
            const std::string dialect = profile == SDL_GL_CONTEXT_PROFILE_ES ? "webgl2" : "desktop-gl";
            const auto vertex = shader(GL_VERTEX_SHADER, readShader(directory + "/" + dialect + "/primitive.vert"));
            GLuint fragment = 0;
            try { fragment = shader(GL_FRAGMENT_SHADER, readShader(directory + "/" + dialect + "/primitive.frag")); }
            catch (...) { gl.glDeleteShader(vertex); throw; }
            program = gl.glCreateProgram(); gl.glAttachShader(program, vertex); gl.glAttachShader(program, fragment); gl.glLinkProgram(program);
            gl.glDeleteShader(vertex); gl.glDeleteShader(fragment);
            GLint ok; gl.glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok) { char log[4096]{}; gl.glGetProgramInfoLog(program, sizeof(log), nullptr, log); throw std::runtime_error(std::string("Renderer shader link: ") + log); }
            viewportUniform = gl.glGetUniformLocation(program, "viewport"); imageUniform = gl.glGetUniformLocation(program, "image");
            texturedUniform = gl.glGetUniformLocation(program, "textured"); alphaUniform = gl.glGetUniformLocation(program, "alpha");
            boundsUniform = gl.glGetUniformLocation(program, "uvBounds");
            gl.glGenVertexArrays(1, &vao); gl.glBindVertexArray(vao); gl.glGenBuffers(1, &vertexBuffer); gl.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            static_assert(sizeof(SDL_Vertex) == RendererShaderLayouts::primitive_stride);
            static_assert(offsetof(SDL_Vertex, position) == 0 && offsetof(SDL_Vertex, color) == 8 && offsetof(SDL_Vertex, tex_coord) == 24);
            for (const auto &attribute : RendererShaderLayouts::primitive_attributes) {
                gl.glEnableVertexAttribArray(attribute.location);
                gl.glVertexAttribPointer(attribute.location, GLint(attribute.components), GL_FLOAT, GL_FALSE,
                    RendererShaderLayouts::primitive_stride, reinterpret_cast<const void *>(std::size_t(attribute.offset)));
            }
            gl.glDisable(GL_DEPTH_TEST); gl.glDisable(GL_CULL_FACE); gl.glDisable(GL_DITHER);
            gl.glGenTextures(1, &whiteTexture); gl.glBindTexture(GL_TEXTURE_2D, whiteTexture);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            const Uint8 white[]{255, 255, 255, 255}; gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
            const std::string shaderRoot = directory + "/" + dialect + "/";
            if (std::ifstream(shaderRoot + "world.vert") && std::ifstream(shaderRoot + "world.frag") &&
                std::ifstream(shaderRoot + "cloud.frag") && std::ifstream(shaderRoot + "point.frag")) {
                worldProgram = materialProgram(shaderRoot + "world.vert", shaderRoot + "world.frag");
                cloudProgram = materialProgram(shaderRoot + "world.vert", shaderRoot + "cloud.frag");
                pointProgram = materialProgram(shaderRoot + "primitive.vert", shaderRoot + "point.frag").program;
                pointViewport = gl.glGetUniformLocation(pointProgram, "viewport");
                pointAlpha = gl.glGetUniformLocation(pointProgram, "alpha");
                supportsWorld = true;
            }
            if (std::ifstream(shaderRoot+"skin.vert") && std::ifstream(shaderRoot+"skin-rig.vert") && std::ifstream(shaderRoot+"skin.frag")) {
                skinProgram=loadSkin(shaderRoot+"skin.vert",shaderRoot+"skin.frag");
                rigProgram=loadSkin(shaderRoot+"skin-rig.vert",shaderRoot+"skin.frag"); supportsSkins=true;
            }
            if(std::ifstream(shaderRoot+"terrain-compose.frag") && std::ifstream(shaderRoot+"terrain-reduce.frag")){
                composeProgram=terrainProgram(shaderRoot+"terrain-compose.vert",shaderRoot+"terrain-compose.frag",false);
                reduceProgram=terrainProgram(shaderRoot+"terrain-reduce.vert",shaderRoot+"terrain-reduce.frag",true);supportsTerrain=true;
            }
            SDL_GL_SetSwapInterval(0); screen();
        } catch (...) { cleanup(); throw; }
    }
    ~ProgrammableGLDevice() override { cleanup(); }
    SDL_GLContext borrowedContext() const { return context; }
    RenderDeviceCapabilities capabilities() const override { return {true, true, false, maxTexture, supportsWorld, supportsWorld, supportsSkins,supportsTerrain}; }
    bool canBeginFrame() override
    {
        current(); poll();
        if (frameOpen || pending.size() < 8) return true;
#ifdef __EMSCRIPTEN__
        return false; // Event-loop progress is required; never busy-wait WebGL.
#else
        while (pending.size() == 8) {
            const auto result = gl.glClientWaitSync(pending.front().fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000);
            if (result == GL_WAIT_FAILED) throw std::runtime_error("GL frame admission fence failed");
            poll();
        }
        trim(); return true;
#endif
    }
    RenderSubmission submit(RenderFrame frame) override
    {
        detail::RenderCpuTimer timing(counts.cpuSubmitNanoseconds);
        current(); poll(); trim();
        if (!frameOpen && !canBeginFrame()) return {};
        if (frame.logicalWidth <= 0 || frame.logicalHeight <= 0 || frame.packets.size() > RenderFrame::CommandCapacity)
            throw std::invalid_argument("Invalid GL frame");
        std::size_t vertices = 0;
        for (const auto &packet : frame.packets) {
            if(packet.kind==DrawPacket::Kind::WeightedCompose)validateCompose(packet);
            if (packet.vertices.size() > RenderFrame::VertexCapacity - vertices) throw std::length_error("GL frame geometry capacity");
            vertices += packet.vertices.size();
            if (packet.kind == DrawPacket::Kind::MeshTriangles &&
                ((packet.material == DrawPacket::Material::Skin || packet.material == DrawPacket::Material::SkinRig) ? !supportsSkins : !supportsWorld)) throw std::invalid_argument("World materials unavailable");
            if (packet.vertices.size() % 3) throw std::invalid_argument("Incomplete GL triangles");
            if (packet.texture && packet.target && key(packet.texture) == key(packet.target)) throw std::invalid_argument("GL target cannot sample itself");
            if (packet.kind == DrawPacket::Kind::Blit && !packet.texture) throw std::invalid_argument("GL blit without texture");
            const auto &t = packet.transform;
            if (!std::isfinite(t.scale) || t.scale <= 0 || !std::isfinite(t.x) || !std::isfinite(t.y)) throw std::invalid_argument("Invalid GL transform");
        }
        screen();
        if(!frameOpen){
            gl.glDisable(GL_SCISSOR_TEST);gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,screenFramebuffer);gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,committedFramebuffer);
            gl.glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            check("Retain completed renderer output");frameOpen=true;
        }
        std::vector<SDL_Vertex> geometry; geometry.reserve(std::min(vertices + frame.packets.size() * 6, RenderFrame::VertexCapacity));
        for (std::size_t begin = 0; begin < frame.packets.size();) {
            const auto &first = frame.packets[begin]; const auto end = frame.batchEnd(begin);
            if (first.kind == DrawPacket::Kind::Barrier) { gl.glFlush(); ++begin; continue; }
            if(first.kind==DrawPacket::Kind::WeightedCompose){drawCompose(first);++counts.packets;++begin;continue;}
            if (first.kind == DrawPacket::Kind::Clear || first.kind == DrawPacket::Kind::MeshTriangles) {
                drawWorld(first, frame.logicalWidth, frame.logicalHeight); ++counts.packets; ++begin; continue;
            }
            if (first.target) target(first.target);
            bool flip = false, premultiplied = false;
            if (first.texture) { auto &allocation = texture(first.texture, first.linear); flip = allocation.framebuffer != 0; premultiplied = allocation.premultiplied; }
            geometry.clear();
            for (auto index = begin; index < end; ++index) {
                const auto &packet = frame.packets[index];
                const auto count = packet.kind == DrawPacket::Kind::Fill || packet.kind == DrawPacket::Kind::Blit ? 6 : packet.vertices.size();
                if (count > RenderFrame::VertexCapacity - geometry.size()) { draw(first, geometry, frame.logicalWidth, frame.logicalHeight); geometry.clear(); }
                appendVertices(geometry, packet, flip, premultiplied); ++counts.packets;
            }
            if (!geometry.empty()) draw(first, geometry, frame.logicalWidth, frame.logicalHeight);
            begin = end;
        }
        check("Submit renderer frame");
        frameOpen = true;
        return {++issued, epoch};
    }
    RenderCompletion completion(RenderSubmission submission) override
    {
        current(); poll();
        if (!submission || submission.deviceGeneration != epoch || submission.identity > issued) return RenderCompletion::Cancelled;
        return submission.identity <= completed ? RenderCompletion::Complete : RenderCompletion::Pending;
    }
    void present() override
    {
        detail::RenderCpuTimer timing(counts.cpuPresentNanoseconds);
        current(); screen();
        gl.glDisable(GL_SCISSOR_TEST); gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, screenFramebuffer); gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl.glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        check("Present renderer frame");
        if (frameOpen) {
            GLsync fence = gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (!fence) throw std::runtime_error("Cannot create renderer fence");
            pending.push_back({{issued, epoch}, fence}); frameOpen = false; gl.glFlush();
        }
        if (!SDL_GL_SwapWindow(window)) throw std::runtime_error(SDL_GetError());
        poll(); trim();
    }
    void discardFrame() override
    {
        current();screen();
        if(!frameOpen)return;
        gl.glDisable(GL_SCISSOR_TEST);gl.glBindFramebuffer(GL_READ_FRAMEBUFFER,committedFramebuffer);gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER,screenFramebuffer);
        gl.glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        const auto fence=gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        if(!fence)throw std::runtime_error("Cannot fence deferred renderer frame");
        pending.push_back({{++issued,epoch},fence});frameOpen=false;gl.glFlush();poll();
        check("Discard renderer frame");
    }
    void reset() override
    {
        current(); cancelCaptures();
        // GL guarantees deleted storage remains live for already issued commands.
        // Submitted snapshots are released only when cancelling this generation.
        for (auto &value : pending) gl.glDeleteSync(value.fence);
        pending.clear();
        for (auto &[key, value] : textures) destroy(value);
        textures.clear();
        for (auto &[key, value] : targets) destroy(value);
        targets.clear();
        for (auto &[key, value] : meshes) destroyMesh(value);
        meshes.clear();
        for(auto &[key,value]:masks)destroy(value);
        masks.clear();
        for(auto &[key,value]:scratchTargets)destroy(value);
        scratchTargets.clear();
        ++epoch; completed = issued; frameOpen = false; counts.resourceBytes = 0;
    }
    std::uint64_t generation() const override { return epoch; }
    void trimRetiredResources() override
    {
        current(); poll(); trim(0, 0, true, true);
        for (auto it = meshes.begin(); it != meshes.end();) {
            if (it->second.lastUse <= completed) {
                destroyMesh(it->second); it = meshes.erase(it);
            } else ++it;
        }
    }
    RenderTelemetry telemetry() const override
    {
        auto result = counts;
        result.resourceBytes = meshBytes + textureBytes + std::size_t(width) * height * (screenDepth ? 12 : 8) + vertexBytes + 4;
        for (const auto &capture : readbacks) result.resourceBytes += std::size_t(capture.request.width) * capture.request.height * 4;
        return result;
    }
    SDL_Rect captureRegion(CaptureRequest &request, GLuint &framebuffer, int &targetHeight)
    {
        int targetWidth=width; targetHeight=height; framebuffer=screenFramebuffer;
        if(request.target) { auto &allocation=target(request.target); allocation.lastUse=issued; framebuffer=allocation.framebuffer; targetWidth=request.target->descriptor.width; targetHeight=request.target->descriptor.height; }
        const auto region=request.pixelRegion.value_or(SDL_Rect{0,0,targetWidth,targetHeight});
        if(region.x<0 || region.y<0 || region.w<=0 || region.h<=0 || region.w>targetWidth || region.h>targetHeight ||
           region.x>targetWidth-region.w || region.y>targetHeight-region.h ||
           (request.width && request.width!=region.w) || (request.height && request.height!=region.h))
            throw std::invalid_argument("GL capture region outside target");
        request.width=region.w; request.height=region.h; return region;
    }
    bool capture(CaptureRequest request) override
    {
        detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
        current(); screen();
        if (readbacks.size() == 3) { ++counts.captureDrops; return false; }
        GLuint framebuffer; int targetHeight; SDL_Rect region;
        try { region=captureRegion(request,framebuffer,targetHeight); } catch(const std::invalid_argument &) { return false; }
        const std::size_t bytes=std::size_t(region.w)*region.h*4;
        if(bytes>TextureBudget-textureBytes-captureBytes){++counts.captureDrops;return false;}
        Readback readback; readback.request = std::move(request);
        gl.glGenBuffers(1, &readback.buffer); gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, readback.buffer);
        gl.glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(region.w) * region.h * 4, nullptr, GL_STREAM_READ);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer); gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.glReadPixels(region.x, targetHeight-region.y-region.h, region.w, region.h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        readback.fence = gl.glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); gl.glFlush();
        if (!readback.fence) { gl.glDeleteBuffers(1, &readback.buffer); throw std::runtime_error("Cannot create capture fence"); }
        readbacks.push_back(std::move(readback)); captureBytes+=bytes; check("Capture admission"); return true;
    }
    bool pollCapture(CaptureResult &result) override
    {
        detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
        current(); if (readbacks.empty()) return false;
        auto &readback = readbacks.front(); const auto status = gl.glClientWaitSync(readback.fence, 0, 0);
        if (status == GL_TIMEOUT_EXPIRED) return false;
        if (status == GL_WAIT_FAILED) throw std::runtime_error("GL capture fence failed");
        CaptureResult captured; captured.request = readback.request;
        captured.pixels.reset(SDL_CreateSurface(readback.request.width, readback.request.height, SDL_PIXELFORMAT_RGBA32));
        if (!captured.pixels) throw std::runtime_error(SDL_GetError());
        const std::size_t bytes = std::size_t(readback.request.width) * readback.request.height * 4;
        gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, readback.buffer);
#ifdef __EMSCRIPTEN__
        copyProgrammableGLReadback(captured.pixels->pixels, int(bytes));
#else
        const void *mapped = gl.glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(bytes), GL_MAP_READ_BIT);
        if (!mapped) throw std::runtime_error("Cannot map completed GL capture");
        for (int y = 0; y < captured.pixels->h; ++y)
            std::memcpy(static_cast<Uint8 *>(captured.pixels->pixels) + y * captured.pixels->pitch,
                static_cast<const Uint8 *>(mapped) + std::size_t(y) * captured.pixels->w * 4, std::size_t(captured.pixels->w) * 4);
        if (!gl.glUnmapBuffer(GL_PIXEL_PACK_BUFFER)) throw std::runtime_error("GL capture buffer corrupt");
#endif
        gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); flipRows(captured.pixels.get());
        counts.captureLatencyNanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-readback.admitted).count();
        ++counts.captureCompletions;
        gl.glDeleteSync(readback.fence); gl.glDeleteBuffers(1, &readback.buffer); captureBytes-=bytes; readbacks.pop_front();
        result = std::move(captured); check("Capture completion"); return true;
    }
    void cancelCaptures() override
    {
        current(); for (auto &readback : readbacks) { gl.glDeleteSync(readback.fence); gl.glDeleteBuffers(1, &readback.buffer); }
        counts.captureDrops += readbacks.size(); readbacks.clear(); captureBytes=0;
    }
    SDL_Surface *captureSynchronously(CaptureRequest request) override
    {
        detail::RenderCpuTimer timing(counts.cpuCaptureNanoseconds);
        current(); screen();
        GLuint framebuffer; int targetHeight; const auto region=captureRegion(request,framebuffer,targetHeight);
        auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(SDL_CreateSurface(region.w, region.h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!surface) throw std::runtime_error(SDL_GetError());
        gl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer); gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.glReadPixels(region.x, targetHeight-region.y-region.h, region.w, region.h, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels); check("Synchronous capture");
        flipRows(surface.get()); return surface.release();
    }
    void outputSize(int &w, int &h) override { current(); screen(); w = width; h = height; }
};
} // namespace
bool programmableGLCompiled() { return true; }
std::unique_ptr<RenderDevice> makeProgrammableGLDevice(SDL_Window *window, const std::string &directory)
{
    return std::make_unique<ProgrammableGLDevice>(window, directory);
}
SDL_GLContext programmableGLContext(const RenderDevice &device)
{
    const auto *gl = dynamic_cast<const ProgrammableGLDevice *>(&device); return gl ? gl->borrowedContext() : nullptr;
}
} // namespace GAGCore

#else
namespace GAGCore
{
bool programmableGLCompiled() { return false; }
std::unique_ptr<RenderDevice> makeProgrammableGLDevice(SDL_Window *, const std::string &)
{ throw std::runtime_error("Programmable GL was not compiled for this platform"); }
SDL_GLContext programmableGLContext(const RenderDevice &) { return nullptr; }
}
#endif
