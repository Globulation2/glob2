// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainCompositor.h"
#include "map/TerrainRegistry.h"
#include "TerrainCompiledPack.h"
#include "TerrainPresentation.h"
#include "render/scene/SceneMap.h"
#include <Toolkit.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace TerrainVisual
{
Compositor::Compositor(Catalog catalog, std::shared_ptr<const MapAssetBundle> assets)
    : definitions(std::move(catalog)), customSprites(std::move(assets))
{
	pack = CompiledPack::load(definitions);
	for (unsigned type = 0; type < TERRAIN_COUNT; ++type)
	{
		const auto found =
			definitions.bindings.find(terrainPresentation(static_cast<TerrainType>(type)).name);
		if (found != definitions.bindings.end())
			terrainBindings[type] = found->second;
		else if (!terrainUsesLegacyCorners(static_cast<TerrainType>(type)))
			throw std::runtime_error("Missing terrain material binding");
	}
	for (const auto &m : definitions.materials)
	{
		auto *sprite = customSprites.resolve(m.sprite);
		if (!sprite)
			throw std::runtime_error("Missing terrain material sprite: " + m.sprite);
		for (const auto &v : m.variants)
			for (int p = 0; p < m.animationFrames; ++p)
			{
				const int frame = v.frame + p * m.animationStride;
				if (frame >= sprite->getFrameCount() || sprite->getW(frame) != 32 ||
					sprite->getH(frame) != 32)
					throw std::runtime_error("Terrain material requires a 32x32 logical frame: " +
											 m.key);
				auto *native = sprite->nativeFrame(frame);
				cleanSources[native->lifetimeIdentity()] = native->contentRevision();
			}
		if (!m.decor.sprite.empty())
		{
			auto *decor = customSprites.resolve(m.decor.sprite);
			if (!decor)
				throw std::runtime_error("Missing terrain decor sprite: " + m.key);
			for (const auto &frames : {m.decor.full, m.decor.edge})
				for (int frame : frames)
					if (frame >= decor->getFrameCount() || decor->getW(frame) > 64 ||
						decor->getH(frame) > 64)
						throw std::runtime_error("Terrain decor frame missing or larger than 64x64: " +
												 m.key);
			if (sharedDecorSprite && sharedDecorSprite != decor) mixedDecorSprites = true;
            sharedDecorSprite = decor;
		}
		decorSprites.push_back(m.decor.sprite.empty() ? nullptr : customSprites.resolve(m.decor.sprite));
        sprites.push_back(sprite);
		textures.emplace_back(m.variants.size());
		materialRevisions.push_back(0);
	}
    if (mixedDecorSprites) sharedDecorSprite = nullptr;
}
void Compositor::readTexture(Texture &t, GAGCore::DrawableSurface *source)
{
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
		SDL_ConvertSurface(source->getSDLSurface(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	if (!rgba)
		throw std::runtime_error(SDL_GetError());
	if (rgba->w != rgba->h || rgba->w < 32 || rgba->w > 512)
	{
		throw std::runtime_error("Invalid terrain texture dimensions");
	}
	std::vector<std::array<unsigned char, 4>> pixels(rgba->w * rgba->h);
	for (int y = 0; y < rgba->h; ++y)
		std::memcpy(pixels.data() + y * rgba->w,
					static_cast<char *>(rgba->pixels) + y * rgba->pitch, rgba->w * 4);
	t.pixels = std::move(pixels);
	t.size = rgba->w;
	t.source = source;
	t.identity = source->lifetimeIdentity();
	t.revision = source->contentRevision();
}
std::pair<GAGCore::Sprite *, unsigned> Compositor::editorIcon(TerrainType type) const
{
	const auto id = terrainBindings[unsigned(type)];
	return {sprites[id], unsigned(definitions.materials[id].variants.front().frame)};
}
std::size_t Compositor::sourceBytes() const
{
	std::size_t bytes = pack ? pack->bytes() : 0;
	for (const auto &set : textures)
		for (const auto &t : set)
			bytes += t.pixels.capacity() * 4;
	return bytes;
}
void Compositor::prepare(bool hd, int time)
{
	int nextResolution = 1;
	for (unsigned id = 0; id < definitions.materials.size(); ++id)
	{
		const auto &m = definitions.materials[id];
		const int phase = unsigned(time) / m.animationTicks % m.animationFrames;
		bool refresh = false;
		for (unsigned i = 0; i < m.variants.size(); ++i)
		{
			const int frame = m.variants[i].frame + phase * m.animationStride;
			auto *source = hd ? sprites[id]->baseFrame(frame) : sprites[id]->nativeFrame(frame);
			if (!source)
				throw std::runtime_error("Missing terrain texture: " + m.key);
			auto &t = textures[id][i];
			refresh |= t.source != source || t.identity != source->lifetimeIdentity() ||
					   t.revision != source->contentRevision();
			if (source->getW() > 32)
				nextResolution = 4;
		}
		if (!refresh)
			continue;
		++materialRevisions[id];
		bool packaged = pack != nullptr;
		if (packaged)
			for (const auto &variant : m.variants)
			{
				const int frame = variant.frame + phase * m.animationStride;
				auto *source = hd ? sprites[id]->baseFrame(frame) : sprites[id]->nativeFrame(frame);
				const auto clean = cleanSources.find(source->lifetimeIdentity());
				packaged &= source == sprites[id]->nativeFrame(frame) &&
							clean != cleanSources.end() &&
							clean->second == source->contentRevision() &&
							pack->matches(m.sprite + std::to_string(frame) + ".png",
										  source->getSDLSurface());
			}
		for (unsigned i = 0; i < m.variants.size(); ++i)
		{
			const int frame = m.variants[i].frame + phase * m.animationStride;
			auto *source = hd ? sprites[id]->baseFrame(frame) : sprites[id]->nativeFrame(frame);
			auto &t = textures[id][i];
			if (packaged)
			{
				pack->read(m.sprite + std::to_string(frame) + ".png", t.pixels);
				t.size = 32;
				t.source = source;
				t.identity = source->lifetimeIdentity();
				t.revision = source->contentRevision();
			}
			else
				readTexture(t, source);
		}
		if (packaged)
			continue; // Compiler already prepared the shared variant borders.
		if (m.periodicEdges)
			continue; // Variants already share one periodic edge band.
		// One periodic master boundary per material, not a different edge for
		// each variant. Blend premultiplied color and alpha together so
		// translucent variants cannot reintroduce rectangular seams.
		const auto master = textures[id][0];
		for (auto &t : textures[id])
			for (int y = 0; y < t.size; ++y)
				for (int x = 0; x < t.size; ++x)
				{
					const int distance = std::min({x, y, t.size - 1 - x, t.size - 1 - y});
					const int band = std::max(1, t.size / 8);
					if (distance >= band)
						continue;
					const int mx = x * master.size / t.size, my = y * master.size / t.size;
					// Reflect the master at each seam: opposing outer pixels agree.
					const int xx = std::min(mx, master.size - 1 - mx),
							  yy = std::min(my, master.size - 1 - my);
					const auto &p = master.pixels[yy * master.size + xx];
					auto &destination = t.pixels[y * t.size + x];
					const unsigned alpha = p[3] * (band - distance) + destination[3] * distance;
					for (int k = 0; k < 3; ++k)
						destination[k] = alpha ? (p[k] * p[3] * (band - distance) +
												  destination[k] * destination[3] * distance) /
													 alpha
											   : 0;
					destination[3] = alpha / band;
				}
	}
	resolution = nextResolution;
}
MaterialId Compositor::materialFor(const SceneMap& map, int x, int y) const {
    const auto type = map.terrainTypeAt(x, y);
    const auto found = definitions.bindings.find(map.terrainRegistry().key(type));
    if (unsigned(type) >= TERRAIN_COUNT && found != definitions.bindings.end()) return found->second;
    return terrainBindings[unsigned(map.appearanceAt(x, y))];
}
GAGCore::Sprite *Compositor::decorSprite(const SceneMap &map, int x, int y) const
{
    return decorSprites[materialFor(map, x & map.getMaskW(), y & map.getMaskH())];
}
int Compositor::decorFrame(const SceneMap &map, int x, int y) const
{
	x &= map.getMaskW();
	y &= map.getMaskH();
	const auto type = map.terrainTypeAt(x, y);
	if (unsigned(type) < TERRAIN_COUNT && terrainUsesLegacyCorners(type))
		return -1;
	const auto id = materialFor(map, x, y);
	if (definitions.materials[id].decor.full.empty())
		return -1;
	bool edge = false;
	for (const auto [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
		edge |= materialFor(map, (x + dx) & map.getMaskW(), (y + dy) & map.getMaskH()) != id;
	return definitions.decorFrame(id, x, y, edge, map.terrainSeed());
}
Recipe Compositor::describe(const SceneMap &map, int x, int y) const
{
	Recipe r;
	r.x = x & map.getMaskW();
	r.y = y & map.getMaskH();
	r.width = map.getW();
	r.height = map.getH();
	r.seed = map.terrainSeed();
	for (int j = 0; j < 4; ++j)
		for (int i = 0; i < 4; ++i)
		{
			const int qx = (r.x * 2 + i - 1) & (r.width * 2 - 1),
					  qy = (r.y * 2 + j - 1) & (r.height * 2 - 1);
			const int cx = qx / 2, cy = qy / 2;
			auto type = map.terrainTypeAt(cx, cy);
			unsigned material = unsigned(map.appearanceAt(cx, cy));
			if (unsigned(type) < TERRAIN_COUNT && terrainUsesLegacyCorners(type))
				material = legacyCorners(map.getTerrain(cx, cy))[(qx & 1) + 2 * (qy & 1)];
			r.samples[j * 4 + i] = unsigned(type) < TERRAIN_COUNT && terrainUsesLegacyCorners(type)
                ? terrainBindings[material] : materialFor(map, cx, cy);
		}
	return r;
}
void Compositor::compose(const Recipe &r, SDL_Surface *target, int ox, int oy, int scale) const
{
	if (target->format != SDL_PIXELFORMAT_ARGB8888)
		throw std::runtime_error("Terrain compositor requires ARGB8888");
	const int size = 32 * scale;
	// Resolve immutable pixel storage once per tile, rather than through
	// vector/array accessors for every channel of every instrumented pixel.
	struct Source
	{
		const std::array<unsigned char, 4> *pixels = nullptr;
		int size = 0;
	};
	std::vector<Source> selected(definitions.materials.size());
	auto *sources = selected.data();
	for (auto id : r.samples)
		if (!sources[id].pixels)
		{
			const auto &texture = textures[id][definitions.variantIndex(id, r.x, r.y, r.seed)];
			sources[id] = {texture.pixels.data(), texture.size};
		}
	const bool uniform = std::all_of(r.samples.begin(), r.samples.end(),
									 [&](auto id) { return id == r.samples[0]; });
	if (uniform)
	{
		const auto &texture = sources[r.samples[0]];
		for (int y = 0; y < size; ++y)
		{
			auto *row = reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(target->pixels) +
												   (oy + y) * target->pitch) +
						ox;
			for (int x = 0; x < size; ++x)
			{
				if (!texture.pixels)
					row[x] = 0;
				else
				{
					const auto *p = texture.pixels[(y * texture.size / size) * texture.size +
													x * texture.size / size].data();
					row[x] = (unsigned(p[3]) << 24) | (unsigned(p[0]) << 16) |
							 (unsigned(p[1]) << 8) | p[2];
				}
			}
		}
		return;
	}
	const PreparedCoverage prepared(definitions, r);
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
		{
			const auto mask = prepared.at((x * 256 + 128) / scale, (y * 256 + 128) / scale);
			const auto *weights = mask.weight.data();
			const auto *materials = mask.material.data();
			std::uint64_t rgb[3] = {};
			unsigned alpha = 0;
			for (int i = 0; i < 4; ++i)
				if (weights[i] && sources[materials[i]].pixels)
				{
					const auto &t = sources[materials[i]];
					const auto *p = t.pixels[(y * t.size / size) * t.size + x * t.size / size].data();
					const unsigned a = weights[i] * p[3];
					alpha += a;
					for (int k = 0; k < 3; ++k)
						rgb[k] += std::uint64_t(p[k]) * a;
				}
			auto *p = reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(target->pixels) +
												 (oy + y) * target->pitch) +
					  ox + x;
			// Seam treatment: the dominant material darkens under a higher
			// neighbor's edge and takes that neighbor's fringe tint. Textures are
			// never blended across the seam; only the contact band is toned.
			unsigned dominant = 0;
			for (unsigned i = 1; i < 4; ++i)
				if (mask.weight[i] > mask.weight[dominant])
					dominant = i;
			const auto &self = definitions.materials[mask.material[dominant]].seam;
			const auto &other = definitions.materials[mask.neighbor].seam;
			int shade = 256, tint = 0;
			if (mask.neighbor != mask.material[dominant])
			{
				if (other.cast && other.height > self.height && int(mask.margin) < other.castWidth)
					shade = 256 - other.cast * (other.castWidth - int(mask.margin)) / other.castWidth;
				if (other.fringe && int(mask.margin) < other.fringeWidth)
					tint = other.fringe * (other.fringeWidth - int(mask.margin)) / other.fringeWidth;
			}
			const auto channel = [&](int k)
			{
				unsigned value = unsigned(alpha ? rgb[k] / alpha : 0) * shade >> 8;
				return value + (unsigned(other.fringeColor[k]) - value) * tint / 256;
			};
			*p = ((alpha + 32768) / 65536 << 24) | (channel(0) << 16) | (channel(1) << 8) |
				 channel(2);
		}
}
void Compositor::composeOverview(const Recipe &r, SDL_Surface *target, int ox, int oy,
								 const std::array<unsigned char, 3> *cellColor) const
{
	if (target->format != SDL_PIXELFORMAT_ARGB8888)
		throw std::runtime_error("Terrain overview requires ARGB8888");
	const auto packed = [](const auto &color)
	{ return 0xFF000000u | (unsigned(color[0]) << 16) | (unsigned(color[1]) << 8) | color[2]; };
	// Saved custom whole-cell aliases may carry their own overview palette.
	const auto colorOf = [&](MaterialId id) -> const std::array<unsigned char, 3> &
	{ return cellColor && id == r.samples[5] ? *cellColor : definitions.materials[id].preview; };
	if (std::all_of(r.samples.begin(), r.samples.end(),
					[&](auto id) { return id == r.samples[0]; }))
	{
		SDL_Rect area{ox, oy, OverviewSamples, OverviewSamples};
		SDL_FillSurfaceRect(target, &area, packed(colorOf(r.samples[0])));
		return;
	}
	const PreparedCoverage prepared(definitions, r);
	for (int y = 0; y < OverviewSamples; ++y)
		for (int x = 0; x < OverviewSamples; ++x)
		{
			const auto mask = prepared.at((x * 8192 + 4096) / OverviewSamples,
										 (y * 8192 + 4096) / OverviewSamples);
			std::array<unsigned, 3> color{};
			for (int i = 0; i < 4; ++i)
				for (int k = 0; k < 3; ++k)
					color[k] += mask.weight[i] * colorOf(mask.material[i])[k];
			for (auto &channel : color)
				channel = (channel + 32768) / 65536;
			auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) +
												 (oy + y) * target->pitch);
			row[ox + x] = packed(color);
		}
}
} // namespace TerrainVisual
