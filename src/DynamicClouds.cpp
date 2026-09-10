// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#include "DynamicClouds.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"
#include <SDL.h>
#include <cmath>
#ifdef HAVE_OPENGL
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#endif

/// how much the slope of a lobe is exaggerated before it is lit, in big-lobe
/// spacings: a taller heap has a broader shaded underside
static const float cloudRelief = .7f;
/// How far the fog mask is grown and then blurred, in world pixels. Growing by as
/// much as the blur softens is what keeps the deck at full strength right up to the
/// edge of the fog; any less and the black layer shows through as a rim.
static const int cloudFogFeather = 16;

static float smoothStep(float from, float to, float value)
{
    float t = (value - from) / (to - from);
    t = std::min(1.0f, std::max(0.0f, t));
    return t * t * (3 - 2 * t);
}

DynamicClouds::~DynamicClouds()
{
#ifdef HAVE_OPENGL
    // Object names belong to their creating context; never touch them in another.
    if (globalContainer && globalContainer->gfx && graphicsContext == SDL_GL_GetCurrentContext() &&
        graphicsGeneration == globalContainer->gfx->getGLContextGeneration())
    {
        if (texture)
            glDeleteTextures(1, &texture);
        if (material)
            glDeleteProgram(material);
    }
#endif
}

unsigned DynamicClouds::createDeckMaterial()
{
#ifdef HAVE_OPENGL
    const char *vertex = "#version 120\n"
                         "varying vec2 uv; varying vec4 tint;\n"
                         "void main(){gl_Position=ftransform();uv=(gl_TextureMatrix[0]*gl_MultiTexCoord0).xy;"
                         "tint=gl_Color;}\n";
    // The sun stands off the top right corner. The normal's y axis runs down the
    // screen, so the lit tops are the slopes rising towards the upper right. The
    // bands are cut a pixel wide, whatever the lattice under them.
    const char *fragment = "#version 120\n"
                           "uniform sampler2D deck;\n"
                           "varying vec2 uv; varying vec4 tint;\n"
                           "void main(){\n"
                           "  vec4 t = texture2D(deck, uv);\n"
                           "  vec2 n = t.rg * 2.0 - 1.0;\n"
                           "  float facing = dot(vec3(n, sqrt(max(0.0, 1.0 - dot(n, n)))), vec3(0.45, -0.55, 0.70));\n"
                           "  float edge = max(fwidth(facing), 0.01);\n"
                           "  float puff = smoothstep(0.0, 0.15, t.b);\n"
                           "  float lit = smoothstep(0.66 - edge, 0.66 + edge, facing) * puff;\n"
                           "  float mid = smoothstep(0.40 - edge, 0.40 + edge, facing) * puff;\n"
                           "  vec3 c = mix(vec3(0.62, 0.667, 0.784), vec3(0.839, 0.871, 0.933), mid);\n"
                           "  c = mix(c, vec3(1.0), lit);\n"
                           "  gl_FragColor = vec4(c * tint.rgb, t.a * tint.a);}\n";
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
        fprintf(stderr, "Cloud deck material: %s\n", log);
        glDeleteProgram(program);
        program = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
#else
    return 0;
#endif
}

void DynamicClouds::feather(const std::valarray<unsigned char> &visibility, int gridW, int gridH,
                            int cellSize, bool wrap, std::valarray<unsigned char> &out)
{
    const int radius = std::max(1, cloudFogFeather / std::max(1, cellSize));
    // Grow twice as far as the blur softens. The fog's own black reaches half a case
    // past the last undiscovered case, so the deck has to still be solid there and
    // do all of its fading beyond it, or the ramp shows the black through itself.
    const int reach = 2 * radius;
    if (out.size() != static_cast<size_t>(gridW * gridH))
        out.resize(gridW * gridH);
    out = visibility;
    std::valarray<unsigned char> pass(gridW * gridH);
    // Grow first, so the deck is still at full strength where the fog begins. If it
    // faded out inside the fog instead, the black layer would show through as a rim.
    for (int step = 0; step < reach; ++step)
    {
        for (int y = 0; y < gridH; ++y)
            for (int x = 0; x < gridW; ++x)
            {
                unsigned char highest = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int ax = x + dx, ay = y + dy;
                        if (wrap)
                        {
                            ax = ((ax % gridW) + gridW) % gridW;
                            ay = ((ay % gridH) + gridH) % gridH;
                        }
                        else if (ax < 0 || ay < 0 || ax >= gridW || ay >= gridH)
                            continue;
                        highest = std::max(highest, out[ay * gridW + ax]);
                    }
                pass[y * gridW + x] = highest;
            }
        out = pass;
    }
    // Then blur, which turns the case-aligned edge into a coast the deck can end on.
    for (int step = 0; step < radius; ++step)
    {
        for (int y = 0; y < gridH; ++y)
            for (int x = 0; x < gridW; ++x)
            {
                int sum = 0, count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int ax = x + dx, ay = y + dy;
                        if (wrap)
                        {
                            ax = ((ax % gridW) + gridW) % gridW;
                            ay = ((ay % gridH) + gridH) % gridH;
                        }
                        else if (ax < 0 || ay < 0 || ax >= gridW || ay >= gridH)
                            continue;
                        sum += out[ay * gridW + ax];
                        ++count;
                    }
                pass[y * gridW + x] = static_cast<unsigned char>(sum / count);
            }
        out = pass;
    }
}

