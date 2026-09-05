// SPDX-License-Identifier: GPL-3.0-or-later

// Standalone rendering regression and benchmark. Uses the production batching
// helper against the original per-cell triangle fan, with identical inputs.
#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <epoxy/gl.h>
#endif
#include "../libgag/include/AlphaMapRender.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

void legacy(const std::valarray<unsigned char> &map, int w, int h, int x, int y, int cw, int ch)
{
    for (int j = 0; j < h - 1; ++j)
        for (int i = 0; i < w - 1; ++i)
        {
            int a[4] = {map[w * j + i], map[w * (j + 1) + i], map[w * (j + 1) + i + 1], map[w * j + i + 1]};
            int middle = ((a[0] + a[3]) / 2 + (a[1] + a[2]) / 2) / 2;
            glBegin(GL_TRIANGLE_FAN);
            glColor4ub(211, 237, 255, middle);
            glVertex2i(x + i * cw + cw / 2, y + j * ch + ch / 2);
            for (int k = 0; k < 5; ++k)
            {
                int corner = k % 4;
                glColor4ub(211, 237, 255, a[corner]);
                glVertex2i(x + (i + (corner == 2 || corner == 3)) * cw,
                           y + (j + (corner == 1 || corner == 2)) * ch);
            }
            glEnd();
        }
}
int main()
{
    if (SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window *window =
        SDL_CreateWindow("Cloud renderer benchmark", 0, 0, 512, 512, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context)
    {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    glViewport(0, 0, 512, 512);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(.12f, .25f, .37f, 1);
    std::printf("GPU: %s\n", glGetString(GL_RENDERER));
    for (int grid : {18, 129, 257, 513})
    {
        std::valarray<unsigned char> map(static_cast<size_t>(grid * grid));
        unsigned seed = 17;
        for (size_t i = 0; i < map.size(); ++i)
        {
            seed = 1664525u * seed + 1013904223u;
            map[i] = (seed >> 24) % 121;
        }
        int cellW = grid == 18 ? 17 : 16, cellH = grid == 18 ? 19 : 16;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, (grid - 1) * cellW, (grid - 1) * cellH, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        auto draw = [&](bool batch)
        {
            if (batch)
                GAGCore::drawAlphaMapBatched(map, grid, grid, -3, -5, cellW, cellH, 211, 237, 255);
            else
                legacy(map, grid, grid, -3, -5, cellW, cellH);
        };
        std::vector<unsigned char> reference(512 * 512 * 4), actual(reference.size());
        glClear(GL_COLOR_BUFFER_BIT);
        draw(false);
        glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, reference.data());
        glClear(GL_COLOR_BUFFER_BIT);
        draw(true);
        glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
        int difference = 0;
        for (size_t i = 0; i < actual.size(); ++i)
            difference = std::max(difference, std::abs(int(actual[i]) - reference[i]));
        if (difference > 1 || glGetError() != GL_NO_ERROR)
        {
            std::fprintf(stderr, "Rendering mismatch: %d\n", difference);
            return 2;
        }
        std::vector<double> times[2];
        for (int run = 0; run < 6; ++run)
            for (int mode = 0; mode < 2; ++mode)
            {
                glClear(GL_COLOR_BUFFER_BIT);
                glFinish();
                Uint64 start = SDL_GetPerformanceCounter();
                draw(mode);
                glFinish();
                double elapsed =
                    1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
                if (run)
                    times[mode].push_back(elapsed);
            }
        for (auto &t : times)
            std::sort(t.begin(), t.end());
        std::printf(
            "%dx%d patches: original %.3f ms, batched %.3f ms, %.2fx faster; max pixel difference %d/255\n",
            grid - 1, grid - 1, times[0][2], times[1][2], times[0][2] / times[1][2], difference);
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
