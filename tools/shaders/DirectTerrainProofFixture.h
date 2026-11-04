// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <RenderFrame.h>
#include <stdexcept>

template<class Image>
GAGCore::DrawPacket directTerrainProofPacket(Image image, bool mixed = false)
{
    using namespace GAGCore;
    using Terrain = DirectTerrainDraw;
    auto data = std::make_shared<Terrain>();
    data->textures[Terrain::Cells] = image(2, 1, {0, 0, 0, 0});
    data->textures[Terrain::MaskIndex] = image(1, 1, {Uint8(mixed), 0, 0, 0});
    data->textures[Terrain::Art] = image(64, 32, {0, 128, 0, 255});
    data->textures[Terrain::Coverage] = image(32, 32, {128, 127, 0, 0});
    // Both packed edge texels select dominant slot zero and neighbor slot one.
    data->textures[Terrain::Edges] = image(16, 32, {0, 64, 0, 64});
    data->textures[Terrain::Variants] = image(32, 1, {0, 0, 0, 0});
    data->textures[Terrain::Discovery] = image(1, 1, {255, 0, 0, 0});
    data->textures[Terrain::ShoreCalm] = image(1, 1, {0, 0, 0, 0});
    const auto write = [&](Terrain::Texture texture, int x, SDL_Color color) {
        if (!SDL_WriteSurfacePixel(data->textures[texture]->pixels.get(), x, 0, color.r, color.g, color.b, color.a))
            throw std::runtime_error(SDL_GetError());
    };
    write(Terrain::Cells, 0, {0, 1, 0, 0});
    write(Terrain::Variants, 0, {0, 1, 1, 0});
    write(Terrain::Variants, 16, {1, 1, 1, 0});
    // The higher neighbor casts a half-strength shadow at zero margin.
    write(Terrain::Variants, 26, {100, 128, 0, 0});
    write(Terrain::Variants, 27, {0, 0, 1, 0});
    auto *art = data->textures[Terrain::Art]->pixels.get();
    const SDL_Rect secondTile{32, 0, 32, 32};
    if (!SDL_FillSurfaceRect(art, &secondTile, SDL_MapSurfaceRGBA(art, 128, 0, 0, 255)))
        throw std::runtime_error(SDL_GetError());
    DrawPacket draw;
    draw.kind = DrawPacket::Kind::DirectTerrain;
    draw.blend = DrawPacket::Blend::Premultiplied;
    draw.directTerrain = data;
    const SDL_FColor white{1, 1, 1, 1};
    const SDL_Vertex a{{0, 0}, white, {-1, -1}}, b{{64, 0}, white, {1, -1}},
                     c{{64, 64}, white, {1, 1}}, d{{0, 64}, white, {-1, 1}};
    draw.vertices = {a, b, c, a, c, d};
    return draw;
}
