// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <SkinMesh.h>
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cstring>
#include <string>

#ifdef HAVE_OPENGL
// The desktop compatibility context exposes the EXT framebuffer entry points.
#ifndef GLOB2_WEBGL2
#undef glGenFramebuffers
#define glGenFramebuffers glGenFramebuffersEXT
#undef glDeleteFramebuffers
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#undef glBindFramebuffer
#define glBindFramebuffer glBindFramebufferEXT
#undef glFramebufferTexture2D
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#undef glCheckFramebufferStatus
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#undef glGenRenderbuffers
#define glGenRenderbuffers glGenRenderbuffersEXT
#undef glDeleteRenderbuffers
#define glDeleteRenderbuffers glDeleteRenderbuffersEXT
#undef glBindRenderbuffer
#define glBindRenderbuffer glBindRenderbufferEXT
#undef glRenderbufferStorage
#define glRenderbufferStorage glRenderbufferStorageEXT
#undef glFramebufferRenderbuffer
#define glFramebufferRenderbuffer glFramebufferRenderbufferEXT
#endif
namespace GAGCore
{
namespace
{
constexpr unsigned TileSize = 128, AtlasSize = 2048;
constexpr unsigned Columns = AtlasSize / TileSize, SlotsPerPage = Columns * Columns;
constexpr unsigned MaxPages = 4, MaxSlots = SkinAtlasCache::Capacity;
constexpr float Padding = 1.25f;
// Shared verbatim with the web designer preview (MeshPreview.tsx); keep the
// marked block identical. Normals are camera space; the camera is orthographic.
const char *const SkinMaterialGLSL = R"GLSL(
// BEGIN skin-material
float skinHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float skinNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 s = f * f * (3.0 - 2.0 * f);
  return mix(mix(skinHash(i), skinHash(i + vec2(1.0, 0.0)), s.x),
             mix(skinHash(i + vec2(0.0, 1.0)), skinHash(i + vec2(1.0, 1.0)), s.x), s.y);
}
// The original glob material bumps its normals with Stucci noise (norfac 5).
float skinStucci(vec2 p) { return skinNoise(p) + 0.5 * skinNoise(p * 2.03 + 17.0); }
vec3 skinBump(vec3 n, vec2 uv, float amount, float frequency) {
  float e = 0.25 / frequency;
  float h = skinStucci(uv * frequency);
  float gu = (skinStucci((uv + vec2(e, 0.0)) * frequency) - h) / e;
  float gv = (skinStucci((uv + vec2(0.0, e)) * frequency) - h) / e;
  // Express the UV height gradient in screen directions, independent of resolution.
  vec2 du = vec2(dFdx(uv.x), dFdy(uv.x));
  vec2 dv = vec2(dFdx(uv.y), dFdy(uv.y));
  float density = max(0.5 * (length(du) + length(dv)), 1e-6);
  vec2 g = amount * (gu * du + gv * dv) / density;
  // Stretched UV regions exaggerate the gradient; keep the tilt bounded.
  g *= min(1.0, 0.7 / max(length(g), 1e-6));
  return normalize(n - vec3(g, 0.0));
}
// Classic glossy: the original glob material's bumped body and broad white
// streaks (specular 0.5, hardness 2), lit like the classic sprites.
vec3 skinShade(vec3 albedo, vec3 surfaceNormal, vec2 uv) {
  vec3 n = skinBump(normalize(surfaceNormal), uv, 0.08, 12.0);
  vec3 l = normalize(vec3(-0.4, 0.7, 1.0));
  vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
  float diffuse = max(0.0, dot(n, l));
  float nh = max(0.0, dot(n, h));
  return albedo * (0.42 + 0.8 * diffuse) + vec3(0.75 * pow(nh, 4.0));
}
// END skin-material
)GLSL";
GLuint compileShader(GLenum type, const std::string &text)
{
    const char *source = text.c_str();
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(shader); return 0; }
    return shader;
}
struct SkinGLState
{
    GLint framebuffer, renderbuffer, program, arrayBuffer, elementBuffer;
#ifdef GLOB2_WEBGL2
    GLint vao, activeTexture, texture, viewport[4], depthFunction, scissorBox[4];
    GLfloat clearColor[4], clearDepth;
    GLboolean depthMask, colorMask[4], blend, depth, scissor, cull;
#endif
    SkinGLState()
    {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
#ifdef GLOB2_WEBGL2
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        blend=glIsEnabled(GL_BLEND); depth=glIsEnabled(GL_DEPTH_TEST);
        scissor=glIsEnabled(GL_SCISSOR_TEST); cull=glIsEnabled(GL_CULL_FACE);
#else
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        glActiveTexture(GL_TEXTURE0);
        glClientActiveTexture(GL_TEXTURE0);
#endif
    }
    ~SkinGLState()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glUseProgram(program);
#ifdef GLOB2_WEBGL2
        glBindVertexArray(vao);
#else
        glPopClientAttrib();
#endif
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBuffer);
#ifdef GLOB2_WEBGL2
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(activeTexture);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glDepthFunc(depthFunction);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        glClearDepthf(clearDepth); glDepthMask(depthMask);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        auto enable=[](GLenum cap, GLboolean on){ if(on)glEnable(cap);else glDisable(cap); };
        enable(GL_BLEND,blend); enable(GL_DEPTH_TEST,depth);
        enable(GL_SCISSOR_TEST,scissor); enable(GL_CULL_FACE,cull);
