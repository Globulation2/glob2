// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Glob2Test.h"
#include <RenderDevice.h>

inline void verifyDirectTerrainReadback(GAGCore::RenderDevice &device)
{
    using namespace GAGCore;
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    RenderResources resources;
    using Terrain = DirectTerrainDraw;
    auto terrain = std::make_shared<Terrain>();
    const auto setTexture = [&](Terrain::Texture slot, Surface pixels) {
        const auto handle = resources.create(pixels.get(), 0);
        REQUIRE(handle);
        terrain->textures[slot] = resources.snapshot(handle);
    };
    const auto filled = [](int w, int h, SDL_Color color) {
        Surface pixels(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        REQUIRE(pixels);
        REQUIRE(SDL_FillSurfaceRect(pixels.get(), nullptr,
                                    SDL_MapSurfaceRGBA(pixels.get(), color.r, color.g, color.b, color.a)));
        return pixels;
    };
    auto cells = filled(4, 1, {0, 0, 0, 0});
    REQUIRE(SDL_WriteSurfacePixel(cells.get(), 0, 0, 1, 0, 0, 0));
    REQUIRE(SDL_WriteSurfacePixel(cells.get(), 2, 0, 1, 0, 0, 0));
    REQUIRE(SDL_WriteSurfacePixel(cells.get(), 3, 0, 1, 0, 0, 0));
    setTexture(Terrain::Cells, std::move(cells));
    setTexture(Terrain::MaskIndex, filled(1, 1, {0, 0, 0, 0}));
    auto art = filled(64, 32, {255, 0, 0, 255});
    const SDL_Rect secondTile{32, 0, 32, 32};
    REQUIRE(SDL_FillSurfaceRect(art.get(), &secondTile, SDL_MapSurfaceRGBA(art.get(), 0, 255, 0, 255)));
    setTexture(Terrain::Art, std::move(art));
    setTexture(Terrain::Coverage, filled(32, 32, {255, 0, 0, 0}));
    setTexture(Terrain::Edges, filled(16, 32, {0, 0, 0, 0}));
    auto variants = filled(512, 9, {0, 0, 0, 0});
    REQUIRE(SDL_WriteSurfacePixel(variants.get(), 16, 0, 0, 1, 1, 0));
    REQUIRE(SDL_WriteSurfacePixel(variants.get(), 16, 8, 1, 1, 1, 0));
    setTexture(Terrain::Variants, std::move(variants));
    setTexture(Terrain::Discovery, filled(1, 1, {0, 255, 0, 0}));
    setTexture(Terrain::ShoreCalm, filled(2, 1, {0, 0, 0, 0}));

    DrawPacket background;
    background.destination = {0, 0, 16, 16};
    background.color = {0, 0, 255, 255};
    background.opaque = true;
    DrawPacket tile;
    tile.kind = DrawPacket::Kind::DirectTerrain;
    tile.blend = DrawPacket::Blend::Premultiplied;
    tile.directTerrain = terrain;
    const SDL_FColor white{1, 1, 1, 1};
    const SDL_Vertex a{{0, 0}, white, {-1, 0}}, b{{16, 0}, white, {1, 0}},
                     c{{16, 16}, white, {1, 1}}, d{{0, 16}, white, {-1, 1}};
    tile.vertices = {a, b, c, a, c, d};
    DrawPacket overlay = background;
    overlay.destination = {0, 0, 2, 2};
    overlay.color = {255, 255, 255, 255};
    RenderFrame frame;
    frame.identity = 1;
    frame.logicalWidth = frame.logicalHeight = 16;
    frame.packets = {background, tile, overlay};
    frame.vertexCount = 6;
    REQUIRE(device.submit(std::move(frame)));
    Surface result(device.captureSynchronously({}), SDL_DestroySurface);
    REQUIRE(result);
    const auto check = [&](int x, int y, Uint8 red, Uint8 green, Uint8 blue) {
        SDL_Color pixel{};
        REQUIRE(SDL_ReadSurfacePixel(result.get(), x, y, &pixel.r, &pixel.g, &pixel.b, &pixel.a));
        CHECK(pixel.r == red);
        CHECK(pixel.g == green);
        CHECK(pixel.b == blue);
    };
    check(4, 8, 0, 255, 0);
    check(12, 8, 0, 0, 255);
    check(1, 1, 255, 255, 255);
}
