// SPDX-License-Identifier: GPL-3.0-or-later

#include <PerformanceTelemetry.h>
#include "TorusView.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "TorusGeometry.h"
#include <GraphicContext.h>
#include <RenderStateScope.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#if defined(HAVE_OPENGL)
#define GLOB2_TORUS_OPENGL
#endif

#ifdef GLOB2_TORUS_OPENGL
#if defined(GLOB2_WEBGL2)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#elif defined(__APPLE__)
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
const int cloudGridLimit = 128;
const float pi = 3.14159265358979323846f;
float clamp(float x, float a, float b) { return std::max(a, std::min(b, x)); }
float smooth(float x)
{
    x = clamp(x, 0, 1);
    return x * x * (3 - 2 * x);
}
float mix(float a, float b, float t) { return a + (b - a) * t; }
#ifdef GLOB2_TORUS_OPENGL
#ifdef GLOB2_WEBGL2
// GLSL ES 1.00 has no version line. Emscripten's legacy-GL bridge rewrites
// ftransform() and the gl_* vertex inputs, and adds the fragment precision.
#define TORUS_GLSL_VERSION ""
// WebGL has no attribute stacks and the bridge does not emulate them. Save
// exactly what each torus block changes, so the 2D renderer's GL state cache
// still matches when the HUD resumes drawing.
struct TextureState
{
    GLint texture = 0, alignment = 1;
    TextureState()
    {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    }
    void restore() const
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    }
};
void setCapability(GLenum capability, bool enabled)
{
    if (enabled)
        glEnable(capability);
    else
        glDisable(capability);
}
struct RingState
{
    bool scissor = glIsEnabled(GL_SCISSOR_TEST), depth = glIsEnabled(GL_DEPTH_TEST),
         blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE;
    GLint scissorBox[4] = {}, depthFunc = GL_LESS, blendFactors[4] = {}, textureEnvironment = GL_MODULATE;
    GLfloat clearColor[4] = {};
    TextureState texture;
    RingState()
    {
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendFactors[0]);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendFactors[1]);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendFactors[2]);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendFactors[3]);
        glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &textureEnvironment);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    }
    void restore() const
    {
        setCapability(GL_SCISSOR_TEST, scissor);
        setCapability(GL_DEPTH_TEST, depth);
        setCapability(GL_BLEND, blend);
        setCapability(GL_CULL_FACE, cull);
        glDepthMask(depthMask);
        glDepthFunc(depthFunc);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glBlendFuncSeparate(blendFactors[0], blendFactors[1], blendFactors[2], blendFactors[3]);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, textureEnvironment);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        // The bridge cannot report point size or current color; return them to
        // their defaults.
        glPointSize(1);
        glColor4f(1, 1, 1, 1);
        texture.restore();
    }
};
// The bridge draws emulated client-array elements as GL_UNSIGNED_SHORT,
// whatever type is requested; the torus mesh fits in 16 bits.
using MeshIndex = GLushort;
const GLenum meshIndexType = GL_UNSIGNED_SHORT;
#else
#define TORUS_GLSL_VERSION "#version 120\n"
using MeshIndex = GLuint;
const GLenum meshIndexType = GL_UNSIGNED_INT;
#endif
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
        TORUS_GLSL_VERSION
        "varying vec2 uv; varying vec3 light; varying vec3 normal;\n"
        "uniform vec2 mapOffset;\n"
        "void main(){gl_Position=ftransform();uv=gl_MultiTexCoord0.xy+mapOffset;light=gl_Color.rgb;normal=gl_Normal;}\n";
    // A sun off to the left: its highlight lies on the ring's left flank, where
    // the surface normal bisects the sun and the eye, never on the front face.
    const char *fragment = TORUS_GLSL_VERSION
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
#ifdef GLOB2_TORUS_OPENGL
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
        for (auto &tile : tiles)
            glDeleteTextures(1, &tile.texture);
        if (tileBuffer)
            glDeleteBuffers(1, &tileBuffer);
        if (cloudTexture)
            glDeleteTextures(1, &cloudTexture);
        if (framebuffer)
            glDeleteFramebuffers(1, &framebuffer);
    }
#endif
    graphicsContext = nullptr;
    cloudTexture = framebuffer = material = meshBuffer = cloudBuffer = indexBuffer = tileBuffer = 0;
    tiles.clear();
    pixelsPerCell = 32;
    tileMeshDirty = true;
    cloudW = cloudH = 0;
    vertices.clear();
    cloudVertices.clear();
    cachedPickX = cachedPickY = -1;
    cachedPickFound = false;
    pickWidth = pickHeight = 0;
}

