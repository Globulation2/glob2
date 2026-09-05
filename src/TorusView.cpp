// SPDX-License-Identifier: GPL-3.0-or-later

#include "TorusView.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include "FileManager.h"
#include <cstdlib>
#include <memory>
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
        "varying vec2 uv; varying vec3 light;\n"
        "uniform vec2 mapOffset;\n"
        "void main(){gl_Position=ftransform();uv=gl_MultiTexCoord0.xy+mapOffset;light=gl_Color.rgb;}\n";
    const char *fragment = "#version 120\n"
                           "uniform sampler2D world;\n"
                           "varying vec2 uv; varying vec3 light;\n"
                           "void main(){gl_FragColor=vec4(texture2D(world,uv).rgb*light,1.0);}\n";
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

TorusView::TorusView()
    : target(false), dragging(false), amount(0), zoom(1), travelU(0), travelV(0), surfaceScaleX(1),
      surfaceScaleY(1), baseViewportX(0), baseViewportY(0), worldW(0), worldH(0), atlasW(0), atlasH(0),
      lastFrame(0), clouds(&globalContainer->settings), texture(0), cloudTexture(0), framebuffer(0),
      material(0), meshBuffer(0), cloudBuffer(0), indexBuffer(0), meshKey{}, failed(false), originX(0),
      originY(0), focusU(0.5f), focusV(0.5f)
{
}
TorusView::~TorusView() { releaseResources(); }

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
    cachedPickX = cachedPickY = -1;
    cachedPickFound = false;
    pickWidth = pickHeight = 0;
}

void TorusView::reset()
{
    releaseResources();
    target = dragging = failed = false;
    amount = travelU = travelV = cameraU = cameraV = 0;
    worldW = worldH = 0;
    lastFrame = 0;
    resetCamera();
}

bool TorusView::available() const
{
#ifdef HAVE_OPENGL
    if (!globalContainer->gfx ||
        !(globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU) ||
        !SDL_GL_GetCurrentContext())
        return false;
    if (failed && graphicsGeneration == globalContainer->gfx->getGLContextGeneration())
        return false;
    int major = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
#ifdef __APPLE__
    return major >= 2 && SDL_GL_ExtensionSupported("GL_EXT_framebuffer_object");
#else
    return major >= 2 && (major >= 3 || SDL_GL_ExtensionSupported("GL_ARB_framebuffer_object"));
#endif
#else
    return false;
#endif
}
void TorusView::toggle()
{
    if (available())
    {
        if (!active())
            resetCamera();
        target = !target;
        dragging = false;
        lastFrame = SDL_GetTicks();
    }
}
void TorusView::resetCamera() { zoom = cameraZoom = 1; }
void TorusView::setHeld(bool down)
{
    if (!available())
        return;
    if (down && !active())
    {
        resetCamera();
        lastFrame = SDL_GetTicks();
    }
    held = down;
}

void TorusView::notifyMove()
{
    if (!available())
        return;
    if (!active())
    {
        resetCamera();
        lastFrame = SDL_GetTicks();
    }
    lastMove = SDL_GetTicks();
    moving = true;
}
void TorusView::setViewport(int x, int y)
{
    if (!worldW || !worldH || !active())
        return;
    auto current =
        TorusGeometry::destination(baseViewportX, baseViewportY, travelU, travelV, worldW, worldH);
    travelU += float(TorusGeometry::wrappedDelta(current.x, x, worldW)) / worldW;
    travelV += float(TorusGeometry::wrappedDelta(current.y, y, worldH)) / worldH;
    travelU -= std::floor(travelU);
    travelV -= std::floor(travelV);
}
bool TorusView::event(const SDL_Event &e, int width, int &vx, int &vy)
{
    if (!active())
        return false;
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_MIDDLE && dragging)
    {
        dragging = false;
        return true;
    }
    if (e.type == SDL_MOUSEMOTION)
    {
        if (!(e.motion.state & SDL_BUTTON_MMASK))
            dragging = false;
        if (dragging && target && worldW && worldH)
        {
            // Move the camera focus across the fixed world surface.
            auto movement = TorusGeometry::surfaceDrag(e.motion.xrel, e.motion.yrel, surfaceScaleX,
                                                       surfaceScaleY, smooth(amount), 0.5f);
            travelU += movement.x;
            travelV += movement.y;
            travelU -= std::floor(travelU);
            travelV -= std::floor(travelV);
            auto position =
                TorusGeometry::destination(baseViewportX, baseViewportY, travelU, travelV, worldW, worldH);
            vx = position.x;
            vy = position.y;
        }
        return dragging;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.x >= 0 && e.button.x < width && e.button.y >= 16)
    {
        if (e.button.button == SDL_BUTTON_MIDDLE)
        {
            dragging = true;
            return true;
        }
    }
    if (e.type == SDL_MOUSEWHEEL)
    {
        int x, y;
        SDL_GetMouseState(&x, &y);
        if (x >= 0 && x < width && y >= 16)
        {
            int direction = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
            if (target)
            {
                zoom = clamp(zoom * std::pow(1.12f, float(direction)), 0.4f, 2.0f);
                if (direction > 0 && zoom >= 2.0f)
                    toggle();
            }
            return true;
        }
    }
    return false;
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
    glPopAttrib();
