// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <SkinMesh.h>
#include <iostream>
#include <cstring>

#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
namespace GAGCore
{
namespace
{
constexpr int TargetSize = 256;
constexpr float Padding = 1.25f;
unsigned shader(unsigned type, const char *source)
{
    const unsigned id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    int good = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &good);
    if (!good) { glDeleteShader(id); return 0; }
    return id;
}
}
void GraphicContext::destroySkinRenderer()
{
    auto &r = skinResources;
    if (r.program) glDeleteProgram(r.program);
    if (r.framebuffer) glDeleteFramebuffersEXT(1, &r.framebuffer);
    if (r.depth) glDeleteRenderbuffersEXT(1, &r.depth);
    if (r.color) glDeleteTextures(1, &r.color);
    for (auto buffer : {r.poses, r.uv, r.indices})
        if (buffer) glDeleteBuffers(1, &buffer);
    r = {};
}

bool GraphicContext::drawSkinMesh(const SkinMesh &mesh, unsigned frame, DrawableSurface &texture,
                                 float x, float y, float w, float h)
{
    if (!context || renderer || glState.isTextureSRectangle || !mesh.identity || frame >= mesh.frames || mesh.poses.empty() || !texture.sdlsurface)
        return false;
    const auto *extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!extensions || !std::strstr(extensions, "GL_EXT_framebuffer_object")) return false;
    // This feasibility renderer intentionally keeps the original painter order.
    // Production batching must amortize targets across identical poses/skins.
    Sprite::flushBatches(this);
    if (texture.glUploadedRevision != texture.contentRevision()) texture.uploadToTexture();
    if (!texture.texture) return false;
    auto &r = skinResources;
    if (r.attempted && !r.program) return false;
    GLint framebuffer = 0, renderbuffer = 0, arrayBuffer = 0, elementBuffer = 0, program = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    auto restore = [&] {
        glBindFramebufferEXT(GL_FRAMEBUFFER, framebuffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER, renderbuffer);
        glUseProgram(program);
        glPopClientAttrib();
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBuffer);
        glPopAttrib();
    };
    if (!r.attempted)
    {
        r.attempted = true;
        const auto vertex = shader(GL_VERTEX_SHADER,
            "#version 120\n"
            "varying vec2 uv; varying vec3 normal;\n"
            "void main(){uv=gl_MultiTexCoord0.xy; normal=gl_Normal;"
            "gl_Position=vec4(gl_Vertex.xy/1.25,gl_Vertex.z,1.0);}\n");
        const auto fragment = shader(GL_FRAGMENT_SHADER,
            "#version 120\n"
            "uniform sampler2D paint; varying vec2 uv; varying vec3 normal;\n"
            "void main(){float light=0.45+0.55*max(0.0,dot(normalize(normal),"
            "normalize(vec3(-0.4,0.7,1.0))));"
            "gl_FragColor=vec4(texture2D(paint,uv).rgb*light,1.0);}\n");
        if (vertex && fragment)
        {
            r.program = glCreateProgram();
            glAttachShader(r.program, vertex); glAttachShader(r.program, fragment);
            glLinkProgram(r.program);
            int good = 0; glGetProgramiv(r.program, GL_LINK_STATUS, &good);
            if (!good) { glDeleteProgram(r.program); r.program = 0; }
        }
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (!r.program)
        {
            restore();
            std::cerr << "Skin mesh shader unavailable; keeping classic sprites\n";
            return false;
        }
        glGenFramebuffersEXT(1, &r.framebuffer);
        glGenRenderbuffersEXT(1, &r.depth);
        glGenTextures(1, &r.color);
        glBindTexture(GL_TEXTURE_2D, r.color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TargetSize, TargetSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebufferEXT(GL_FRAMEBUFFER, r.framebuffer);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.color, 0);
        glBindRenderbufferEXT(GL_RENDERBUFFER, r.depth);
        glRenderbufferStorageEXT(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, TargetSize, TargetSize);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r.depth);
        if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            destroySkinRenderer(); r.attempted = true;
            restore(); return false;
        }
        glGenBuffers(1, &r.poses); glGenBuffers(1, &r.uv); glGenBuffers(1, &r.indices);
    }
    if (r.meshIdentity != mesh.identity)
    {
        glBindBuffer(GL_ARRAY_BUFFER, r.uv);
        glBufferData(GL_ARRAY_BUFFER, mesh.uv.size()*sizeof(float), mesh.uv.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size()*sizeof(std::uint32_t), mesh.indices.data(), GL_STATIC_DRAW);
        r.meshIdentity = mesh.identity;
        r.frame = ~0u;
    }
    if (r.frame != frame)
    {
        glBindBuffer(GL_ARRAY_BUFFER, r.poses);
        glBufferData(GL_ARRAY_BUFFER, mesh.vertices*6*sizeof(float),
                     mesh.poses.data()+std::size_t(frame)*mesh.vertices*6, GL_STREAM_DRAW);
        r.frame = frame;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER, r.framebuffer);
    glViewport(0, 0, TargetSize, TargetSize);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 0); glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(r.program);
    glUniform1i(glGetUniformLocation(r.program, "paint"), 0);
    glBindTexture(GL_TEXTURE_2D, texture.texture);
    glBindBuffer(GL_ARRAY_BUFFER, r.poses);
    const std::uintptr_t offset = 0;
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 6*sizeof(float), reinterpret_cast<void*>(offset));
    glNormalPointer(GL_FLOAT, 6*sizeof(float), reinterpret_cast<void*>(offset + 3*sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, r.uv);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r.indices);
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
    restore();

    // Composite the live target with the exact map transform, scissor and painter
    // order that the classic sprite would use. Model depth never affects the map.
    glUseProgram(0);
    glState.doTexture(true); glState.setTexture(r.color); glState.doBlend(true);
    glState.blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const float padX = w*(Padding-1)/2, padY = h*(Padding-1)/2;
    x -= padX; y -= padY; w *= Padding; h *= Padding;
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,1); glVertex2f(x,y);
    glTexCoord2f(1,1); glVertex2f(x+w,y);
    glTexCoord2f(1,0); glVertex2f(x+w,y+h);
    glTexCoord2f(0,0); glVertex2f(x,y+h);
    glEnd();
    glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawCalls += 2;
    return true;
}
}
#else
namespace GAGCore
{
void GraphicContext::destroySkinRenderer() { skinResources = {}; }
bool GraphicContext::drawSkinMesh(const SkinMesh &, unsigned, DrawableSurface &, float, float, float, float) { return false; }
}
#endif