bool TorusView::prepareRenderTarget()
{
#ifdef GLOB2_TORUS_OPENGL
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
    const int limit = std::min({textureLimit, maximumTexture, maximumViewport[0], maximumViewport[1]});
    if (tiles.empty() && !failed)
    {
#ifdef GLOB2_WEBGL2
        const TextureState textureState;
#else
        glPushAttrib(GL_TEXTURE_BIT);
#endif
        GLint oldFramebuffer;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFramebuffer);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        for (pixelsPerCell = 32; pixelsPerCell >= 1; pixelsPerCell /= 2)
        {
            auto candidate = TorusTextureTiles::layout(worldW * 32, worldH * 32, limit, pixelsPerCell);
            bool okay = !candidate.empty();
            size_t allocatedPixels = 0;
            for (auto &tile : candidate)
            {
                if (!okay) break;
                allocatedPixels += size_t(tile.textureW) * tile.textureH;
                if (allocationPixelLimit && allocatedPixels > size_t(allocationPixelLimit))
                { okay = false; break; }
                glGenTextures(1, &tile.texture);
                glBindTexture(GL_TEXTURE_2D, tile.texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tile.textureW, tile.textureH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                GLenum error = glGetError();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.texture, 0);
                okay = error == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                // Consume allocation errors so a successful retry leaves clean GL state.
                for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
            }
            if (okay)
            {
                tiles = std::move(candidate);
                tileMeshDirty = true;
                if (pixelsPerCell < 32)
                    fprintf(stderr, "Torus view: allocation pressure reduced capture to %d pixels per map cell\n", pixelsPerCell);
                break;
            }
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
            for (auto &tile : candidate)
                if (tile.texture) glDeleteTextures(1, &tile.texture);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, oldFramebuffer);
#ifdef GLOB2_WEBGL2
        textureState.restore();
#else
        glPopAttrib();
#endif
        if (tiles.empty())
        {
            failed = true;
            fprintf(stderr, "Torus view: full and reduced-resolution render targets unavailable\n");
        }
        if (!failed && !material) material = createMaterial();
        if (!material) failed = true;
        if (failed)
        {
            for (auto &tile : tiles) glDeleteTextures(1, &tile.texture);
            tiles.clear();
            glDeleteFramebuffers(1, &framebuffer);
            framebuffer = 0;
        }
    }
    return !failed;
#else
    return false;
#endif
}

