// SPDX-License-Identifier: GPL-3.0-or-later

#include "TorusView.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "TorusGeometry.h"
#include <GraphicContext.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#else
#include <epoxy/gl.h>
#endif
#endif

namespace
{
const float pi = 3.14159265358979323846f;
float clamp(float x, float a, float b) { return std::max(a, std::min(b, x)); }
float smooth(float x)
{
    x = clamp(x, 0, 1);
    return x * x * (3 - 2 * x);
}
float mix(float a, float b, float t) { return a + (b - a) * t; }
#ifdef HAVE_OPENGL
struct SkyPoint
{
    float x, y, z, brightness;
    int size;
};
// Deterministic visual-only randomness, independent of simulation state.
const std::vector<SkyPoint> &skyPoints(bool haze)
{
    static std::vector<SkyPoint> stars, clouds;
    auto &points = haze ? clouds : stars;
    if (!points.empty())
        return points;
    unsigned seed = haze ? 81991u : 1729u;
    auto random = [&]()
    {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) / 16777216.0f;
    };
    for (int i = 0; i < (haze ? 1700 : 5200); ++i)
    {
        float longitude = random() * 2 * pi;
        float latitude = haze ? (random() + random() + random() - 1.5f) * 0.10f : random() * 2 - 1;
        float radius = std::sqrt(std::max(0.0f, 1 - latitude * latitude));
        float x = radius * std::cos(longitude), y = latitude, z = radius * std::sin(longitude);
        points.push_back({x * 0.82f - y * 0.572f, x * 0.572f + y * 0.82f, z, random(),
                          i % 29 == 0 ? 2 : (i % 5 == 0 ? 1 : 0)});
    }
    return points;
}
void drawSky(float yaw, float pitch, float fade, float sx, float sy, float distance, int width, int height,
             int renderWidth)
{
    glUseProgram(0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_POINT_SMOOTH);
    auto project = [&](const SkyPoint &p, float &x, float &y)
    {
        TorusGeometry::Point screen;
        if (!TorusGeometry::projectSkyDirection({p.x, p.y, p.z}, {yaw, pitch}, fade, sx, sy, distance,
                                                screen))
            return false;
        x = width * 0.5f + screen.x;
        y = (height + 16) * 0.5f + screen.y;
        return x > -80 && x < renderWidth + 80 && y > -80 && y < height + 80;
    };
    glPointSize(48);
    glBegin(GL_POINTS);
    for (const auto &p : skyPoints(true))
    {
        float x, y;
        if (project(p, x, y))
        {
            glColor4f(0.28f, 0.33f, 0.48f, fade * (0.003f + p.brightness * 0.008f));
            glVertex2f(x, y);
        }
    }
    glEnd();
    for (int size = 0; size < 3; ++size)
    {
        glPointSize(size == 0 ? 1 : (size == 1 ? 1.7f : 2.6f));
        glBegin(GL_POINTS);
        for (const auto &p : skyPoints(false))
        {
            float x, y;
            if (p.size == size && project(p, x, y))
            {
                float warm = p.brightness;
                glColor4f(mix(0.65f, 1.0f, warm), mix(0.78f, 0.90f, warm), mix(1.0f, 0.72f, warm),
                          fade * (0.20f + p.brightness * 0.65f));
                glVertex2f(x, y);
            }
        }
        glEnd();
    }
    glDisable(GL_POINT_SMOOTH);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
}
GLuint createMaterial()
{
    const char *vertex =
        "#version 120\n"
        "varying vec2 uv; varying vec3 light; varying vec3 normal;\n"
        "uniform vec2 mapOffset;\n"
        "void main(){gl_Position=ftransform();uv=gl_MultiTexCoord0.xy+mapOffset;light=gl_Color.rgb;normal=gl_Normal;}\n";
    // A sun off to the left: its highlight lies on the ring's left flank, where
    // the surface normal bisects the sun and the eye, never on the front face.
    const char *fragment = "#version 120\n"
                           "uniform sampler2D world; uniform vec3 sunHalf; uniform float specular;\n"
                           "varying vec2 uv; varying vec3 light; varying vec3 normal;\n"
                           "void main(){\n"
                           "  float s = pow(max(dot(normalize(normal), sunHalf), 0.0), 36.0) * specular;\n"
                           "  gl_FragColor=vec4(texture2D(world,uv).rgb*light + vec3(1.0, 0.96, 0.85) * s, 1.0);}\n";
    GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &vertex, 0);
    glCompileShader(vs);
    glShaderSource(fs, 1, &fragment, 0);
    glCompileShader(fs);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint okay = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &okay);
    if (!okay)
    {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), 0, log);
        fprintf(stderr, "Torus material: %s\n", log);
        glDeleteProgram(program);
        program = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}