#else
        glPopAttrib();
#endif
    }
};
auto keyFor(const SkinMeshRequest &request)
{
    return std::make_tuple(request.mesh->identity, request.frame,
        request.texture->lifetimeIdentity(), request.texture->contentRevision());
}
bool valid(const SkinMeshRequest &request)
{
    return request.mesh && request.texture && request.mesh->identity &&
        request.frame < request.mesh->frames && !request.mesh->poses.empty();
}
}
void GraphicContext::destroySkinRenderer()
{
    auto &r = skinResources;
    if (r.program) glDeleteProgram(r.program);
    if (r.framebuffer) glDeleteFramebuffers(1, &r.framebuffer);
    if (r.depth) glDeleteRenderbuffers(1, &r.depth);
    for (auto color : r.colors) glDeleteTextures(1, &color);
#ifdef GLOB2_WEBGL2
    if (r.vao) glDeleteVertexArrays(1, &r.vao);
#endif
    for (auto buffer : {r.poses, r.uv, r.indices}) if (buffer) glDeleteBuffers(1, &buffer);
    r = {};
}
void GraphicContext::prepareSkinMeshes(const std::vector<SkinMeshRequest> &requests)
{
    PERF_SCOPE_TIME(SkinPrepare);
    auto &r = skinResources;
    if (!context || renderer || requests.empty() || (r.attempted && !r.program)) return;
#ifndef GLOB2_WEBGL2
    if (glState.isTextureSRectangle) return;
    const auto *extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!extensions || !std::strstr(extensions, "GL_EXT_framebuffer_object")) return;