#endif
}

// GLOB2_TORUS_DUMP=<seconds> writes the world texture, reduced 8 times, to
// torus-atlas.ppm in the user directory at that interval, and names the GL
// driver once, so a stale ring can be told from a stale texture.
void TorusView::dumpAtlas(Uint32 now)
{
#ifdef HAVE_OPENGL
    const char *every = getenv("GLOB2_TORUS_DUMP");
    if (!every)
        return;
    if (!lastDump)
        fprintf(stderr, "Torus view: GL %s, %s, %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
                glGetString(GL_VERSION));
    if (lastDump && now - lastDump < Uint32(std::max(1, atoi(every))) * 1000)
        return;
    lastDump = now;
    std::vector<unsigned char> pixels(size_t(atlasW) * atlasH * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, atlasW, atlasH, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const int step = 8, w = atlasW / step, h = atlasH / step;
    std::unique_ptr<GAGCore::OutputStream> out(new GAGCore::BinaryOutputStream(
        GAGCore::Toolkit::getFileManager()->openOutputStreamBackend("torus-atlas.ppm")));
    if (out->isEndOfStream())
        return;
    std::string header = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    out->write(header.data(), header.size(), "header");
    std::vector<unsigned char> row(size_t(w) * 3);
    for (int y = 0; y < h; ++y)
    {
        // GL rows run bottom-up; the map's top row goes first
        const unsigned char *src = &pixels[size_t(atlasH - 1 - y * step) * atlasW * 3];
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                row[x * 3 + c] = src[size_t(x) * step * 3 + c];
        out->write(row.data(), row.size(), "row");
    }
    fprintf(stderr, "Torus view: wrote torus-atlas.ppm (%dx%d) at %u ms\n", w, h, now);
#endif
}

bool TorusView::draw(Game &game, int team, unsigned options, int &vx, int &vy, int width, int height,
                     Game::ViewState &view, const BuildingGuiStateMap *buildingGuiState)
{
#ifdef HAVE_OPENGL
    Uint32 now = SDL_GetTicks();
    float dt = std::min(0.1f, float(now - lastFrame) / 1000.0f);
    lastFrame = now;
    // A pause in the movement ends the pull-back.
    if (moving && now - lastMove > 250)
        moving = false;
    const bool pullBack = target || moving || held;
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
        focusU = (((vx - originX) & game.map.getMaskW()) + width / 64.0f) / game.map.getW();
        focusV = (((vy - originY) & game.map.getMaskH()) + (height + 16) / 64.0f) / game.map.getH();
        baseViewportX = vx;
        baseViewportY = vy;
        worldW = game.map.getW();
        worldH = game.map.getH();
        travelU = travelV = cameraU = cameraV = 0;
    }
    // Movement pulls back slowly; the return is quick. The hand switch keeps its own pace.
    const float pace = target ? 1.8f : ((moving || held) ? 4.0f : 0.45f);
    amount = clamp(amount + (pullBack ? dt : -dt) / pace, 0, 1);
    // The ordinary map and minimap track the same destination as the torus.
    // Ease the sub-tile remainder away while returning to the tile-based 2D camera.
    if (!pullBack)
    {
        float settle = amount == 0 ? 1 : 1 - std::exp(-16 * dt);
        travelU = mix(travelU, std::round(travelU * worldW) / worldW, settle);
        travelV = mix(travelV, std::round(travelV * worldH) / worldH, settle);
    }
    // Input remains the logical map destination; render the mesh, picking and
    // distant sky through one smoothly following camera. Never filter the sky
    // separately, which would make the universe drift relative to the surface.
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
        return false;
    }
    // Keep terrain, units, clouds and pointer previews on the normal render cadence.
    {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, atlasW, atlasH);
        glOrtho(0, game.map.getW() * 32, game.map.getH() * 32, 0, -1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // Capture the normal cloud and shadow layers along with the world,
        // respecting the same graphics-quality setting as the 2D view.
        game.drawMap(0, 0, game.map.getW() * 32, game.map.getH() * 32, 0, 0, originX, originY, team, view,
                     options | Game::DRAW_NO_CLOUD_LAYER, nullptr, buildingGuiState);
        if (game.gui)
            game.gui->drawTorusMapOverlay(originX, originY);
        dumpAtlas(now);
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
    // tube as the view pans, and turns about the axis as it scrolls, while
    // the sky drifts with both so the movement stays readable.
    const float ringV = 0.5f;
    float cameraDistance = TorusGeometry::hoverDistance(ringV);
    float scale = std::min(width / 10.0f, height / 9.0f) * mix(1, cameraZoom, roll);
    float sx = std::exp(mix(std::log(game.map.getW() * 32 / (8 * pi)), std::log(scale), pull));
    float sy = sx * TorusGeometry::verticalScale(focusU, focusV, roll, aspect);
    surfaceScaleX = sx;
    surfaceScaleY = sy;
    float cx = width * 0.5f, cy = (height + 16) * 0.5f;
    float major = smooth(roll), minor = smooth(roll / 0.85f);
    float ya = -(anchorU - 0.5f) * 2 * pi, pa = TorusGeometry::latitude(ringV, aspect);
    float viewPitch = pa + TorusGeometry::overviewTilt(ringV, roll);
    float skyPitch = viewPitch + (anchorV - 0.5f) * pi * 0.5f;
    drawSky(ya, skyPitch, roll, sx, sy, cameraDistance, width, height, gfx->getW());
    if (material)
    {
        glUseProgram(material);
        glUniform1i(glGetUniformLocation(material, "world"), 0);
        glUniform2f(glGetUniformLocation(material, "mapOffset"), anchorU, 1 - anchorV);
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
        std::vector<MeshVertex> cloudVertices(vertices.size());
        // The cloud ring floats above the ground by a fixed share of the tube
        // radius; it settles onto the flat map as the fold opens.
        const float cloudHeight = 0.12f * roll + 0.003f, e = 0.001f;
        for (int j = 0; j <= V; ++j)
            for (int i = 0; i <= U; ++i)
            {
                float du = float(i) / U - 0.5f, dv = float(j) / V - 0.5f;
                auto p = TorusGeometry::overviewPoint(du, dv, roll, ringV);
                auto pu = TorusGeometry::overviewPoint(du + e, dv, roll, ringV);
                auto pv = TorusGeometry::overviewPoint(du, dv + e, roll, ringV);
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
                                             {du, -dv}};
                float cw = 1 - c.z * roll / cameraDistance;
                cloudVertices[j * (U + 1) + i] = {{cx * cw + c.x * sx, cy * cw + c.y * sy, c.z * scale, cw},
                                                  {light, light, light},
                                                  {du, -dv}};
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
    glVertexPointer(4, GL_FLOAT, sizeof(MeshVertex),
                    reinterpret_cast<void *>(offsetof(MeshVertex, position)));
    glColorPointer(3, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, color)));
    glTexCoordPointer(2, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, uv)));
    // Geometry stays on the GPU while stationary. Longitude navigation only
    // changes a uniform; latitude or unfolding rebuilds one shared vertex grid.
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
        glTranslatef(anchorU, anchorV, 0);
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

bool TorusView::pick(int x, int y, int &px, int &py) const
{
    if (!active() || !worldW || !worldH || x < 0 || y < 16 || x >= pickWidth || y >= pickHeight)
        return false;
    if (x != cachedPickX || y != cachedPickY)
    {
        cachedPick = TorusPicking::Hit{};
        cachedPickFound = TorusPicking::mesh(vertices, meshColumns, meshRows, x, y, cachedPick);
        cachedPickX = x;
        cachedPickY = y;
    }
    if (!cachedPickFound)
        return false;
    px = TorusPicking::worldPixel(pickU + cachedPick.u, originX, worldW);
    py = TorusPicking::worldPixel(pickV - cachedPick.v, originY, worldH);
    return true;
}