// The cloud layer lives on its own ring above the ground, sampled from the
// same world-anchored field as the shadows the atlas already carries.
void TorusView::updateClouds(int time)
{
#ifdef GLOB2_TORUS_OPENGL
    int gridW, gridH;
    clouds.computeWorld(worldW, worldH, time, cloudPixels, gridW, gridH, cloudGridLimit);
#ifdef GLOB2_WEBGL2
    const TextureState textureState;
#else
    glPushAttrib(GL_TEXTURE_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
#endif
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
#ifdef GLOB2_WEBGL2
    textureState.restore();
#else
    glPopClientAttrib();
    glPopAttrib();
#endif
#endif
}

bool TorusView::draw(Game &game, int team, unsigned options, int &vx, int &vy, int width, int height, float flatZoom, float fractionX, float fractionY)
{
	PERF_SCOPE_TIME(Torus);
#ifdef GLOB2_TORUS_OPENGL
    if (!active() || !available())
    {
        reset();
        return false;
    }
    Uint32 now = SDL_GetTicks();
    float dt = std::min(0.1f, float(now - lastFrame) / 1000.0f);
    lastFrame = now;
    const bool automatic = globalContainer->settings.automaticTorus;
    // Movement only reports tile changes, so a held pan that pauses looks
    // identical to a finished one. Hold the overview out until the button goes up.
    if (!automatic || (!panHeld && now - lastMove > 250))
        moving = false;
    const bool pullBack = target || moving;
    if (!pullBack && amount == 0)
    {
        reset();
        return false;
    }
    if (!amount && pullBack)
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
        focusU = (((vx - originX) & game.map.getMaskW()) + fractionX / 32.0f + width / (64.0f * flatZoom)) / game.map.getW();
        focusV = (((vy - originY) & game.map.getMaskH()) + fractionY / 32.0f + (height + 16) / (64.0f * flatZoom)) / game.map.getH();
        baseViewportX = vx;
        baseViewportY = vy;
        worldW = game.map.getW();
        worldH = game.map.getH();
        travelU = travelV = cameraU = cameraV = 0;
    }
    // Manual switching is symmetric. Automatic movement unfolds gradually,
    // then returns quickly; pointer gestures keep their current projection.
    const float pace = automatic && !target ? (moving ? 4.0f : 0.45f) : 1.8f;
    if (!pointerHeld)
        amount = clamp(amount + (pullBack ? dt : -dt) / pace, 0, 1);
    // The ordinary map and minimap track the same destination as the torus.
    // Ease the sub-tile remainder away while returning to the tile-based 2D camera.
    if (!pullBack && !pointerHeld)
    {
        float settle = amount == 0 ? 1 : 1 - std::exp(-16 * dt);
        travelU = mix(travelU, std::round(travelU * worldW) / worldW, settle);
        travelV = mix(travelV, std::round(travelV * worldH) / worldH, settle);
    }
    // Input remains the logical map destination. Smooth the rendered focus for
    // the surface and picking; the sky shares only its horizontal movement.
    cameraU = TorusGeometry::follow(cameraU, travelU, dt, true);
    cameraV = TorusGeometry::follow(cameraV, travelV, dt, true);
    if (amount == 0)
    {
        cameraU = travelU;
        cameraV = travelV;
    }
    auto destination =
        TorusGeometry::destination(baseViewportX, baseViewportY, travelU, travelV, worldW, worldH);
    vx = destination.x;
    vy = destination.y;
    float pull = smooth(amount);
    float roll = smooth(amount);
    float aspect = float(game.map.getW()) / game.map.getH();
    const TorusGeometry::Shape shape(aspect);
    float anchorU = focusU + cameraU, anchorV = focusV + cameraV;
    // The ring keeps one attitude on screen: the surface slides around the
    // tube as the view pans, and turns about the axis as it scrolls.
    viewAspect = float(width) / std::max(1, height - 16);
    const float ringV = TorusGeometry::overviewLatitude(viewAspect, shape);
    float cameraDistance = TorusGeometry::hoverDistance(ringV);
    // Keep the focused landscape at screen center throughout the transition.
    // The ring's silhouette is measured once per view shape in camera units,
    // with how far the folded surface spreads the map at its focus.
    if (ringAspect != viewAspect || ringMapAspect != aspect)
    {
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        const auto project = [&](float du, float dv)
        {
            auto p = TorusGeometry::overviewPoint(du, dv, 1, ringV, viewAspect, shape);
            float w = 1 - p.z / cameraDistance;
            return TorusGeometry::Point{p.x / w, p.y / w, 0};
        };
        for (int j = 0; j <= 40; ++j)
            for (int i = 0; i <= 40; ++i)
            {
                auto p = project(i / 40.0f - 0.5f, TorusGeometry::meshOffset(j / 40.0f, ringV, shape));
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
        ringWidth = maxX - minX;
        ringHeight = maxY - minY;
        focusGain = (project(0.001f, 0).x - project(-0.001f, 0).x) / 0.002f;
        ringAspect = viewAspect;
        ringMapAspect = aspect;
    }
    // The ring shows its focus at the 2D camera's zoom: folding the map
    // neither zooms in nor out, and the wheel means the same in both views.
    // Only an automatic reveal pulls back, to the whole ring.
    const float mapPixels = game.map.getW() * 32.0f;
    const float fit = 0.9f * std::min(width / ringWidth, (height - 16) / ringHeight);
    float scale = mapPixels * flatZoom / focusGain;
    // Fully zoomed out the whole ring is in view. The flat view of a very
    // wide map stops zooming out before its ring would fit, so that ring is
    // drawn smaller throughout by what it lacks there.
    const float minimumZoom = std::min(1.0f, std::max(width / mapPixels, height / (game.map.getH() * 32.0f)));
    scale *= std::min(1.0f, fit * focusGain / (mapPixels * minimumZoom));
    if (wholeRing)
        scale = std::min(scale, fit);
    float sx = std::exp(mix(std::log(mapPixels * flatZoom / (8 * pi)), std::log(scale), pull));
    float sy = sx * TorusGeometry::verticalScale(focusU, focusV, roll, aspect);
    // The zoom the focus is seen at this frame decides how the map is drawn:
    // zoomed out, the ring carries the same strategic overview as the 2D view.
    const float shownZoom = std::exp(mix(std::log(flatZoom), std::log(scale * focusGain / mapPixels), pull));
    auto gfx = globalContainer->gfx;
    Sprite::flushBatches(gfx);
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
        target = moving = false;
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
        GLint oldFramebuffer;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        Game::ViewState mapView;
        if (!game.gui) mapView.render = std::move(standaloneRender);
        Game::ViewState &captureView = game.gui ? game.gui->view : mapView;
        const Scene *previousScene = captureView.scene;
        const unsigned captureOptions = options | Game::DRAW_NO_CLOUD_LAYER | Game::DRAW_TILED_CAPTURE;
        game.prepareMapCapture(team, captureView, captureOptions, game.gui && game.gui->gamePaused);
        captureView.scene = previousScene ? previousScene : &captureView.render.ownScene;
        if (game.gui) game.gui->toolManager.setDrawnScene(captureView.scene);
        bool advancePreviews = true;
        for (const auto &tile : tiles)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tile.texture, 0);
            glViewport(0, 0, tile.textureW, tile.textureH);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, (tile.w + 64) * shownZoom, (tile.h + 64) * shownZoom, 0, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glClear(GL_COLOR_BUFFER_BIT);
            // Use the shared zoom to choose sprites and strategic markers, while
            // the inverse projection/target scale keeps one texel per world
            // pixel at native resolution (raster scale * map zoom == 1).
            gfx->setRenderTargetScale(float(pixelsPerCell) / (32 * shownZoom));
            const int reach = 30000;
            GAGCore::MapTransformScope mapPass(*gfx, shownZoom, 0, 0,
                SDL_Rect{0, gfx->getH() - reach, reach, reach});
            glDisable(GL_SCISSOR_TEST);
            const int tx = (originX + tile.x / 32 - 1) & game.map.getMaskW();
            const int ty = (originY + tile.y / 32 - 1) & game.map.getMaskH();
            if (game.gui)
                game.gui->drawTorusMap(tx, ty, tile.w + 64, tile.h + 64, team, captureOptions, cloudGridLimit, advancePreviews);
            else
                game.drawMap(0, 0, tile.w + 64, tile.h + 64, 0, 0, tx, ty, team,
                             captureView, captureOptions, nullptr, nullptr, false, cloudGridLimit, true);
            Sprite::flushBatches(gfx);
            advancePreviews = false;
        }
        Game::finishMapCapture(captureView, captureOptions, game.gui && game.gui->gamePaused);
        captureView.scene = previousScene;
        if (!game.gui) standaloneRender = std::move(mapView.render);
        gfx->setRenderTargetScale(0);
        gfx->setClipRect();
        // Capture disables scissoring directly while the 2D cache remembers it
        // enabled. Restore that agreement before saving state for the ring/HUD.
        glEnable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFramebuffer);
    }
    const bool drawClouds =
        globalContainer->settings.clouds;
    if (drawClouds)
        updateClouds(game.gui ? game.gui->mapAnimationTime() : standaloneRender.animationTime);

    // Save GL state AFTER the game renderer: its state cache must still match
    // the restored state when the ordinary HUD resumes drawing.