void DynamicClouds::shadeCell(const CloudField::Sample &sample, float spacing, unsigned char weight, bool fog,
                              unsigned char *rgba) const
{
    // The slope is exaggerated into a normal for the shader to light.
    float relief = cloudRelief * spacing;
    float nx = -sample.slopeX * relief, ny = -sample.slopeY * relief;
    float length = std::sqrt(nx * nx + ny * ny + 1);
    float alpha;
    float w = weight / 255.0f;
    if (fog)
    {
        // Solid where the fog is solid. Along its edge, the lobes stick out of the
        // ramp and the gaps retreat into it, so the deck ends in a scalloped coast
        // rather than a smear. Where the ground is merely out of sight the deck is
        // thinner, and thinner still in the gaps, where the ground shows through.
        alpha = w >= .999f ? 1.0f : std::min(w, smoothStep(.3f, .6f, w + .25f * (sample.height - .6f)));
    }
    else
        alpha = maxAlpha / 255.0f * smoothStep(.45f, .7f, sample.height);
    rgba[0] = static_cast<unsigned char>((nx / length * .5f + .5f) * 255 + .5f);
    rgba[1] = static_cast<unsigned char>((ny / length * .5f + .5f) * 255 + .5f);
    rgba[2] = static_cast<unsigned char>(sample.height * 255 + .5f);
    rgba[3] = static_cast<unsigned char>(alpha * 255 + .5f);
}

void DynamicClouds::prepare(const int viewPortX, const int viewPortY, const int viewPortWidth,
                            const int viewPortHeight, int &gridW, int &gridH, int &originX, int &originY,
                            int &cellSize)
{
    // Keep the lattice anchored in world space, so the deck does not swim when the
    // viewport scrolls, and so it lands on the same points the torus ring samples.
    renderCellSize = granularity;
    int pixelX = viewPortX * 32, pixelY = viewPortY * 32;
    renderOffsetX = -(pixelX % renderCellSize);
    renderOffsetY = -(pixelY % renderCellSize);
    wGrid = (viewPortWidth - renderOffsetX + renderCellSize - 1) / renderCellSize + 1;
    hGrid = (viewPortHeight - renderOffsetY + renderCellSize - 1) / renderCellSize + 1;
    gridW = wGrid;
    gridH = hGrid;
    originX = pixelX + renderOffsetX;
    originY = pixelY + renderOffsetY;
    cellSize = renderCellSize;
}

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int viewPortWidth,
                            const int viewPortHeight, const int time, const int worldWidth,
                            const int worldHeight, const std::valarray<unsigned char> *visibility)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU))
        return;
    int gridW, gridH, startX, startY, cellSize;
    prepare(viewPortX, viewPortY, viewPortWidth, viewPortHeight, gridW, gridH, startX, startY, cellSize);
    if (pixels.size() != static_cast<size_t>(wGrid * hGrid * 4))
        pixels.resize(wGrid * hGrid * 4);
    if (visibility)
        feather(*visibility, wGrid, hGrid, renderCellSize, false, fogMap);
    else
        fogMap.resize(0);

    const CloudField deck = field(worldWidth, worldHeight, time);
    for (int y = 0; y < hGrid; ++y)
        for (int x = 0; x < wGrid; ++x)
        {
            unsigned char *texel = &pixels[(y * wGrid + x) * 4];
            // Ground the player can see never carries cloud, so skip the lobes there.
            if (fogMap.size() && fogMap[y * wGrid + x] == 0)
            {
                texel[0] = texel[1] = texel[2] = texel[3] = 0;
                continue;
            }
            int wx = startX + x * renderCellSize, wy = startY + y * renderCellSize;
            shadeCell(deck.sample(wx, wy), deck.spacing, fogMap.size() ? fogMap[y * wGrid + x] : 255,
                      fogMap.size() != 0, texel);
        }
}

void DynamicClouds::getWorldGrid(const int worldWidth, const int worldHeight, int &gridW, int &gridH,
                                 int &cellSize, int maxGridSize) const
{
    // Start from the 2D lattice, which is the same power of two the map sides are
    // multiples of: a cell that divides them exactly is what lets the texture wrap
    // without a seam, and the same cell is what lets the ring and the flat view
    // sample the same points.
    cellSize = std::max(1, granularity);
    while (std::max(worldWidth, worldHeight) * 32 / cellSize > std::max(1, maxGridSize))
        cellSize *= 2;
    gridW = std::max(1, worldWidth * 32 / cellSize);
    gridH = std::max(1, worldHeight * 32 / cellSize);
}

