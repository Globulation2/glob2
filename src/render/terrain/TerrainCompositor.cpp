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
	hasContextualProfiles = std::any_of(definitions.profiles.begin(), definitions.profiles.end(),
		[](const auto &profile) { return profile.contextual; });
	pack = CompiledPack::load(definitions);
	for (unsigned type = 0; type < TERRAIN_COUNT; ++type)
	{
		const auto found =
			definitions.bindings.find(terrainPresentation(static_cast<TerrainType>(type)).name);
		if (found == definitions.bindings.end())
			throw std::runtime_error("Missing terrain material binding");
		terrainBindings[type] = found->second;
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
		animationTextures.emplace_back();
		if (m.animationFrames > 1 && m.animationTicks > 1)
		{
			animationTextures.back().current.resize(m.variants.size());
			animationTextures.back().next.resize(m.variants.size());
		}
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
	for (const auto &animation : animationTextures)
		for (const auto *set : {&animation.current, &animation.next})
			for (const auto &t : *set)
				bytes += t.pixels.capacity() * 4;
	return bytes;
}
bool Compositor::prepareMaterial(unsigned id, bool hd, int phase, std::vector<Texture> &target)
{
	const auto &m = definitions.materials[id];
	bool refresh = false;
	for (unsigned i = 0; i < m.variants.size(); ++i)
	{
		const int frame = m.variants[i].frame + phase * m.animationStride;
		auto *source = hd ? sprites[id]->baseFrame(frame) : sprites[id]->nativeFrame(frame);
		if (!source)
			throw std::runtime_error("Missing terrain texture: " + m.key);
		auto &t = target[i];
		refresh |= t.source != source || t.identity != source->lifetimeIdentity() ||
				   t.revision != source->contentRevision();
	}
	if (!refresh)
		return false;
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
		auto &t = target[i];
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
	if (packaged || m.periodicEdges)
		return true; // Sources already have their shared variant borders.
	// One periodic master boundary per material, not a different edge for
	// each variant. Blend premultiplied color and alpha together so
	// translucent variants cannot reintroduce rectangular seams.
	const auto master = target[0];
	for (auto &t : target)
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
	return true;
}
void Compositor::prepare(bool hd, int time)
{
	int nextResolution = 1;
	for (unsigned id = 0; id < definitions.materials.size(); ++id)
	{
		const auto &m = definitions.materials[id];
		const int phase = unsigned(time) / m.animationTicks % m.animationFrames;
		auto &animation = animationTextures[id];
		if (animation.current.empty())
		{
			if (prepareMaterial(id, hd, phase, textures[id])) ++materialRevisions[id];
		}
		else
		{
			// Reuse the previous next phase as the current endpoint on ordinary advances.
			const int frame = m.variants.front().frame + phase * m.animationStride;
			auto *source = hd ? sprites[id]->baseFrame(frame) : sprites[id]->nativeFrame(frame);
			const bool advanced = animation.next.front().source == source;
			if (advanced) animation.current.swap(animation.next);
			const bool currentChanged = prepareMaterial(id, hd, phase, animation.current);
			const bool nextChanged = prepareMaterial(id, hd, (phase + 1) % m.animationFrames, animation.next);
			const unsigned blend = unsigned(time) % m.animationTicks;
			if (advanced || currentChanged || nextChanged || animation.blend != blend)
			{
				++materialRevisions[id];
				animation.blend = blend;
				for (unsigned i = 0; i < m.variants.size(); ++i)
				{
					const auto &a = animation.current[i], &b = animation.next[i];
					auto &out = textures[id][i];
					out.size = std::max(a.size, b.size);
					out.pixels.resize(out.size * out.size);
					const unsigned weightB = (std::uint64_t(blend) * 65536 + m.animationTicks / 2) / m.animationTicks;
					const unsigned weightA = 65536 - weightB;
					for (std::size_t p = 0; p < out.pixels.size(); ++p)
					{
						// Partial HD packs can supply only one endpoint at higher resolution.
						const auto sample = [&](const Texture &t) -> const auto &
						{
							if (t.size == out.size) return t.pixels[p];
							return t.pixels[(p / out.size * t.size / out.size) * t.size +
								p % out.size * t.size / out.size];
						};
						const auto &pa = sample(a), &pb = sample(b);
						auto &pixel = out.pixels[p];
						if (pa[3] == 255 && pb[3] == 255)
						{
							for (unsigned k = 0; k < 3; ++k)
								pixel[k] = (pa[k] * weightA + pb[k] * weightB + 32768) >> 16;
							pixel[3] = 255;
							continue;
						}
						const auto alpha = pa[3] * weightA + pb[3] * weightB;
						// Blend premultiplied colour to avoid fringes in translucent custom art.
						for (unsigned k = 0; k < 3; ++k)
							pixel[k] = alpha ? (pa[k] * pa[3] * weightA +
								pb[k] * pb[3] * weightB + alpha / 2) / alpha : 0;
						pixel[3] = (alpha + 32768) >> 16;
					}
				}
			}
		}
		for (const auto &texture : textures[id])
			if (texture.size > 32) nextResolution = 4;
	}
	resolution = nextResolution;
}
MaterialId Compositor::materialFor(const SceneMap& map, TerrainType type) const {
    // Custom terrain with its own artwork binds by key; anything else draws its appearance.
    if (unsigned(type) >= TERRAIN_COUNT)
        if (const auto found = definitions.bindings.find(map.terrainRegistry().key(type)); found != definitions.bindings.end())
            return found->second;
    return terrainBindings[unsigned(map.terrainRegistry().appearance(type))];
}
std::pair<MaterialId, unsigned> Compositor::decorMaterial(const SceneMap &map, int x, int y) const
{
	// The decorated material most corners share: all four draw its full decor,
	// two or three its edge decor, a single corner none.
	MaterialId best = 0;
	unsigned count = 0;
	const auto corners = map.cellCorners(x & map.getMaskW(), y & map.getMaskH());
	std::array<MaterialId, 4> materials;
	for (unsigned k = 0; k < corners.size(); ++k)
		materials[k] = materialFor(map, corners[k]);
	for (const auto id : materials)
	{
		if (definitions.materials[id].decor.full.empty())
			continue;
		const auto same = unsigned(std::count(materials.begin(), materials.end(), id));
		if (same > count)
		{
			best = id;
			count = same;
		}
	}
	return {best, count};
}
GAGCore::Sprite *Compositor::decorSprite(const SceneMap &map, int x, int y) const
{
    return decorSprites[decorMaterial(map, x, y).first];
}
int Compositor::decorFrame(const SceneMap &map, int x, int y) const
{
	x &= map.getMaskW();
	y &= map.getMaskH();
	const auto [best, count] = decorMaterial(map, x, y);
	if (count < 2)
		return -1;
	return definitions.decorFrame(best, x, y, count < 4, map.terrainSeed());
}
Recipe Compositor::describe(const SceneMap &map, int x, int y) const
{
	Recipe r;
	r.x = x & map.getMaskW();
	r.y = y & map.getMaskH();
	r.width = map.getW();
	r.height = map.getH();
	r.seed = map.terrainSeed();
	const auto corners = map.cellCorners(r.x, r.y);
	for (unsigned k = 0; k < corners.size(); ++k)
		r.corners[k] = materialFor(map, corners[k]);
	const auto first = r.corners[0];
	MaterialId other = first;
	for (const auto id : r.corners)
		if (id != first) other = id;
	if (other == first || !definitions.contextualFor(first, other) ||
		std::any_of(r.corners.begin(), r.corners.end(),
			[first, other](auto id) { return id != first && id != other; }))
		return r;
	r.hasNeighborhood = true;
	for (int y = -1; y <= 2; ++y)
		for (int x = -1; x <= 2; ++x)
			r.neighborhood[(y + 1) * 4 + x + 1] =
				materialFor(map, map.vertexTerrainAt(r.x + x, r.y + y));
	return r;
}
void Compositor::compose(const Recipe &r, SDL_Surface *target, int ox, int oy, int scale,
						 const CellMask *mask) const
{
	if (target->format != SDL_PIXELFORMAT_ARGB8888)
		throw std::runtime_error("Terrain compositor requires ARGB8888");
	const int size = 32 * scale;
	// Resolve immutable pixel storage once per tile, rather than through
	// vector/array accessors for every channel of every instrumented pixel.
	struct Source
	{
		const std::array<unsigned char, 4> *pixels = nullptr;
		int size = 0, step = 0;
	};
	std::vector<Source> selected(definitions.materials.size());
	auto *sources = selected.data();
	for (auto id : r.corners)
		if (!sources[id].pixels)
		{
			const auto &texture = textures[id][definitions.variantIndex(id, r.x, r.y, r.seed)];
			sources[id] = {texture.pixels.data(), texture.size,
			texture.size % size == 0 ? texture.size / size : 0};
		}
	const bool uniform = std::all_of(r.corners.begin(), r.corners.end(),
									 [&](auto id) { return id == r.corners[0]; });
	if (uniform)
	{
		const auto &texture = sources[r.corners[0]];
		// Native and HD sources usually divide the composition grid exactly.
		// Walk their texels directly instead of dividing twice per output pixel.
		if (texture.pixels && texture.step)
		{
			for (int y = 0; y < size; ++y)
			{
				auto *row = reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(target->pixels) +
					(oy + y) * target->pitch) + ox;
				const auto *source = texture.pixels + y * texture.step * texture.size;
				for (int x = 0; x < size; ++x, source += texture.step)
				{
					const auto *p = source->data();
					row[x] = (unsigned(p[3]) << 24) | (unsigned(p[0]) << 16) |
						(unsigned(p[1]) << 8) | p[2];
				}
			}
			return;
		}
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
	// Uncached composition reuses one mask per thread rather than allocating.
	thread_local CellMask scratch;
	if (!mask)
		this->mask(r, scale, scratch);
	const auto &cell = mask ? *mask : scratch;
	if (cell.recipe != r || cell.scale != scale)
		throw std::runtime_error("Terrain cell mask does not match its recipe");
	const auto *sample = cell.samples.data();
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x, ++sample)
		{
			unsigned weights[4];
			const unsigned largest = sample->slots & 3;
			unsigned rest = 65536;
			for (unsigned slot = 0, other = 0; slot < 4; ++slot)
				if (slot != largest)
				{
					weights[slot] = sample->others[other++];
					rest -= weights[slot];
				}
			weights[largest] = rest;
			std::uint64_t rgb[3] = {};
			unsigned alpha = 0;
			for (unsigned slot = 0; slot < cell.colors; ++slot)
				if (weights[slot] && sources[cell.palette[slot]].pixels)
				{
					const auto &t = sources[cell.palette[slot]];
					const int sx = t.step ? x * t.step : x * t.size / size;
					const int sy = t.step ? y * t.step : y * t.size / size;
					const auto *p = t.pixels[sy * t.size + sx].data();
					const unsigned a = weights[slot] * p[3];
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
			const auto dominant = cell.palette[sample->slots >> 2 & 3];
			const auto neighbor = cell.palette[sample->slots >> 4 & 3];
			const int margin = sample->margin;
			const auto &self = definitions.materials[dominant].seam;
			const auto &other = definitions.materials[neighbor].seam;
			int shade = 256, tint = 0;
			if (neighbor != dominant)
			{
				if (other.cast && other.height > self.height && margin < other.castWidth)
					shade = 256 - other.cast * (other.castWidth - margin) / other.castWidth;
				if (other.fringe && margin < other.fringeWidth)
					tint = other.fringe * (other.fringeWidth - margin) / other.fringeWidth;
			}
			const auto channel = [&](int k)
			{
				// Opaque materials have a constant denominator. Keep the exact
				// integer result while avoiding three variable-width divisions
				// per sample when an animated terrain page is re-blended.
				constexpr unsigned opaqueAlpha = 255u * 65536;
				const unsigned blended = alpha == opaqueAlpha ? unsigned(rgb[k] / opaqueAlpha)
					: unsigned(alpha ? rgb[k] / alpha : 0);
				unsigned value = blended * shade >> 8;
				return value + (unsigned(other.fringeColor[k]) - value) * tint / 256;
			};
			*p = ((alpha + 32768) / 65536 << 24) | (channel(0) << 16) | (channel(1) << 8) |
				 channel(2);
		}
}
Compositor::CellMask Compositor::mask(const Recipe &r, int scale) const
{
	CellMask cell;
	mask(r, scale, cell);
	return cell;
}
void Compositor::mask(const Recipe &r, int scale, CellMask &cell) const
{
	cell.palette = {};
	cell.colors = 0;
	cell.recipe = r;
	cell.scale = scale;
	const auto slotOf = [&](MaterialId id)
	{
		for (unsigned slot = 0; slot < cell.colors; ++slot)
			if (cell.palette[slot] == id)
				return slot;
		throw std::runtime_error("Terrain coverage names a material outside its cell");
	};
	for (auto id : r.corners)
		if (std::find(cell.palette.begin(), cell.palette.begin() + cell.colors, id) ==
			cell.palette.begin() + cell.colors)
			cell.palette[cell.colors++] = id;
	const int size = 32 * scale;
	cell.samples.resize(std::size_t(size) * size);
	auto *sample = cell.samples.data();
	const PreparedCoverage prepared(definitions, r);
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x, ++sample)
		{
			const auto coverage = prepared.at((x * 256 + 128) / scale, (y * 256 + 128) / scale);
			// Entries of one material add linearly in the blend, so they merge.
			unsigned weights[4] = {};
			for (int i = 0; i < 4; ++i)
				if (coverage.weight[i])
					weights[slotOf(coverage.material[i])] += coverage.weight[i];
			unsigned dominant = 0, largest = 0;
			for (unsigned i = 1; i < 4; ++i)
				if (coverage.weight[i] > coverage.weight[dominant])
					dominant = i;
			for (unsigned slot = 1; slot < 4; ++slot)
				if (weights[slot] > weights[largest])
					largest = slot;
			for (unsigned slot = 0, other = 0; slot < 4; ++slot)
				if (slot != largest)
					sample->others[other++] = std::uint16_t(weights[slot]);
			sample->margin = std::uint16_t(std::min(coverage.margin, 65535u));
			sample->slots = std::uint8_t(largest | slotOf(coverage.material[dominant]) << 2 |
										 slotOf(coverage.neighbor) << 4);
		}
}
void Compositor::composeOverview(const Recipe &r, SDL_Surface *target, int ox, int oy,
								 const CornerColors *cornerColors) const
{
	if (target->format != SDL_PIXELFORMAT_ARGB8888)
		throw std::runtime_error("Terrain overview requires ARGB8888");
	const auto packed = [](const auto &color)
	{ return 0xFF000000u | (unsigned(color[0]) << 16) | (unsigned(color[1]) << 8) | color[2]; };
	// Saved custom terrain may carry its own overview palette: a material takes
	// the colour of the first corner drawn with it that names one.
	const auto colorOf = [&](MaterialId id) -> const std::array<unsigned char, 3> &
	{
		if (cornerColors)
			for (unsigned k = 0; k < r.corners.size(); ++k)
				if (r.corners[k] == id && (*cornerColors)[k])
					return *(*cornerColors)[k];
		return definitions.materials[id].preview;
	};
	if (std::all_of(r.corners.begin(), r.corners.end(),
					[&](auto id) { return id == r.corners[0]; }))
	{
		SDL_Rect area{ox, oy, OverviewSamples, OverviewSamples};
		SDL_FillSurfaceRect(target, &area, packed(colorOf(r.corners[0])));
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