#ifdef GLOB2_WEBGL2
    const RingState ringState;
#else
    glPushAttrib(GL_ALL_ATTRIB_BITS);
#endif
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
    glBindTexture(GL_TEXTURE_2D, tiles.front().texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, gfx->getW(), gfx->getH(), 0, -10000, 10000);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float cx = width * 0.5f;
    float cy = (height + 16) * 0.5f;
    float skyYaw = -(anchorU - 0.5f) * 2 * pi, pa = TorusGeometry::latitude(ringV, shape);
    float viewPitch = pa + TorusGeometry::overviewTilt(ringV, roll, viewAspect, shape);
    // Vertical navigation rolls the map around the tube, without pitching the
    // sky. Its tilt follows only the explicit transition and window shape.
    drawSky(skyYaw, viewPitch, roll, sx, sy, cameraDistance, width, height, gfx->getW());
    if (material)
    {
        glUseProgram(material);
        glUniform1i(glGetUniformLocation(material, "world"), 0);
        glUniform2f(glGetUniformLocation(material, "mapOffset"), 0, 0);
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
#ifdef GLOB2_WEBGL2
    static_assert((meshColumns + 1) * (meshRows + 1) <= 65536, "WebGL torus indices are 16-bit");
#endif
    using MeshVertex = TorusPicking::Vertex;
    pickU = anchorU;
    pickV = anchorV;
    pickWidth = width;
    pickHeight = height;
    // Depth only orders the surface; it must stay inside the projection's range at any zoom.
    const float depthScale = 100;
    float key[9] = {roll, ringV, sx, sy, cx, cy, depthScale, cameraDistance, aspect};
    GLint oldArrayBuffer, oldIndexBuffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &oldIndexBuffer);