#endif
} // namespace

void TorusView::releaseResources()
{
#ifdef HAVE_OPENGL
    if (graphicsContext && graphicsContext == SDL_GL_GetCurrentContext() &&
        graphicsGeneration == globalContainer->gfx->getGLContextGeneration())
    {
        if (meshBuffer)
            glDeleteBuffers(1, &meshBuffer);
        if (cloudBuffer)
            glDeleteBuffers(1, &cloudBuffer);
        if (indexBuffer)
            glDeleteBuffers(1, &indexBuffer);
        if (material)
            glDeleteProgram(material);
        if (texture)
            glDeleteTextures(1, &texture);
        if (cloudTexture)
            glDeleteTextures(1, &cloudTexture);
        if (framebuffer)
            glDeleteFramebuffers(1, &framebuffer);
    }
#endif
    graphicsContext = nullptr;
    texture = cloudTexture = framebuffer = material = meshBuffer = cloudBuffer = indexBuffer = 0;
    atlasW = atlasH = 0;
    cloudW = cloudH = 0;
    vertices.clear();
    cloudVertices.clear();
    cachedPickX = cachedPickY = -1;
    cachedPickFound = false;
    pickWidth = pickHeight = 0;
}

bool TorusView::prepareRenderTarget()
{
#ifdef HAVE_OPENGL
    // Resolution/fullscreen changes can replace SDL's GL context. Object names
    // belong to their creating context; never delete or reuse them in another.
    if (graphicsContext != SDL_GL_GetCurrentContext() ||
        graphicsGeneration != globalContainer->gfx->getGLContextGeneration())
    {
        releaseResources();
        graphicsContext = SDL_GL_GetCurrentContext();
        graphicsGeneration = globalContainer->gfx->getGLContextGeneration();
        failed = false;
    }
    GLint maximumTexture = 0, maximumViewport[2] = {0, 0};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTexture);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maximumViewport);
    int nextW = std::min(worldW * 32, std::min(8192, std::min(maximumTexture, maximumViewport[0])));
    int nextH = std::min(worldH * 32, std::min(8192, std::min(maximumTexture, maximumViewport[1])));
    if (!texture || atlasW != nextW || atlasH != nextH)
    {
        if (texture)
            glDeleteTextures(1, &texture);
        if (framebuffer)
            glDeleteFramebuffers(1, &framebuffer);
        atlasW = nextW;
        atlasH = nextH;
        glPushAttrib(GL_TEXTURE_BIT);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            target = false;
            amount = 0;
            failed = true;
            fprintf(stderr, "Torus view: offscreen framebuffer is unavailable\n");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPopAttrib();
        if (!failed && !material)
            material = createMaterial();
        if (!material)
            failed = true;
    }
    return !failed;
#else
    return false;
#endif
}

// The cloud layer lives on its own ring above the ground, sampled from the
// same world-anchored field as the shadows the atlas already carries.
void TorusView::updateClouds()
{
#ifdef HAVE_OPENGL
    int gridW, gridH;
    clouds.computeWorld(worldW, worldH, SDL_GetTicks64() / 40, cloudPixels, gridW, gridH);
    glPushAttrib(GL_TEXTURE_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    if (!cloudTexture || gridW != cloudW || gridH != cloudH)
    {
        if (cloudTexture)
            glDeleteTextures(1, &cloudTexture);
        cloudW = gridW;
        cloudH = gridH;
        glGenTextures(1, &cloudTexture);
        glBindTexture(GL_TEXTURE_2D, cloudTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, cloudW, cloudH, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
                     &cloudPixels[0]);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, cloudTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cloudW, cloudH, GL_ALPHA, GL_UNSIGNED_BYTE, &cloudPixels[0]);
    }
    glPopClientAttrib();
    glPopAttrib();
#endif
}