bool DynamicClouds::sampleWorldRows(const int worldWidth, const int worldHeight, const int time, int gridW,
                                   int gridH, int cellSize, int &nextRow, int rows)
{
    // A whole world at this lattice is far more work than one frame is worth, so it
    // is filled a band at a time and only shown once every row is from the same
    // moment. Nothing reads worldField until then.
    if (worldField.size() != static_cast<size_t>(gridW * gridH))
    {
        worldField.resize(gridW * gridH);
        nextRow = 0;
    }
    const CloudField deck = field(worldWidth, worldHeight, time);
    int last = std::min(gridH, nextRow + std::max(1, rows));
    for (int y = nextRow; y < last; ++y)
        for (int x = 0; x < gridW; ++x)
            worldField[y * gridW + x] = deck.sample(x * cellSize, y * cellSize);
    nextRow = last;
    return nextRow >= gridH;
}

void DynamicClouds::shadeWorld(std::valarray<unsigned char> &out, int gridW, int gridH, int cellSize,
                               const std::valarray<unsigned char> *visibility) const
{
    if (out.size() != static_cast<size_t>(gridW * gridH * 4))
        out.resize(gridW * gridH * 4);
    // The lattice covers the whole toroidal world, so the mask wraps with it.
    std::valarray<unsigned char> fog;
    if (visibility)
        feather(*visibility, gridW, gridH, cellSize, true, fog);
    // Only the spacing is needed here, and it does not depend on time.
    const float spacing = field(gridW * cellSize / 32, gridH * cellSize / 32, 0).spacing;
    for (int y = 0; y < gridH; ++y)
        for (int x = 0; x < gridW; ++x)
            shadeCell(worldField[y * gridW + x], spacing, fog.size() ? fog[y * gridW + x] : 255,
                      fog.size() != 0, &out[(y * gridW + x) * 4]);
}

void DynamicClouds::drawQuad(int x, int y, int w, int h, float red, float green, float blue, float alpha)
{
#ifdef HAVE_OPENGL
    // The lattice points are the texel centres, so the quad spans the outermost
    // ones and the texture is sampled from centre to centre.
    float u0 = .5f / wGrid, u1 = (wGrid - .5f) / wGrid;
    float v0 = .5f / hGrid, v1 = (hGrid - .5f) / hGrid;
    glColor4f(red, green, blue, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0);
    glVertex2f(x, y);
    glTexCoord2f(u1, v0);
    glVertex2f(x + w, y);
    glTexCoord2f(u1, v1);
    glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1);
    glVertex2f(x, y + h);
    glEnd();
#endif
}

void DynamicClouds::render(DrawableSurface *dest, const int, const int, DynamicClouds::Layer layer)
{
#ifdef HAVE_OPENGL
    GraphicContext *gfx = dynamic_cast<GraphicContext *>(dest);
    if (!gfx || !(gfx->getOptionFlags() & GraphicContext::USEGPU))
        return;
    if (wGrid < 2 || hGrid < 2 || pixels.size() != static_cast<size_t>(wGrid * hGrid * 4))
        return;
    // Resolution changes can replace SDL's GL context; a name from the old one
    // must not be reused in the new one.
    if (graphicsContext != SDL_GL_GetCurrentContext() || graphicsGeneration != gfx->getGLContextGeneration())
    {
        graphicsContext = SDL_GL_GetCurrentContext();
        graphicsGeneration = gfx->getGLContextGeneration();
        texture = material = 0;
        textureW = textureH = 0;
        materialFailed = false;
    }
    if (!material && !materialFailed)
    {
        material = createDeckMaterial();
        materialFailed = !material;
    }
    if (!material)
        return;
    // Sprites batched by the game renderer go under the deck, and the state
    // cache libgag keeps must find the state it left once the deck is drawn.
    Sprite::flushBatches(gfx);
    GLint oldProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TRANSFORM_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glUseProgram(material);
    glUniform1i(glGetUniformLocation(material, "deck"), 0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glEnable(GL_TEXTURE_2D);
    if (!texture)
        glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (layer == CLOUD || textureW != wGrid || textureH != hGrid)
    {
        // The deck changes every frame; its shadow is the same texels once more.
        if (textureW != wGrid || textureH != hGrid)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wGrid, hGrid, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
            textureW = wGrid;
            textureH = hGrid;
        }
        else
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, wGrid, hGrid, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    int offsetX = renderOffsetX, offsetY = renderOffsetY;
    if (layer == SHADOW)
    {
        // The shadow is this deck cast onto the ground, so it is the same map
        // displaced away from the sun by however high the clouds hang.
        int drop = int((cloudHeight - 1) * 80);
        drawQuad(offsetX - drop, offsetY + drop, (wGrid - 1) * renderCellSize, (hGrid - 1) * renderCellSize, 0, 0,
                 0, .45f);
    }
    else
        drawQuad(offsetX, offsetY, (wGrid - 1) * renderCellSize, (hGrid - 1) * renderCellSize, 1, 1, 1, 1);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    glUseProgram(oldProgram);
#endif
}