#endif
    // Sort by mesh and pose so team variants reuse geometry uploads. Wrapped map
    // copies and identical units share one live rasterization for this frame.
    std::map<SkinResources::Key, SkinMeshRequest> unique;
    for (const auto &request : requests)
        if (valid(request) && request.texture->sdlsurface) unique.emplace(keyFor(request), request);
    if (unique.empty()) return;
    // Keep the working set bounded; protect all visible hits before replacing
    // old tiles. Overflow draws take the same eviction path on demand.
    while (unique.size() > MaxSlots) unique.erase(std::prev(unique.end()));
    for (auto it = unique.begin(); it != unique.end(); )
        if (r.slots.touch(it->first)) it = unique.erase(it); else ++it;
    if (unique.empty()) return;
    Sprite::flushBatches(this);
    for (const auto &[key, request] : unique)
        if (request.texture->glUploadedRevision != request.texture->contentRevision())
            request.texture->uploadToTexture();
    SkinGLState saved;
    if (!r.attempted)
    {
        r.attempted = true;
        GLint limit = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (limit < static_cast<int>(AtlasSize)) return;
#ifdef GLOB2_WEBGL2
        const auto vertex = compileShader(GL_VERTEX_SHADER,
            "#version 300 es\nprecision highp float;\n"
            "layout(location=0) in vec3 position;layout(location=1) in vec3 surfaceNormal;layout(location=2) in vec2 texcoord;\n"
            "out vec2 uv;out vec3 normal;void main(){uv=texcoord;normal=surfaceNormal;gl_Position=vec4(position.xy/1.25,position.z,1.0);}\n");
        const auto fragment = compileShader(GL_FRAGMENT_SHADER,
            std::string("#version 300 es\nprecision highp float;\n")+
            "uniform sampler2D paint;in vec2 uv;in vec3 normal;out vec4 color;\n"
            + std::string(SkinMaterialGLSL) +
            "void main(){color=vec4(skinShade(texture(paint,uv).rgb,normal,uv),1.0);}\n");
#else
        const auto vertex = compileShader(GL_VERTEX_SHADER,
            "#version 120\nvarying vec2 uv;varying vec3 normal;\n"
            "void main(){uv=gl_MultiTexCoord0.xy;normal=gl_Normal;gl_Position=vec4(gl_Vertex.xy/1.25,gl_Vertex.z,1.0);}\n");
        const auto fragment = compileShader(GL_FRAGMENT_SHADER,
            std::string("#version 120\nuniform sampler2D paint;varying vec2 uv;varying vec3 normal;\n")
            + std::string(SkinMaterialGLSL) +
            "void main(){gl_FragColor=vec4(skinShade(texture2D(paint,uv).rgb,normal,uv),1.0);}\n");
#endif
        if (vertex && fragment)
        {
            r.program = glCreateProgram();
            glAttachShader(r.program, vertex); glAttachShader(r.program, fragment);
            glLinkProgram(r.program);
            GLint ok = 0; glGetProgramiv(r.program, GL_LINK_STATUS, &ok);
            if (!ok) { glDeleteProgram(r.program); r.program = 0; }
        }
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (!r.program) return;
        glGenFramebuffers(1, &r.framebuffer); glGenRenderbuffers(1, &r.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, r.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, AtlasSize, AtlasSize);
        glGenBuffers(1, &r.poses); glGenBuffers(1, &r.indices);
#ifdef GLOB2_WEBGL2
        glGenVertexArrays(1, &r.vao);
#else
        glGenBuffers(1, &r.uv);
#endif
    }
    const unsigned pages = std::min(MaxPages, static_cast<unsigned>((r.slots.size()+unique.size()+SlotsPerPage-1)/SlotsPerPage));
    while (r.colors.size() < pages)
    {
        GLuint color = 0; glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, AtlasSize, AtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        r.colors.push_back(color);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, r.framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r.depth);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glClearColor(0,0,0,0);
#ifdef GLOB2_WEBGL2
    glClearDepthf(1); glBindVertexArray(r.vao);
#else
    glClearDepth(1);
#endif
    glUseProgram(r.program); glUniform1i(glGetUniformLocation(r.program, "paint"), 0);
    unsigned boundPage = ~0u;
    for (const auto &[key, request] : unique)
    {
        if (!request.texture->texture) continue;
        const unsigned slot = r.slots.reserve(key);
        const unsigned page = slot / SlotsPerPage;
        if (page != boundPage)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.colors[page], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            { destroySkinRenderer(); skinResources.attempted = true; return; }
            boundPage = page;
        }
        // Preserve other cached poses, including the transparent tile border.
        const unsigned tile = slot % SlotsPerPage;
        glEnable(GL_SCISSOR_TEST);
        glScissor((tile%Columns)*TileSize, (tile/Columns)*TileSize, TileSize, TileSize);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const auto &mesh = *request.mesh;
        PerformanceTelemetry::Scope geometryTime(PerformanceTelemetry::Id::SkinGeometry);
        if (r.meshIdentity != mesh.identity)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.indices);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size()*sizeof(std::uint32_t), mesh.indices.data(), GL_STATIC_DRAW);
#ifndef GLOB2_WEBGL2
            glBindBuffer(GL_ARRAY_BUFFER, r.uv);
            glBufferData(GL_ARRAY_BUFFER, mesh.uv.size()*sizeof(float), mesh.uv.data(), GL_STATIC_DRAW);
