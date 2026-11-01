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
		if (m.variantBlocks > 1) GAGCore::Sprite::deferHighResolution(m.sprite);
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
			for (const auto &frames : {m.decor.full, m.decor.edge, m.decor.connected, m.decor.connectedInterior})
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
	// Shore calm pulls waves toward one colour per material, so the fade
	// stays continuous across variants and does not shift the shore's tone.
	for (unsigned id = 0; id < definitions.materials.size(); ++id)
	{
		auto &m = definitions.materials[id];
		m.shoreCalm = m.surf.period > 0 && m.animationFrames > 1;
		if (!m.shoreCalm)
			continue;
		std::uint64_t sum[3] = {}, count = 0;
		for (const auto &v : m.variants)
		{
			Texture t;
			readTexture(t, sprites[id]->nativeFrame(v.frame));
			for (const auto &p : t.pixels)
				for (int k = 0; k < 3; ++k)
					sum[k] += p[k];
			count += t.pixels.size();
		}
		for (int k = 0; k < 3; ++k)
			m.calmMean[k] = static_cast<unsigned char>((sum[k] + count / 2) / count);
	}
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
    if (auto retained = publishedCpu.lock())
        bytes += retained->bytes;
    else if (!publishedPixels.empty())
        bytes += cpuPublicationBytes();
	return bytes;
}
bool Compositor::prepareMaterial(unsigned id, bool hd, int phase, std::vector<Texture> &target,
                                 const std::vector<unsigned char> *wanted)
{
	const auto &m = definitions.materials[id];
    bool refresh = false, changed = false;
	for (unsigned i = 0; i < m.variants.size(); ++i)
	{
		if (wanted && !(*wanted)[i]) continue;
		const int frame = m.variants[i].frame + phase * m.animationStride;
		auto *source = hd ? sprites[id]->baseFrame(frame) : sprites[id]->nativeFrame(frame);
		if (!source)
			throw std::runtime_error("Missing terrain texture: " + m.key);
		auto &t = target[i];
        const bool sourceChanged = t.source != source || t.identity != source->lifetimeIdentity() ||
                                   t.revision != source->contentRevision();
        // Residency is independent of content: revisiting unchanged art must
        // not invalidate already composed pages elsewhere in the map.
        changed |= t.identity != 0 && sourceChanged;
        refresh |= sourceChanged || t.pixels.empty();
	}
	if (!refresh)
		return false;
    publishedCpu.reset();
	bool packaged = pack != nullptr;
	if (packaged)
		for (unsigned i = 0; i < m.variants.size(); ++i)
		{
            if (wanted && !(*wanted)[i]) continue;
            const auto &variant = m.variants[i];
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
		if (wanted && !(*wanted)[i]) continue;
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
		return changed; // Sources already have their shared variant borders.
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
	return changed;
}
void Compositor::prepare(bool hd, int time)
{
    // The complete catalogue is for one-shot callers: wait for deferred HD
    // windows, then read the published frames.
    prepareSelected(hd, time, nullptr);
    if (!hd) return;
    GAGCore::Sprite::finishHighResolution();
    prepareSelected(hd, time, nullptr);
}
void Compositor::prepareForRecipes(bool hd, int time, std::span<const Recipe> recipes)
{
    Usage usage;
    for (const auto &m : definitions.materials) usage.emplace_back(m.variants.size(), 0);
    for (const auto &r : recipes)
        for (const auto id : r.corners)
            usage[id][definitions.variantIndex(id, r.x, r.y, r.seed)] = 1;
    prepareSelected(hd, time, &usage);
}
void Compositor::prepareVisible(bool hd, int time, const SceneMap &map,
                                int left, int top, int right, int bottom)
{
    Usage usage;
    for (const auto &m : definitions.materials) usage.emplace_back(m.variants.size(), 0);
    const int width = std::min(map.getW(), std::max(0, right - left + 1));
    const int height = std::min(map.getH(), std::max(0, bottom - top + 1));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto r = describe(map, left + x, top + y);
            for (const auto id : r.corners)
                usage[id][definitions.variantIndex(id, r.x, r.y, r.seed)] = 1;
        }
    prepareSelected(hd, time, &usage);
}
void Compositor::prepareSelected(bool hd, int time, const Usage *requested)
{
    // Nonperiodic custom art uses variant zero as its shared boundary source.
    Usage normalized;
    if (requested)
    {
        normalized = *requested;
        for (unsigned id = 0; id < normalized.size(); ++id)
            if (!definitions.materials[id].periodicEdges &&
                std::any_of(normalized[id].begin(), normalized[id].end(), [](auto v) { return v != 0; }))
                normalized[id][0] = 1;
        requested = &normalized;
    }
    // A sprite can back several materials: declare the union before any reads.
    std::map<GAGCore::Sprite *, std::vector<unsigned>> windows;
    for (unsigned id = 0; id < definitions.materials.size(); ++id)
    {
        const auto &m = definitions.materials[id];
        auto &window = windows[sprites[id]];
        if (!hd) continue;
        const unsigned phase = unsigned(time) / m.animationTicks % m.animationFrames;
        for (unsigned i = 0; i < m.variants.size(); ++i)
            if (!requested || (*requested)[id][i])
            {
                window.push_back(m.variants[i].frame + phase * m.animationStride);
                if (m.animationFrames > 1 && m.animationTicks > 1)
                    window.push_back(m.variants[i].frame + (phase + 1) % m.animationFrames * m.animationStride);
            }
    }
    for (auto &[sprite, window] : windows) sprite->setHighResolutionFrames(window);

	int nextResolution = 1;
	// Surf moves once per `ticks`, so entering a new step invalidates its pages
	// too, also when the texture itself is still (dark water).
	const unsigned previousTime = preparedTime;
	preparedTime = unsigned(time);
	for (unsigned id = 0; id < definitions.materials.size(); ++id)
	{
		const auto &m = definitions.materials[id];
		const auto revision = materialRevisions[id];
        const auto *wanted = requested ? &(*requested)[id] : nullptr;
        bool evicted = false;
        const auto evict = [&](auto &set) {
            for (unsigned i = 0; i < set.size(); ++i)
                if (wanted && !(*wanted)[i] && set[i].size)
                {
                    // Keep only source stamps so a later identical reload is
                    // distinguished from an actual artwork/HD revision.
                    set[i].pixels = MaterialPixels{};
                    set[i].size = 0;
                    evicted = true;
                }
        };
        evict(textures[id]);
        evict(animationTextures[id].current);
        evict(animationTextures[id].next);
        if (evicted)
        {
            publishedCpu.reset();
            if (id < publishedPixels.size()) publishedPixels[id].pixels.clear();
        }
        if (wanted && std::none_of(wanted->begin(), wanted->end(), [](auto v) { return v != 0; }))
        {
            continue;
        }
		const int phase = unsigned(time) / m.animationTicks % m.animationFrames;
		auto &animation = animationTextures[id];
		if (animation.current.empty())
		{
			if (prepareMaterial(id, hd, phase, textures[id], wanted)) ++materialRevisions[id];
		}
		else
		{
			// Reuse the previous next phase as the current endpoint on ordinary advances.
			const unsigned first = wanted ? std::find(wanted->begin(), wanted->end(), 1) - wanted->begin() : 0;
            const int frame = m.variants[first].frame + phase * m.animationStride;
			auto *source = hd ? sprites[id]->baseFrame(frame) : sprites[id]->nativeFrame(frame);
			const bool advanced = animation.next[first].source == source;
			if (advanced) animation.current.swap(animation.next);
			const bool currentChanged = prepareMaterial(id, hd, phase, animation.current, wanted);
			const bool nextChanged = prepareMaterial(id, hd, (phase + 1) % m.animationFrames, animation.next, wanted);
			const unsigned blend = unsigned(time) % m.animationTicks;
            bool missingOutput = false;
            for (unsigned i = 0; i < m.variants.size(); ++i)
                if ((!wanted || (*wanted)[i]) && textures[id][i].pixels.empty()) missingOutput = true;
            const bool animationChanged = advanced || currentChanged || nextChanged || animation.blend != blend;
			if (animationChanged || missingOutput)
			{
                if (animationChanged) ++materialRevisions[id];
				animation.blend = blend;
				for (unsigned i = 0; i < m.variants.size(); ++i)
				{
					if (wanted && !(*wanted)[i]) continue;
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
		const unsigned ticks = unsigned(m.surf.ticks);
		if (m.surf.period && unsigned(time) / ticks != previousTime / ticks &&
			materialRevisions[id] == revision)
			++materialRevisions[id];
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
	x &= map.getMaskW();
	y &= map.getMaskH();
	const auto corners = map.cellCorners(x, y);
	std::array<MaterialId, 4> materials;
	for (unsigned k = 0; k < corners.size(); ++k)
	{
		materials[k] = materialFor(map, corners[k]);
		if (!decorSprites[materials[k]] || definitions.materials[materials[k]].decor.full.empty())
			return {0, 0};
	}
	// Choosing one of four corners gives 2+2 borders equal odds and 3+1
	// borders a three-to-one split. Presentation hashing never consumes game RNG.
	const auto chosen = materials[hash(x, y, mapSeedSalt(map.terrainSeed()) ^ 0xd3c04a71u) & 3u];
	return {chosen, unsigned(std::count(materials.begin(), materials.end(), chosen))};
}
GAGCore::Sprite *Compositor::decorSprite(const SceneMap &map, int x, int y) const
{
	const auto [material, count] = decorMaterial(map, x, y);
	return count ? decorSprites[material] : nullptr;
}
int Compositor::decorFrame(const SceneMap &map, int x, int y) const
{
	x &= map.getMaskW();
	y &= map.getMaskH();
	const auto [material, count] = decorMaterial(map, x, y);
	if (!count)
		return -1;
	const auto &decor = definitions.materials[material].decor;
	unsigned mask = 0;
	const auto corners = map.cellCorners(x, y);
	for (unsigned k = 0; k < corners.size(); ++k)
		if (const auto id = materialFor(map, corners[k]);
			id == material ||
			(!decor.connected.empty() && !definitions.materials[id].decor.connected.empty()))
			mask |= 1u << k;
	return definitions.decorFrame(
		material, x, y, count < 4, map.terrainSeed(), mask,
		cornerColours(x, y, map.getMaskW(), map.getMaskH(), map.terrainSeed()));
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
	describeShoreCalm(map, r);
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
void Compositor::describeShoreCalm(const SceneMap &map, Recipe &r) const
{
	if (std::none_of(r.corners.begin(), r.corners.end(),
					 [&](auto id) { return definitions.materials[id].shoreCalm; }))
		return;
	// Vertices around the cell, enough to reach land within the fade of every corner.
	constexpr int R = ShoreCalmSearch, W = 2 * R + 2;
	std::array<bool, W * W> land{};
	for (int y = 0; y < W; ++y)
		for (int x = 0; x < W; ++x)
			land[y * W + x] = definitions.materials[materialFor(
				map, map.vertexTerrainAt(r.x + x - R, r.y + y - R))].surf.period == 0;
	for (int k = 0; k < 4; ++k)
	{
		const int cx = R + (k & 1), cy = R + (k >> 1);
		if (land[cy * W + cx])
		{
			r.calm[k] = 255; // The shoreline runs beside every land corner.
			continue;
		}
		int nearest = 0;
		for (int y = cy - R; y <= cy + R; ++y)
			for (int x = cx - R; x <= cx + R; ++x)
				if (land[y * W + x])
				{
					const int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
					if (!nearest || d2 < nearest)
						nearest = d2;
				}
		r.calm[k] = static_cast<std::uint8_t>(shoreCalmAt(nearest));
	}
	// A cell whose liquid corners are all far from land needs no calm at all;
	// land corners alone keep their ordinary uniform and seam composition.
	bool liquidCalm = false;
	for (int k = 0; k < 4; ++k)
		liquidCalm |= definitions.materials[r.corners[k]].shoreCalm && r.calm[k];
	if (!liquidCalm)
		r.calm = {};
}
Compositor::PreparedMaterialView Compositor::preparedMaterial(const Recipe &recipe, MaterialId id) const
{
    const unsigned variant = definitions.variantIndex(id, recipe.x, recipe.y, recipe.seed);
    const auto &texture = textures.at(id).at(variant);
    return {variant, materialRevisions.at(id), texture.size, texture.pixels};
}
std::size_t Compositor::cpuPublicationBytes() const
{
    // Include the bounded weak tables, revision keys and control blocks even
    // when the last whole snapshot has expired. Admission reserves the maximum
    // table shape so selecting another material cannot grow uncharged metadata.
    // make_shared keeps inline object storage until its last weak reference
    // disappears, including an expired whole snapshot or catalog publication.
    std::size_t bytes = sizeof(CpuMaterials) + sizeof(Catalog) + 256 +
        textures.capacity() * (sizeof(CpuPublication) + sizeof(std::uint64_t));
    for (const auto &set : textures)
    {
        bytes += set.size() * sizeof(std::weak_ptr<const MaterialPixels>);
        // Empty slots have no shared pixel allocation or control block.
        for (const auto &texture : set) if (!texture.pixels.empty()) bytes += 64;
    }
    return bytes;
}
Compositor::Usage Compositor::recipeUsage(std::span<const Recipe> recipes) const
{
    Usage usage;
    for (const auto &m : definitions.materials) usage.emplace_back(m.variants.size(), 0);
    for (const auto &r : recipes)
        for (auto id : r.corners)
            usage[id][definitions.variantIndex(id, r.x, r.y, r.seed)] = 1;
    return usage;
}
std::size_t Compositor::cpuMaterialBytesForRecipes(std::span<const Recipe> recipes) const
{
    const auto usage = recipeUsage(recipes);
    return cpuMaterialBytesSelected({}, &usage);
}
std::shared_ptr<const Compositor::CpuMaterials> Compositor::cpuMaterialsForRecipes(std::span<const Recipe> recipes)
{
    const auto usage = recipeUsage(recipes);
    return cpuMaterialsSelected({}, &usage);
}
std::size_t Compositor::cpuMaterialBytes(std::span<const MaterialId> needed) const
{
    return cpuMaterialBytesSelected(needed, nullptr);
}
std::size_t Compositor::cpuMaterialBytesSelected(std::span<const MaterialId> needed, const Usage *usage) const
{
    // Capacity accounting includes the catalog's nested allocations and
    // shared ownership/control storage; allocator headers receive headroom.
    std::size_t bytes = cpuPublicationBytes() + sizeof(CpuMaterials) + sizeof(Catalog) + 256 +
                        definitions.profiles.capacity() * sizeof(Profile) +
                        definitions.materials.capacity() * sizeof(Material) +
                        definitions.treatments.capacity() * sizeof(PairTreatment) +
                        textures.capacity() * sizeof(std::vector<CpuMaterials::Texture>);
    for (const auto &profile : definitions.profiles)
    {
        bytes +=
            profile.key.capacity() + profile.contours.capacity() * sizeof(std::vector<int>) + 128;
        for (const auto &contour : profile.contours)
            bytes += contour.capacity() * sizeof(int) + 64;
    }
    for (const auto &material : definitions.materials)
        bytes += material.key.capacity() + material.sprite.capacity() +
                 material.variants.capacity() * sizeof(Variant) + material.decor.sprite.capacity() +
                 (material.decor.full.capacity() + material.decor.edge.capacity() + material.decor.connected.capacity() +
                  material.decor.connectedInterior.capacity()) * sizeof(int) +
                 256;
    for (const auto &binding : definitions.bindings)
        bytes += sizeof(binding) + binding.first.capacity() + 128;
    bytes += textures.size();
    for (std::size_t id = 0; id < textures.size(); ++id)
        if (usage ? std::any_of((*usage)[id].begin(), (*usage)[id].end(), [](auto v) { return v != 0; })
                  : needed.empty() || std::find(needed.begin(), needed.end(), id) != needed.end())
        {
            bytes += textures[id].size() * sizeof(CpuMaterials::Texture) * 2;
            for (unsigned variant = 0; variant < textures[id].size(); ++variant)
                if ((!usage || (*usage)[id][variant]) && !textures[id][variant].pixels.empty())
                    bytes += 128 + textures[id][variant].pixels.capacity() * sizeof(MaterialPixels::value_type);
        }
    return bytes;
}
std::shared_ptr<const Compositor::CpuMaterials> Compositor::cpuMaterials(std::span<const MaterialId> needed)
{
    return cpuMaterialsSelected(needed, nullptr);
}
std::shared_ptr<const Compositor::CpuMaterials> Compositor::cpuMaterialsSelected(std::span<const MaterialId> needed,
                                                                             const Usage *usage)
{
    auto previous = publishedCpu.lock();
    std::vector<char> included(textures.size(), !usage && needed.empty());
    for (const auto id : needed) included.at(id) = true;
    if (usage)
        for (unsigned id = 0; id < usage->size(); ++id)
            included[id] = std::any_of((*usage)[id].begin(), (*usage)[id].end(), [](auto v) { return v != 0; });
    if (!usage && !publishedSubset && previous && previous->included == included && publishedRevisions == materialRevisions)
        return previous;
    auto next = std::make_shared<CpuMaterials>();
    next->definitions = publishedCatalog.lock();
    if (!next->definitions)
    {
        // Copy only composition fields: even transient manifest-string copies
        // would exceed the slim snapshot's reserved allocation.
        auto copy = std::make_shared<Catalog>();
        copy->version = definitions.version;
        copy->boundaryWarp = definitions.boundaryWarp;
        copy->profiles = definitions.profiles;
        copy->materials = definitions.materials;
        copy->treatments = definitions.treatments;
        copy->bindings = definitions.bindings;
        next->definitions = std::move(copy);
    }
    publishedCatalog = next->definitions;
    publishedPixels.resize(textures.size());
    next->textures.resize(textures.size());
    next->included = std::move(included);
    for (std::size_t id = 0; id < textures.size(); ++id)
    {
        if (!next->included[id]) continue;
        auto &record = publishedPixels[id];
        if (record.revision != materialRevisions[id])
        {
            record.pixels.assign(textures[id].size(), {});
            record.revision = materialRevisions[id];
        }
        else
            record.pixels.resize(textures[id].size());
        next->textures[id].reserve(textures[id].size());
        for (std::size_t variant = 0; variant < textures[id].size(); ++variant)
        {
            const auto &texture = textures[id][variant];
            if ((usage && !(*usage)[id][variant]) || texture.pixels.empty())
            {
                next->textures[id].push_back({});
                continue;
            }
            auto pixels = record.pixels[variant].lock();
            if (!pixels)
            {
                pixels = std::make_shared<const MaterialPixels>(texture.pixels);
                record.pixels[variant] = pixels;
            }
            next->textures[id].push_back({texture.size, std::move(pixels)});
        }
    }
    next->bytes = cpuMaterialBytesSelected(needed, usage);
    publishedSubset = usage != nullptr;
    next->surfTime = preparedTime;
    publishedRevisions = materialRevisions;
    publishedCpu = next;
    return next;
}
void Compositor::CpuMaterials::compose(const Recipe &recipe, SDL_Surface *target, int scale,
                                       const CellMask *mask) const
{
    CpuScratch scratch;
    compose(recipe, target, scale, scratch, mask);
}
void Compositor::CpuMaterials::compose(const Recipe &recipe, SDL_Surface *target, int scale,
                                       CpuScratch &scratch, const CellMask *mask) const
{
    scratch.sources.resize(definitions->materials.size());
    auto &sources = scratch.sources;
    for (auto id : recipe.corners)
    {
        const auto variant = definitions->variantIndex(id, recipe.x, recipe.y, recipe.seed);
        const auto &texture = textures.at(id).at(variant);
        sources[id] = {variant, 0, texture.size, *texture.pixels,
                       texture.size % (32 * scale) ? 0 : texture.size / (32 * scale)};
    }
    if (!mask && std::any_of(recipe.corners.begin(), recipe.corners.end(),
                             [&](auto id) { return id != recipe.corners[0]; }))
    {
        buildMask(*definitions, recipe, scale, scratch.mask);
        mask = &scratch.mask;
    }
    composePixels(*definitions, recipe, target, 0, 0, scale, sources, mask, surfTime);
}
Compositor::CellMask Compositor::CpuMaterials::mask(const Recipe &recipe, int scale) const
{
    CellMask result;
    buildMask(*definitions, recipe, scale, result);
    return result;
}
void Compositor::compose(const Recipe &r, SDL_Surface *target, int ox, int oy, int scale,
						 const CellMask *mask) const
{
    thread_local std::vector<PreparedMaterialView> sources;
    sources.resize(definitions.materials.size());
    for (auto id : r.corners)
    {
        sources[id] = preparedMaterial(r, id);
        sources[id].step = sources[id].size % (32 * scale) ? 0 : sources[id].size / (32 * scale);
    }
    composePixels(definitions, r, target, ox, oy, scale, sources, mask, preparedTime);
}
void Compositor::composePixels(const Catalog &definitions, const Recipe &r, SDL_Surface *target,
                               int ox, int oy, int scale,
                               std::span<const PreparedMaterialView> prepared, const CellMask *mask,
                               unsigned surfTime)
{
	if (target->format != SDL_PIXELFORMAT_ARGB8888)
		throw std::runtime_error("Terrain compositor requires ARGB8888");
	const int size = 32 * scale;
	// Resolve immutable pixel storage once per tile, rather than through
	// vector/array accessors for every channel of every instrumented pixel.
    const auto *sources = prepared.data();
	const bool uniform = std::all_of(r.corners.begin(), r.corners.end(),
									 [&](auto id) { return id == r.corners[0]; });
	// Q8 shore calm of a sample, for animated liquids near land.
	const unsigned calmStrength = 256 - ShoreCalmContrastQ8;
	const auto calmAt = [&](int x, int y)
	{
		return shoreCalm(r.calm, unsigned(2 * x + 1) * 128 / size, unsigned(2 * y + 1) * 128 / size);
	};
	if (uniform)
	{
		const auto &texture = sources[r.corners[0]];
		const auto &material = definitions.materials[r.corners[0]];
		if (material.shoreCalm && r.calmed() && !texture.pixels.empty())
		{
			for (int y = 0; y < size; ++y)
			{
				auto *row = reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(target->pixels) +
					(oy + y) * target->pitch) + ox;
				for (int x = 0; x < size; ++x)
				{
					const int sx = texture.step ? x * texture.step : x * texture.size / size;
					const int sy = texture.step ? y * texture.step : y * texture.size / size;
					const auto *p = texture.pixels[sy * texture.size + sx].data();
					unsigned rgb[3] = {p[0], p[1], p[2]};
					applyShoreCalm(calmAt(x, y), calmStrength, material.calmMean, rgb);
					row[x] = (unsigned(p[3]) << 24) | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
				}
			}
			return;
		}
		// Native and HD sources usually divide the composition grid exactly.
		// Walk their texels directly instead of dividing twice per output pixel.
        if (!texture.pixels.empty() && texture.step)
		{
			for (int y = 0; y < size; ++y)
			{
				auto *row = reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(target->pixels) +
					(oy + y) * target->pitch) + ox;
                const auto *source = texture.pixels.data() + y * texture.step * texture.size;
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
                if (texture.pixels.empty())
					row[x] = 0;
				else
				{
                    const auto *p = texture
                                        .pixels[(y * texture.size / size) * texture.size +
                                                x * texture.size / size]
                                        .data();
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
        buildMask(definitions, r, scale, scratch);
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
                if (weights[slot] && !sources[cell.palette[slot]].pixels.empty())
				{
					const auto &t = sources[cell.palette[slot]];
					const int sx = t.step ? x * t.step : x * t.size / size;
					const int sy = t.step ? y * t.step : y * t.size / size;
					const auto *p = t.pixels[sy * t.size + sx].data();
					const unsigned a = weights[slot] * p[3];
					alpha += a;
					unsigned source[3] = {p[0], p[1], p[2]};
					const auto &material = definitions.materials[cell.palette[slot]];
					if (material.shoreCalm && r.calmed())
						applyShoreCalm(calmAt(x, y), calmStrength, material.calmMean, source);
					for (int k = 0; k < 3; ++k)
						rgb[k] += std::uint64_t(source[k]) * a;
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
				// Connected canopies supply their own raised edge and contact shadow.
				// Casting again from the rough undergrowth contour makes teeth and
				// hairlines protrude beyond the foliage.
				if (definitions.materials[neighbor].decor.connected.empty() && other.cast &&
					other.height > self.height && margin < other.castWidth)
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
			unsigned value[3] = {channel(0), channel(1), channel(2)};
			const auto &selfSurf = definitions.materials[dominant].surf;
			const auto &otherSurf = definitions.materials[neighbor].surf;
			if ((selfSurf.period > 0) != (otherSurf.period > 0))
			{
				// Q8 world pixels of the sample centre, as the coverage samples it.
				const int wx = r.x * 8192 + (2 * x + 1) * 4096 / size;
				const int wy = r.y * 8192 + (2 * y + 1) * 4096 / size;
				applySurf(selfSurf.period ? selfSurf : otherSurf, selfSurf.period > 0, margin, wx,
						  wy, surfTime, value);
			}
			*p = ((alpha + 32768) / 65536 << 24) | (value[0] << 16) | (value[1] << 8) | value[2];
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
    buildMask(definitions, r, scale, cell);
}
void Compositor::buildMask(const Catalog &definitions, const Recipe &r, int scale, CellMask &cell)
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