#ifndef GLOB2_WEBGL2
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
#endif
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
        // Keep clouds outside the ground and inside the aperture on very tall maps.
        const float cloudHeight = std::min(0.039f * shape.tubeRadius,
                                          0.25f * (shape.majorRadius - shape.tubeRadius)) * roll;
        const float e = 0.0001f;
        for (int j = 0; j <= V; ++j)
            for (int i = 0; i <= U; ++i)
            {
                float du = float(i) / U - 0.5f;
                float dv = TorusGeometry::meshOffset(float(j) / V, ringV, shape);
                auto p = TorusGeometry::overviewPoint(du, dv, roll, ringV, viewAspect, shape);
                auto pu = TorusGeometry::overviewPoint(du + e, dv, roll, ringV, viewAspect, shape);
                auto pv = TorusGeometry::overviewPoint(du, dv + e, roll, ringV, viewAspect, shape);
                TorusGeometry::Point tu = TorusGeometry::subtract(pu, p), tv = TorusGeometry::subtract(pv, p);
                TorusGeometry::Point n = {tu.y * tv.z - tu.z * tv.y, tu.z * tv.x - tu.x * tv.z,
                                          tu.x * tv.y - tu.y * tv.x};
                float len = std::max(1e-12f, TorusGeometry::length(n));
                TorusGeometry::Point c = {p.x + n.x / len * cloudHeight, p.y + n.y / len * cloudHeight,
                                          p.z + n.z / len * cloudHeight};
                float nx = n.x / len, ry = n.y / len, rz = n.z / len;
                float light =
                    mix(1, 0.48f + 0.52f * clamp(-nx * 0.35f - ry * 0.45f + rz * 0.82f, 0, 1), roll);
                float w = 1 - p.z * roll / cameraDistance;
                vertices[j * (U + 1) + i] = {{cx * w + p.x * sx, cy * w + p.y * sy, p.z * depthScale, w},
                                             {light, light, light},
                                             {du, -dv},
                                             {nx, ry, rz}};
                float cw = 1 - c.z * roll / cameraDistance;
                cloudVertices[j * (U + 1) + i] = {{cx * cw + c.x * sx, cy * cw + c.y * sy, c.z * depthScale, cw},
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
        tileMeshDirty = true;
    }
    if (!indexBuffer)
    {
        std::vector<MeshIndex> indices;
        indices.reserve(U * V * 6);
        for (int j = 0; j < V; ++j)
            for (int i = 0; i < U; ++i)
            {
                const MeshIndex a = MeshIndex(j * (U + 1) + i), b = MeshIndex(a + U + 1);
                indices.insert(indices.end(), {a, b, MeshIndex(a + 1), MeshIndex(a + 1), b, MeshIndex(b + 1)});
            }
        glGenBuffers(1, &indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(MeshIndex), indices.data(),
                     GL_STATIC_DRAW);
    }
    if (tileMeshDirty || tileOffsetU != anchorU || tileOffsetV != 1 - anchorV)
    {
        auto partitioned = TorusTextureTiles::partition(tiles, vertices, U, V, worldW * 32, worldH * 32,
                                                       anchorU, 1 - anchorV);
        if (!tileBuffer) glGenBuffers(1, &tileBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, tileBuffer);
        glBufferData(GL_ARRAY_BUFFER, partitioned.size() * sizeof(MeshVertex), partitioned.data(), GL_DYNAMIC_DRAW);
        tileOffsetU = anchorU; tileOffsetV = 1 - anchorV;
        tileMeshDirty = false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, tileBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(4, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, position)));
    glColorPointer(3, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, color)));
    glTexCoordPointer(2, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, uv)));
    glNormalPointer(GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, normal)));
    for (const auto &tile : tiles)
    {
        glBindTexture(GL_TEXTURE_2D, tile.texture);
        glDrawArrays(GL_TRIANGLES, tile.first, tile.count);
    }
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
        glNormalPointer(GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, normal)));
        glDrawElements(GL_TRIANGLES, U * V * 6, meshIndexType, nullptr);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
#ifdef GLOB2_WEBGL2
    // The bridge has no client attribute stacks; Glob2's other batched paths
    // leave client arrays disabled too (see AlphaMapRender).
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
#else
    glPopClientAttrib();
#endif
    glBindBuffer(GL_ARRAY_BUFFER, oldArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oldIndexBuffer);
    glUseProgram(oldProgram);
#ifdef GLOB2_WEBGL2
    ringState.restore();
#else
    glPopAttrib();
#endif
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