#endif
            r.meshIdentity = mesh.identity; r.frame = ~0u;
        }
        glBindBuffer(GL_ARRAY_BUFFER, r.poses);
        if (r.frame != request.frame)
        {
            const float *pose = mesh.poses.data()+std::size_t(request.frame)*mesh.vertices*6;
#ifdef GLOB2_WEBGL2
            // Emscripten's legacy VAO implementation supports a single vertex buffer.
            std::vector<float> vertices(std::size_t(mesh.vertices)*8);
            for (std::size_t v=0; v<mesh.vertices; ++v)
            {
                std::copy_n(pose+v*6, 6, vertices.data()+v*8);
                std::copy_n(mesh.uv.data()+v*2, 2, vertices.data()+v*8+6);
            }
            glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STREAM_DRAW);
#else
            glBufferData(GL_ARRAY_BUFFER, mesh.vertices*6*sizeof(float), pose, GL_STREAM_DRAW);
#endif
            r.frame = request.frame;
        }
#ifdef GLOB2_WEBGL2
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),nullptr);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<void*>(6*sizeof(float)));
#else
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY);
        glVertexPointer(3,GL_FLOAT,6*sizeof(float),nullptr);
        glNormalPointer(GL_FLOAT,6*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER,r.uv); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2,GL_FLOAT,0,nullptr);
#endif
        geometryTime.stop();
        PERF_SCOPE_TIME(SkinRaster);
        glViewport((tile%Columns)*TileSize, (tile/Columns)*TileSize, TileSize, TileSize);
        glBindTexture(GL_TEXTURE_2D, request.texture->texture);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.indices);
        glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
        ++drawCalls;
    }
}
bool GraphicContext::drawSkinMesh(const SkinMesh &mesh, unsigned frame, DrawableSurface &texture,
                                  float x, float y, float w, float h, DrawableSurface *underlay, Uint8 alpha)
{
    PERF_SCOPE_TIME(SkinComposite);
    const SkinMeshRequest request{&mesh, frame, &texture};
    if (!valid(request) || !texture.sdlsurface) return false;
    if (alpha == 0) return true;
    const auto key = keyFor(request);
    auto &r = skinResources;
    auto found = r.slots.find(key);
    if (!found)
    {
        // Standalone and overflow draws evict one least-recently-used tile.
        prepareSkinMeshes({request});
        found = r.slots.find(key);
        if (!found) return false;
    }
    // Draw the original ground shadow only after confirming the mesh can
    // render. A failed mesh draw must leave the classic fallback untouched.
    if (underlay) drawSurface(x, y, w, h, underlay, alpha);
    Sprite::flushBatches(this);
    const unsigned slot = *found, tile = slot % SlotsPerPage;
    const float u = float(tile%Columns)/Columns, v = float(tile/Columns)/Columns;
    const float extent = 1.f/Columns;
    glUseProgram(0); glState.doTexture(true); glState.setTexture(r.colors[slot/SlotsPerPage]);
    glState.doBlend(true); glState.blendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    x-=w*(Padding-1)/2; y-=h*(Padding-1)/2; w*=Padding; h*=Padding;
    // The atlas uses premultiplied alpha: fade RGB together with alpha.
    const float opacity = alpha / 255.f;
    glBegin(GL_QUADS);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u,v+extent); glVertex2f(x,y);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u+extent,v+extent); glVertex2f(x+w,y);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u+extent,v); glVertex2f(x+w,y+h);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u,v); glVertex2f(x,y+h);
    glEnd(); glColor4f(1,1,1,1); glState.blendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); ++drawCalls;
    return true;
}
}
#else
namespace GAGCore
{
void GraphicContext::destroySkinRenderer() { skinResources = {}; }
void GraphicContext::prepareSkinMeshes(const std::vector<SkinMeshRequest> &) {}
bool GraphicContext::drawSkinMesh(const SkinMesh &, unsigned, DrawableSurface &, float,float,float,float, DrawableSurface *, Uint8) { return false; }
}
#endif