bool TorusView::draw(Game &game, int team, unsigned options, int &vx, int &vy, int width, int height)
{
#ifdef HAVE_OPENGL
    if (!active() || !available())
    {
        reset();
        return false;
    }
    Uint32 now = SDL_GetTicks();
    float dt = std::min(0.1f, float(now - lastFrame) / 1000.0f);
    lastFrame = now;
    if (!amount && target)
    {
        // Put the current viewport center on the front of the torus. Preserve
        // its sub-tile offset, so even the first/last frame matches normal 2D.
        TorusGeometry::MapFocus focus =
            TorusGeometry::mapFocus(game.map.getW(), game.map.getH(), vx, vy, width, height);
        if (!worldW || !worldH)
        {
            originX = focus.originX;
            originY = focus.originY;
        }
        focusU = (((vx - originX) & game.map.getMaskW()) + width / 64.0f) / game.map.getW();
        focusV = (((vy - originY) & game.map.getMaskH()) + (height + 16) / 64.0f) / game.map.getH();
        baseViewportX = vx;
        baseViewportY = vy;
        worldW = game.map.getW();
        worldH = game.map.getH();
        travelU = travelV = cameraU = cameraV = 0;
    }
    // Enter and leave the overview deliberately, with the same gentle transition.
    amount = clamp(amount + (target ? dt : -dt) / 1.8f, 0, 1);
    // The ordinary map and minimap track the same destination as the torus.
    // Ease the sub-tile remainder away while returning to the tile-based 2D camera.
    if (!target)
    {
        float settle = amount == 0 ? 1 : 1 - std::exp(-16 * dt);
        travelU = mix(travelU, std::round(travelU * worldW) / worldW, settle);
        travelV = mix(travelV, std::round(travelV * worldH) / worldH, settle);
    }
    // Input remains the logical map destination. Smooth the rendered focus for
    // the surface and picking; the sky shares only its horizontal movement.
    cameraU = TorusGeometry::follow(cameraU, travelU, dt, true);
    cameraV = TorusGeometry::follow(cameraV, travelV, dt, true);
    cameraZoom = TorusGeometry::follow(cameraZoom, zoom, dt);
    if (amount == 0)
    {
        cameraU = travelU;
        cameraV = travelV;
    }
    auto destination =
        TorusGeometry::destination(baseViewportX, baseViewportY, travelU, travelV, worldW, worldH);
    vx = destination.x;
    vy = destination.y;
    auto gfx = globalContainer->gfx;
    gfx->setClipRect();
    GLint oldViewport[4], oldMatrixMode, oldProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetIntegerv(GL_MATRIX_MODE, &oldMatrixMode);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    if (!prepareRenderTarget())
    {
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(oldMatrixMode);
        target = false;
        amount = 0;
        vertices.clear();
        pickWidth = pickHeight = 0;
        return false;
    }
    // Keep terrain, units, clouds and pointer previews on the normal render cadence.
    // The whole texture is redrawn: no scissor, program or depth test may be left
    // over from the ring, whatever the driver restored.
    {
        glUseProgram(0);
        glActiveTexture(GL_TEXTURE0);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, atlasW, atlasH);
        glOrtho(0, game.map.getW() * 32, game.map.getH() * 32, 0, -1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // Capture the normal map and cloud shadows,
        // respecting the same graphics-quality setting as the 2D view.
        game.drawMap(0, 0, game.map.getW() * 32, game.map.getH() * 32, 0, 0, originX, originY, team,
                     options | Game::DRAW_NO_CLOUD_LAYER);
        if (game.gui)
            game.gui->drawTorusMapOverlay(originX, originY);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    const bool drawClouds =
        (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0;
    if (drawClouds)
        updateClouds();

    // Save GL state AFTER the game renderer: its state cache must still match
    // the restored state when the ordinary HUD resumes drawing.
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glEnable(GL_SCISSOR_TEST);
    // The sidebar is translucent: render beneath it, while retaining the
    // playable-area camera center and input bounds. The scissor box is in
    // framebuffer pixels: the viewport the 2D view draws through maps the
    // logical size onto them, whatever the window system reports.
    glScissor(oldViewport[0], oldViewport[1], oldViewport[2], (height - 16) * oldViewport[3] / gfx->getH());
    glClearColor(0.025f, 0.037f, 0.06f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, gfx->getW(), gfx->getH(), 0, -10000, 10000);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float pull = smooth(amount);
    float roll = smooth(amount);
    float aspect = float(game.map.getW()) / game.map.getH();
    // One direct, restrained pullback. There is no intermediate zoom to a
    // distant full-map sheet, then zoom back in to the torus.
    float anchorU = focusU + cameraU, anchorV = focusV + cameraV;
    // The ring keeps one attitude on screen: the surface slides around the
    // tube as the view pans, and turns about the axis as it scrolls.
    const float ringV = 0.5f;
    float cameraDistance = TorusGeometry::hoverDistance(ringV);
    viewAspect = float(width) / std::max(1, height - 16);
    // The folded ring sits centred in the view; the flat map keeps its focus there.
    // The ring's silhouette is measured once per view shape in camera units.
    if (ringAspect != viewAspect)
    {
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (int j = 0; j <= 40; ++j)
            for (int i = 0; i <= 40; ++i)
            {
                auto p = TorusGeometry::overviewPoint(i / 40.0f - 0.5f, j / 40.0f - 0.5f, 1, ringV, viewAspect);
                float w = 1 - p.z / cameraDistance;
                minX = std::min(minX, p.x / w);
                maxX = std::max(maxX, p.x / w);
                minY = std::min(minY, p.y / w);
                maxY = std::max(maxY, p.y / w);
            }
        ringCentreX = (minX + maxX) * 0.5f;
        ringCentreY = (minY + maxY) * 0.5f;
        ringWidth = maxX - minX;
        ringHeight = maxY - minY;
        ringAspect = viewAspect;
    }
    float scale = 0.9f * std::min(width / ringWidth, (height - 16) / ringHeight) * mix(1, cameraZoom, roll);
    float sx = std::exp(mix(std::log(game.map.getW() * 32 / (8 * pi)), std::log(scale), pull));
    float sy = sx * TorusGeometry::verticalScale(focusU, focusV, roll, aspect);
    float cx = width * 0.5f - ringCentreX * scale * smooth(roll);
    float cy = (height + 16) * 0.5f - ringCentreY * scale * smooth(roll);
    float major = smooth(roll), minor = smooth(roll / 0.85f);
    float skyYaw = -(anchorU - 0.5f) * 2 * pi, pa = TorusGeometry::latitude(ringV, aspect);
    float viewPitch = pa + TorusGeometry::overviewTilt(ringV, roll, viewAspect);
    // Vertical navigation rolls the map around the tube, without pitching the
    // sky. Its tilt follows only the explicit transition and window shape.
    drawSky(skyYaw, viewPitch, roll, sx, sy, cameraDistance, width, height, gfx->getW());
    if (material)
    {
        glUseProgram(material);
        glUniform1i(glGetUniformLocation(material, "world"), 0);
        glUniform2f(glGetUniformLocation(material, "mapOffset"), anchorU, 1 - anchorV);
        // A low sun far to the left and a little above; the eye looks along +z.
        // The highlight sits where the normal bisects the two, on the left flank.
        float lx = -1.0f, ly = 0.25f, lz = 0.15f;
        const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
        lx /= ll, ly /= ll, lz = lz / ll + 1;
        const float hl = std::sqrt(lx * lx + ly * ly + lz * lz);
        glUniform3f(glGetUniformLocation(material, "sunHalf"), lx / hl, ly / hl, lz / hl);
        glUniform1f(glGetUniformLocation(material, "specular"), 0.18f * roll);
    }
    const int U = meshColumns, V = meshRows;
    using MeshVertex = TorusPicking::Vertex;
    pickU = anchorU;
    pickV = anchorV;
    pickWidth = width;
    pickHeight = height;
    float key[8] = {roll, ringV, sx, sy, cx, cy, scale, cameraDistance};
    GLint oldArrayBuffer, oldIndexBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &oldIndexBuffer);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    if (!meshBuffer || std::memcmp(key, meshKey, sizeof(key)) != 0)
    {
        if (!meshBuffer)
            glGenBuffers(1, &meshBuffer);
        if (!cloudBuffer)
            glGenBuffers(1, &cloudBuffer);
        cachedPickX = cachedPickY = -1;
        vertices.resize((U + 1) * (V + 1));
        cloudVertices.resize(vertices.size());
        // The cloud ring floats above the ground by a fixed share of the tube
        // radius; it settles onto the flat map as the fold opens.
        const float cloudHeight = 0.039f * roll, e = 0.001f;
        for (int j = 0; j <= V; ++j)
            for (int i = 0; i <= U; ++i)
            {
                float du = float(i) / U - 0.5f, dv = float(j) / V - 0.5f;
                auto p = TorusGeometry::overviewPoint(du, dv, roll, ringV, viewAspect);
                auto pu = TorusGeometry::overviewPoint(du + e, dv, roll, ringV, viewAspect);
                auto pv = TorusGeometry::overviewPoint(du, dv + e, roll, ringV, viewAspect);
                TorusGeometry::Point tu = TorusGeometry::subtract(pu, p), tv = TorusGeometry::subtract(pv, p);
                TorusGeometry::Point n = {tu.y * tv.z - tu.z * tv.y, tu.z * tv.x - tu.x * tv.z,
                                          tu.x * tv.y - tu.y * tv.x};
                float len = std::max(1e-12f, TorusGeometry::length(n));
                TorusGeometry::Point c = {p.x + n.x / len * cloudHeight, p.y + n.y / len * cloudHeight,
                                          p.z + n.z / len * cloudHeight};
                float a = du * 2 * pi * major, b = pa + dv * 2 * pi * minor;
                float nx = std::sin(a) * std::cos(b), ny = std::sin(b), nz = std::cos(a) * std::cos(b);
                float ry = ny * std::cos(viewPitch) - nz * std::sin(viewPitch);
                float rz = ny * std::sin(viewPitch) + nz * std::cos(viewPitch);
                float light =
                    mix(1, 0.48f + 0.52f * clamp(-nx * 0.35f - ry * 0.45f + rz * 0.82f, 0, 1), roll);
                float w = 1 - p.z * roll / cameraDistance;
                vertices[j * (U + 1) + i] = {{cx * w + p.x * sx, cy * w + p.y * sy, p.z * scale, w},
                                             {light, light, light},
                                             {du, -dv},
                                             {nx, ry, rz}};
                float cw = 1 - c.z * roll / cameraDistance;
                cloudVertices[j * (U + 1) + i] = {{cx * cw + c.x * sx, cy * cw + c.y * sy, c.z * scale, cw},
                                                  {light, light, light},
                                                  {du, -dv},
                                                  {nx, ry, rz}};
            }
        glBindBuffer(GL_ARRAY_BUFFER, meshBuffer);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(MeshVertex), vertices.data(),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, cloudBuffer);
        glBufferData(GL_ARRAY_BUFFER, cloudVertices.size() * sizeof(MeshVertex), cloudVertices.data(),
                     GL_DYNAMIC_DRAW);
        std::memcpy(meshKey, key, sizeof(key));
    }
    if (!indexBuffer)
    {
        std::vector<unsigned> indices;
        indices.reserve(U * V * 6);
        for (int j = 0; j < V; ++j)
            for (int i = 0; i < U; ++i)
            {
                unsigned a = j * (U + 1) + i, b = a + U + 1;
                indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        glGenBuffers(1, &indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned), indices.data(),
                     GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, meshBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(4, GL_FLOAT, sizeof(MeshVertex),
                    reinterpret_cast<void *>(offsetof(MeshVertex, position)));
    glColorPointer(3, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, color)));
    glTexCoordPointer(2, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, uv)));
    glNormalPointer(GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, normal)));
    // Both navigation axes only change texture offsets. Unfolding or resizing
    // rebuilds the shared surface and cloud geometry.
    glDrawElements(GL_TRIANGLES, U * V * 6, GL_UNSIGNED_INT, nullptr);
    if (drawClouds && cloudTexture)
    {
        // White clouds lit like the ground, blended over it without writing depth.
        glUseProgram(0);
        glBindTexture(GL_TEXTURE_2D, cloudTexture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glMatrixMode(GL_TEXTURE);
        glPushMatrix();
        glLoadIdentity();
        glTranslatef(anchorU + float(originX) / worldW + 0.5f / cloudW,
                     anchorV + float(originY) / worldH + 0.5f / cloudH, 0);
        glScalef(1, -1, 1);
        glBindBuffer(GL_ARRAY_BUFFER, cloudBuffer);
        glVertexPointer(4, GL_FLOAT, sizeof(MeshVertex),
                        reinterpret_cast<void *>(offsetof(MeshVertex, position)));
        glColorPointer(3, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, color)));
        glTexCoordPointer(2, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, uv)));
        glDrawElements(GL_TRIANGLES, U * V * 6, GL_UNSIGNED_INT, nullptr);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    glPopClientAttrib();
    glBindBuffer(GL_ARRAY_BUFFER, oldArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oldIndexBuffer);
    glUseProgram(oldProgram);
    glPopAttrib();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(oldMatrixMode);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    return true;
#else
    return false;
#endif
}
