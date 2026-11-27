// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainCompositor.h"
#include "TerrainArtBorder.h"
#include "TerrainBuiltins.h"
#include <atomic>
#include <unordered_map>
#include "map/TerrainRegistry.h"
#include "TerrainCompiledPack.h"
#include "TerrainPresentation.h"
#include "render/scene/SceneMap.h"
#include <Toolkit.h>
#include <SteadyClock.h>
#include <HighResolutionPolicy.h>
#ifdef __EMSCRIPTEN__
#include <ApplicationHost.h>
#include <bitset>
#endif
#include <set>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cassert>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <limits>
#include <SDL3/SDL_timer.h>

namespace TerrainVisual
{
namespace
{
// Numbers compositors for presentation caches that outlive one of them.
std::atomic<std::uint64_t> compositorInstances{0};
} // namespace
namespace
{
// MaterialSheets state. Sheets are Toolkit sprites; these records only say
// which were loaded on demand, when they were last used and their size.
struct SheetRecord
{
	GAGCore::Sprite *sprite = nullptr; // Null while loading.
	std::size_t bytes = 0;
	std::int64_t used = 0;
};
std::map<std::string, SheetRecord> sheetRecords;
std::map<const void *, std::vector<std::string>> sheetOwners;
std::uint64_t sheetGenerationCounter = 1;
std::int64_t sheetClock = 0, sheetCollected = 0;
std::size_t sheetBytes(GAGCore::Sprite &sprite)
{
	std::size_t bytes = 0;
	for (int i = 0; i < sprite.getFrameCount(); ++i)
		if (auto *frame = sprite.nativeFrame(unsigned(i)); frame && frame->hasPixels())
			bytes += std::size_t(frame->getW()) * frame->getH() * 4;
	return bytes;
}
bool sheetRetained(const std::string &name)
{
	for (const auto &[owner, names] : sheetOwners)
		if (std::binary_search(names.begin(), names.end(), name))
			return true;
	return false;
}
// The fallback texture's stamp: never a surface's identity (those count up from 1).
constexpr std::uint64_t FallbackIdentity = std::uint64_t(1) << 63;
} // namespace

GAGCore::Sprite *MaterialSheets::loaded(const std::string &name)
{
	const auto found = sheetRecords.find(name);
	if (found == sheetRecords.end() || !found->second.sprite)
		return nullptr;
	// Toolkit::close deletes every sprite; a record never outlives its sprite.
	if (GAGCore::Toolkit::findSprite(name) != found->second.sprite)
	{
		sheetRecords.erase(found);
		++sheetGenerationCounter;
		return nullptr;
	}
	return found->second.sprite;
}
GAGCore::Sprite *MaterialSheets::find(const std::string &name)
{
	if (auto *sprite = loaded(name))
		return sprite;
	if (GAGCore::Toolkit::spriteFailed(name))
		return nullptr;
	auto &record = sheetRecords[name];
	record.used = GAGCore::steadyClockNanoseconds();
	if (auto *sprite = GAGCore::Toolkit::findSprite(name))
	{
		// Already loaded (by an earlier request or a tool): adopt it.
		record.sprite = sprite;
		record.bytes = sheetBytes(*sprite);
		++sheetGenerationCounter;
		return sprite;
	}
	GAGCore::Toolkit::requestSprite(name);
	return nullptr;
}
bool MaterialSheets::failed(const std::string &name)
{
	return !loaded(name) && GAGCore::Toolkit::spriteFailed(name);
}
void MaterialSheets::finish(std::span<const std::string> names)
{
	for (const auto &name : names)
		find(name);
	std::uint64_t since = 0;
	bool reported = false;
	for (const auto &name : names)
		while (!loaded(name) && !GAGCore::Toolkit::spriteFailed(name))
		{
			if (!GAGCore::Toolkit::spriteLoading(name) && !GAGCore::Toolkit::findSprite(name))
				find(name);
			GAGCore::Toolkit::pollAssets(4);
			poll();
			GAGCore::Toolkit::reportSpriteStall("material-sheet " + name, since, reported);
#ifndef __EMSCRIPTEN__
			if (!loaded(name)) SDL_Delay(1);
#endif
		}
}
std::uint64_t MaterialSheets::generation() { return sheetGenerationCounter; }
void MaterialSheets::touch(const std::string &name)
{
	if (const auto found = sheetRecords.find(name); found != sheetRecords.end())
		found->second.used = GAGCore::steadyClockNanoseconds();
}
void MaterialSheets::poll()
{
	sheetClock = GAGCore::steadyClockNanoseconds();
	for (auto it = sheetRecords.begin(); it != sheetRecords.end();)
	{
		auto &record = it->second;
		if (record.sprite)
		{
			if (GAGCore::Toolkit::findSprite(it->first) != record.sprite)
			{
				it = sheetRecords.erase(it);
				++sheetGenerationCounter;
				continue;
			}
		}
		else if (auto *sprite = GAGCore::Toolkit::findSprite(it->first))
		{
			record.sprite = sprite;
			record.bytes = sheetBytes(*sprite);
			record.used = sheetClock;
			++sheetGenerationCounter;
		}
		else if (!GAGCore::Toolkit::spriteLoading(it->first))
		{
			// Failed (Toolkit remembers it) or forgotten by a Toolkit restart.
			it = sheetRecords.erase(it);
			continue;
		}
		++it;
	}
	// Releasing is not urgent: look a few times a second.
	if (sheetClock - sheetCollected < 250'000'000)
		return;
	sheetCollected = sheetClock;
	const auto budget = GAGCore::Sprite::retiredHighResolutionBudget();
	const std::size_t retiredFrames = GAGCore::Sprite::retiredHighResolutionBytes();
	std::vector<std::map<std::string, SheetRecord>::iterator> unretained;
	std::size_t bytes = 0;
	for (auto it = sheetRecords.begin(); it != sheetRecords.end(); ++it)
		if (it->second.sprite && !sheetRetained(it->first))
		{
			unretained.push_back(it);
			bytes += it->second.bytes;
		}
	// Oldest first. A sheet used within the last second may still be in a
	// pending draw, so only age releases sheets that recently used.
	std::sort(unretained.begin(), unretained.end(),
			  [](auto a, auto b) { return a->second.used < b->second.used; });
	for (auto it : unretained)
	{
		const auto age = sheetClock - it->second.used;
		const bool expired = age >= budget.lifetimeNanoseconds;
		const bool overBudget = retiredFrames + bytes > budget.bytes && age >= 1'000'000'000;
		if (!expired && !overBudget)
			continue;
		bytes -= it->second.bytes;
		const auto name = it->first;
		sheetRecords.erase(it);
		GAGCore::Toolkit::releaseSprite(name);
		++sheetGenerationCounter;
	}
}
void MaterialSheets::retain(const void *owner, std::vector<std::string> names)
{
	std::sort(names.begin(), names.end());
	sheetOwners[owner] = std::move(names);
}
void MaterialSheets::forget(const void *owner) { sheetOwners.erase(owner); }
std::size_t MaterialSheets::residentBytes()
{
	std::size_t bytes = 0;
	for (const auto &[name, record] : sheetRecords)
		bytes += record.sprite ? record.bytes : 0;
	return bytes;
}
std::size_t MaterialSheets::loading()
{
	return std::size_t(std::count_if(sheetRecords.begin(), sheetRecords.end(),
									 [](const auto &record) { return !record.second.sprite; }));
}
std::size_t MaterialSheets::unretainedBytes()
{
	std::size_t bytes = 0;
	for (const auto &[name, record] : sheetRecords)
		if (record.sprite && !sheetRetained(name))
			bytes += record.bytes;
	return bytes;
}

Compositor::~Compositor() { MaterialSheets::forget(this); }
Compositor::Compositor(Catalog catalog, std::shared_ptr<const MapAssetBundle> assets, bool onDemand)
    : definitions(std::move(catalog)), customSprites(std::move(assets))
{
	hasNeighborhoodProfiles = std::any_of(definitions.profiles.begin(), definitions.profiles.end(),
		[](const auto &profile) { return profile.shape != BoundaryShape::Patch; });
	pack = CompiledPack::load(definitions);
	stoneMaps = std::make_shared<const StoneMaps>(definitions);
	instance = ++compositorInstances;
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
		GAGCore::Sprite::keepHighResolutionPixels(m.sprite); // prepareMaterial reads them.
		// Map asset bundles carry their own sheets, already in memory.
		const bool lazy = onDemand && !m.sprite.starts_with("data/sets/");
		// A sheet loaded mid-game decodes HD frames only once a page asks.
		if (lazy) GAGCore::Sprite::loadHighResolutionOnDemand(m.sprite);
		lazySheets.push_back(lazy);
		invalidSheets.push_back(0);
		GAGCore::Sprite *sprite = nullptr;
		if (!lazy)
		{
			sprite = customSprites.resolve(m.sprite);
			if (!sprite)
				throw std::runtime_error("Missing terrain material sprite: " + m.sprite);
		}
		sprites.push_back(sprite);
		if (!m.decor.sprite.empty())
		{
			GAGCore::Sprite::deferHighResolution(m.decor.sprite);
			auto *decor = customSprites.resolve(m.decor.sprite);
			if (!decor)
				throw std::runtime_error("Missing terrain decor sprite: " + m.key);
			tieredDecor |= m.decor.connectedTiers;
			for (const auto &frames : {m.decor.full, m.decor.edge, m.decor.connected, m.decor.connectedInterior,
				 m.decor.connectedForeign})
				for (int frame : frames)
				{
					if (frame >= decor->getFrameCount() || decor->getW(frame) > MaximumDecorFrame ||
						decor->getH(frame) > MaximumDecorFrame)
						throw std::runtime_error("Terrain decor frame missing or larger than 128x128: " +
												 m.key);
					reach.include(decor->getW(frame), decor->getH(frame));
				}
			if (sharedDecorSprite && sharedDecorSprite != decor) mixedDecorSprites = true;
			// Terraces draw the connected frames up to MaxDecorTiers rises higher.
			if (m.decor.connectedTiers && !m.decor.connected.empty())
				reach.up = std::max(reach.up, decorOffsetY(decor->getH(m.decor.connected.front())) +
					MaxDecorTiers * DecorTierRise);
            sharedDecorSprite = decor;
		}
		decorSprites.push_back(m.decor.sprite.empty() ? nullptr : customSprites.resolve(m.decor.sprite));
		if (!m.overlay.sprite.empty())
		{
			auto *overlay = customSprites.resolve(m.overlay.sprite);
			if (!overlay)
				throw std::runtime_error("Missing terrain overlay sprite: " + m.key);
			for (const auto &cycle : m.overlay.cycles)
				for (int frame : cycle)
					if (frame >= 0 && (frame >= overlay->getFrameCount() || overlay->getW(frame) > 64 ||
									   overlay->getH(frame) > 64))
						throw std::runtime_error("Terrain overlay frame missing or larger than 64x64: " +
												 m.key);
			if (sharedOverlaySprite && sharedOverlaySprite != overlay) mixedOverlaySprites = true;
			sharedOverlaySprite = overlay;
		}
		overlaySprites.push_back(m.overlay.sprite.empty() ? nullptr : customSprites.resolve(m.overlay.sprite));
		textures.emplace_back(m.variants.size());
		animationTextures.emplace_back();
		if (m.animationFrames > 1 && m.animationTicks > 1)
		{
			animationTextures.back().current.resize(m.variants.size());
			animationTextures.back().next.resize(m.variants.size());
		}
		textureRevisions.push_back(0);
		highResolutionSizes.push_back(0);
	}
    if (mixedDecorSprites) sharedDecorSprite = nullptr;
	if (mixedOverlaySprites) sharedOverlaySprite = nullptr;
	for (unsigned id = 0; id < definitions.materials.size(); ++id)
	{
		auto &m = definitions.materials[id];
		m.shoreCalm = m.surf.period > 0 && m.animationFrames > 1;
		// Until its sheet arrives, a material's waves calm toward its preview.
		m.calmMean = m.preview;
		if (sprites[id])
			adoptSheet(id, sprites[id]); // Eager: a bad sheet throws here.
	}
	sheetGeneration = MaterialSheets::generation();
}
void Compositor::adoptSheet(unsigned id, GAGCore::Sprite *sprite)
{
	auto &m = definitions.materials[id];
	for (const auto &v : m.variants)
		for (int p = 0; p < m.animationFrames; ++p)
		{
			const int frame = v.frame + p * m.animationStride;
			if (frame >= sprite->getFrameCount() || sprite->getW(frame) != 32 ||
				sprite->getH(frame) != 32 || !sprite->nativeFrame(frame))
				throw std::runtime_error("Terrain material requires a 32x32 logical frame: " +
										 m.key);
		}
	for (const auto &v : m.variants)
		for (int p = 0; p < m.animationFrames; ++p)
		{
			auto *native = sprite->nativeFrame(v.frame + p * m.animationStride);
			cleanSources[native->lifetimeIdentity()] = native->contentRevision();
		}
	// Shore calm pulls waves toward one colour per material, so the fade
	// stays continuous across variants and does not shift the shore's tone.
	if (!m.shoreCalm)
		return;
	std::uint64_t sum[3] = {}, count = 0;
	for (const auto &v : m.variants)
	{
		Texture t;
		readTexture(t, sprite->nativeFrame(v.frame));
		for (const auto &p : t.pixels)
			for (int k = 0; k < 3; ++k)
				sum[k] += p[k];
		count += t.pixels.size();
	}
	for (int k = 0; k < 3; ++k)
		m.calmMean[k] = static_cast<unsigned char>((sum[k] + count / 2) / count);
}
GAGCore::Sprite *Compositor::sheet(unsigned id) const
{
	auto *sprite = sprites[id];
	// A released sheet's pointer is stale until refreshSheets runs.
	if (lazySheets[id] && sprite && sheetGeneration != MaterialSheets::generation() &&
		MaterialSheets::loaded(definitions.materials[id].sprite) != sprite)
		return nullptr;
	return sprite;
}
void Compositor::wantSheet(unsigned id) const
{
	if (!lazySheets[id] || invalidSheets[id])
		return;
	if (sprites[id])
		MaterialSheets::touch(definitions.materials[id].sprite);
	else
		MaterialSheets::find(definitions.materials[id].sprite);
}
bool Compositor::sheetsReady(const Recipe &recipe) const
{
	std::array<MaterialId, 4> palette;
	bool ready = true;
	for (unsigned i = 0, n = recipe.palette(palette); i < n; ++i)
	{
		const auto id = palette[i];
		if (!lazySheets[id] || invalidSheets[id] || sheet(id))
			continue;
		if (MaterialSheets::failed(definitions.materials[id].sprite))
			continue;
		wantSheet(id);
		ready = false;
	}
	return ready;
}
void Compositor::refreshSheets()
{
	if (std::none_of(lazySheets.begin(), lazySheets.end(), [](char lazy) { return lazy; }))
		return;
	MaterialSheets::poll();
	while (!prefetchSheets.empty() && MaterialSheets::loading() < 2)
	{
		MaterialSheets::find(prefetchSheets.back());
		prefetchSheets.pop_back();
	}
	const auto generation = MaterialSheets::generation();
	if (generation == sheetGeneration)
		return;
	sheetGeneration = generation;
	bool changed = false;
	for (unsigned id = 0; id < sprites.size(); ++id)
	{
		if (!lazySheets[id] || invalidSheets[id])
			continue;
		auto *sprite = MaterialSheets::loaded(definitions.materials[id].sprite);
		if (sprite == sprites[id])
			continue;
		sprites[id] = nullptr;
		if (sprite)
		{
			try
			{
				adoptSheet(id, sprite);
				sprites[id] = sprite;
				++sheetArrivals;
			}
			catch (const std::exception &error)
			{
				std::fprintf(stderr, "TERRAIN_SHEET_INVALID %s\n", error.what());
				invalidSheets[id] = 1;
			}
		}
		// Textures prepared from the old sheet (or the fallback) are refreshed
		// by prepareMaterial, which compares their source stamps.
		++textureRevisions[id];
		changed = true;
	}
	if (changed)
	{
		// The published catalog carries calmMean.
		publishedCpu.reset();
		publishedCatalog.reset();
	}
}
void Compositor::fallbackTexture(unsigned id, Texture &t) const
{
	const auto &preview = definitions.materials[id].preview;
	t.pixels = MaterialPixels(32 * 32, {preview[0], preview[1], preview[2], 255});
	t.size = 32;
	t.source = nullptr;
	t.identity = FallbackIdentity | id;
	t.revision = 0;
	t.opaque = true;
}
std::vector<std::string> Compositor::presentSheets() const
{
	std::vector<std::string> names;
	for (unsigned id = 0; id < scan.present.size(); ++id)
		if (scan.present[id] && lazySheets[id])
			names.push_back(definitions.materials[id].sprite);
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}
bool Compositor::advanceScan(const SceneMap &map, std::size_t vertexBudget)
{
	if (scan.identity != map.identity() || scan.present.size() != definitions.materials.size())
	{
		scan = {};
		prefetchSheets.clear();
		scan.identity = map.identity();
		scan.present.assign(definitions.materials.size(), 0);
		scan.scanning = true;
	}
	else if (scan.revision != map.terrainRevision() && !scan.scanning)
	{
		// An edit can add materials: scan again, keeping those found.
		scan.scanning = true;
		scan.row = 0;
	}
	scan.revision = map.terrainRevision();
	if (!scan.scanning)
		return scan.complete;
	if (scan.seen.empty())
		scan.seen.assign(std::size_t(1) << 16, 0);
	const int width = map.getW(), height = map.getH();
	std::size_t visited = 0;
	while (scan.row < height && visited < vertexBudget)
	{
		for (int x = 0; x < width; ++x)
		{
			const auto type = map.vertexTerrainAt(x, scan.row);
			if (scan.seen[type])
				continue;
			scan.seen[type] = 1;
			scan.present[materialFor(map, type)] = 1;
		}
		visited += std::size_t(width);
		++scan.row;
	}
	if (scan.row < height)
		return scan.complete;
	scan.scanning = false;
	scan.complete = true;
	std::vector<char>().swap(scan.seen);
	auto names = presentSheets();
	prefetchSheets.assign(names.rbegin(), names.rend());
	MaterialSheets::retain(this, std::move(names));
	return true;
}
bool Compositor::materialsReady(const SceneMap &map)
{
	refreshSheets();
	if (!advanceScan(map, 65536))
		return false;
	for (unsigned id = 0; id < scan.present.size(); ++id)
		if (scan.present[id] && lazySheets[id] && !invalidSheets[id] && !sprites[id] &&
			!MaterialSheets::failed(definitions.materials[id].sprite))
			return false;
	return true;
}
void Compositor::finishMaterials(const SceneMap &map)
{
	if (std::none_of(lazySheets.begin(), lazySheets.end(), [](char lazy) { return lazy; }))
		return;
	advanceScan(map, std::numeric_limits<std::size_t>::max());
	const auto names = presentSheets();
	MaterialSheets::finish(names);
	refreshSheets();
}
void Compositor::finishMaterials(std::span<const MaterialId> ids)
{
	std::vector<std::string> names;
	for (const auto id : ids)
		if (lazySheets.at(id) && !invalidSheets[id])
			names.push_back(definitions.materials[id].sprite);
	if (names.empty())
		return;
	MaterialSheets::finish(names);
	refreshSheets();
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
	markOpaque(t);
}
void Compositor::markOpaque(Texture &t)
{
	t.opaque = std::all_of(t.pixels.begin(), t.pixels.end(), [](const auto &p) { return p[3] == 255; });
}
std::pair<GAGCore::Sprite *, unsigned> Compositor::editorIcon(TerrainType type)
{
	refreshSheets();
	const auto id = terrainBindings[unsigned(type)];
	wantSheet(id);
	return {sheet(id), unsigned(definitions.materials[id].variants.front().frame)};
}
void Compositor::drawEditorIcon(GAGCore::GraphicContext &target, int x, int y, TerrainType type)
{
	const auto [sprite, frame] = editorIcon(type);
	if (sprite)
	{
		target.drawSprite(x, y, sprite, frame);
		target.finishDrawingSprite(sprite, 255);
		return;
	}
	const auto &preview = definitions.materials[terrainBindings[unsigned(type)]].preview;
	target.drawFilledRect(x, y, 32, 32, GAGCore::Color(preview[0], preview[1], preview[2]));
}
Compositor::MaterialPixels Compositor::nativeMaterialPixels(MaterialId id, unsigned variant, unsigned phase)
{
    refreshSheets();
    const auto count = definitions.materials.at(id).variants.size();
    std::vector<Texture> prepared(count);
    std::vector<unsigned char> wanted(count);
    wanted.at(variant) = 1;
    wanted.front() = 1; // Variant zero defines the shared edge band.
    prepareMaterial(id, false, phase, prepared, &wanted);
    return prepared[variant].pixels.take();
}
Compositor::PhaseSource Compositor::phaseSource(MaterialId id, unsigned variant, unsigned phase, bool native) const
{
    const auto &material = definitions.materials.at(id);
    const auto frame = material.variants.at(variant).frame + phase * material.animationStride;
    auto *sprite = sheet(id);
    // A sheet still loading answers with the flat fallback's stamp, which
    // its arrival replaces.
    wantSheet(id);
    if (!sprite) return {FallbackIdentity | id, 0, 32, 0, 0};
    const auto frameOf = [&](unsigned index) { return native ? sprite->nativeFrame(index) : sprite->baseFrame(index); };
    auto *source = frameOf(frame);
    if (!source) throw std::runtime_error("Missing animated terrain source");
    auto *edge = material.periodicEdges ? nullptr : frameOf(
        material.variants.front().frame + phase * material.animationStride);
    return {source->lifetimeIdentity(), source->contentRevision(), source->getW(),
        edge ? edge->lifetimeIdentity() : 0, edge ? edge->contentRevision() : 0};
}
Compositor::MaterialPixels Compositor::phasePixels(MaterialId id, unsigned variant, unsigned phase, bool native)
{
    refreshSheets();
    const auto count = definitions.materials.at(id).variants.size();
    std::vector<Texture> prepared(count);
    std::vector<unsigned char> wanted(count);
    wanted.at(variant) = 1;
    wanted.front() = 1;
    prepareMaterial(id, !native, phase, prepared, &wanted);
    return prepared[variant].pixels.take();
}
const SDL_Surface *Compositor::phaseSurface(MaterialId id, unsigned variant, unsigned phase, bool native) const
{
    const auto &material = definitions.materials.at(id);
    // Other art takes its border from variant zero, which prepareMaterial blends in.
    if (!material.periodicEdges) return nullptr;
    const auto frame = material.variants.at(variant).frame + phase * material.animationStride;
    auto *sprite = sheet(id);
    if (!sprite) return nullptr;
    auto *source = native ? sprite->nativeFrame(frame) : sprite->baseFrame(frame);
    const auto *pixels = source ? source->getSDLSurface() : nullptr;
    if (!pixels || pixels->format != SDL_PIXELFORMAT_ARGB8888 || pixels->w != pixels->h ||
        pixels->w < 32 || pixels->w > 512 || pixels->pitch != pixels->w * 4)
        return nullptr;
    return pixels;
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
    {
        // The snapshot shares unchanged textures; count only pixels that a
        // prepare since replaced, which it alone keeps alive.
        bytes += cpuPublicationBytes();
        for (std::size_t id = 0; id < retained->textures.size(); ++id)
            for (std::size_t variant = 0; variant < retained->textures[id].size(); ++variant)
            {
                const auto &pixels = retained->textures[id][variant].pixels;
                if (pixels && !(id < textures.size() && variant < textures[id].size() &&
                                textures[id][variant].pixels.share() == pixels))
                    bytes += pixels->capacity() * sizeof(MaterialPixels::value_type);
            }
    }
    else if (!publishedPixels.empty())
        bytes += cpuPublicationBytes();
	return bytes;
}
bool Compositor::prepareMaterial(unsigned id, bool hd, int phase, std::vector<Texture> &target,
                                 const std::vector<unsigned char> *wanted)
{
	const auto &m = definitions.materials[id];
    bool refresh = false, changed = false;
    auto *sprite = sheet(id);
    wantSheet(id);
    if (!sprite)
    {
        // The sheet is loading (or failed): its preview colour stands in,
        // stamped so that the sheet's arrival counts as a change.
        for (unsigned i = 0; i < m.variants.size(); ++i)
        {
            if (wanted && !(*wanted)[i]) continue;
            auto &t = target[i];
            if (t.identity == (FallbackIdentity | id) && !t.pixels.empty()) continue;
            changed |= t.identity != 0 && t.identity != (FallbackIdentity | id);
            fallbackTexture(id, t);
            refresh = true;
        }
        if (refresh)
        {
            publishedCpu.reset();
            ++sheetFallbacks;
        }
        return changed;
    }
	for (unsigned i = 0; i < m.variants.size(); ++i)
	{
		if (wanted && !(*wanted)[i]) continue;
		const int frame = m.variants[i].frame + phase * m.animationStride;
		auto *source = hd ? sprite->baseFrame(frame) : sprite->nativeFrame(frame);
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
			auto *source = hd ? sprite->baseFrame(frame) : sprite->nativeFrame(frame);
			const auto clean = cleanSources.find(source->lifetimeIdentity());
			packaged &= source == sprite->nativeFrame(frame) &&
						clean != cleanSources.end() &&
						clean->second == source->contentRevision() &&
						pack->matches(m.sprite + std::to_string(frame) + ".png",
									  source->getSDLSurface());
		}
	for (unsigned i = 0; i < m.variants.size(); ++i)
	{
		if (wanted && !(*wanted)[i]) continue;
		const int frame = m.variants[i].frame + phase * m.animationStride;
		auto *source = hd ? sprite->baseFrame(frame) : sprite->nativeFrame(frame);
		auto &t = target[i];
		if (packaged)
		{
			pack->read(m.sprite + std::to_string(frame) + ".png", t.pixels.mutate());
			markOpaque(t);
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
	{
		auto &pixels = t.pixels.mutate(); // Copies the master's shared pixels once.
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
				auto &destination = pixels[y * t.size + x];
				const unsigned alpha = p[3] * (band - distance) + destination[3] * distance;
				for (int k = 0; k < 3; ++k)
					destination[k] = alpha ? (p[k] * p[3] * (band - distance) +
											  destination[k] * destination[3] * distance) /
												 alpha
										   : 0;
				destination[3] = alpha / band;
			}
	}
	for (auto &t : target) markOpaque(t);
	return changed;
}
void Compositor::prepare(bool hd, int time)
{
    // One-shot callers draw the whole catalogue: every sheet first.
    std::vector<MaterialId> all(definitions.materials.size());
    for (unsigned id = 0; id < all.size(); ++id) all[id] = MaterialId(id);
    finishMaterials(all);
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
    {
        std::array<MaterialId, 4> palette;
        for (unsigned i = 0, n = r.palette(palette); i < n; ++i)
            usage[palette[i]][definitions.variantIndex(palette[i], r.x, r.y, r.seed)] = 1;
    }
    // Recipe callers (editor swatches) keep what they compose: wait for its sheets.
    std::vector<MaterialId> needed;
    for (unsigned id = 0; id < usage.size(); ++id)
        if (std::any_of(usage[id].begin(), usage[id].end(), [](auto v) { return v != 0; }))
            needed.push_back(MaterialId(id));
    finishMaterials(needed);
    prepareSelected(hd, time, &usage);
}
#ifdef __EMSCRIPTEN__
static void requestTerrainPackage(const std::string& package)
{
    if (!GAGCore::ApplicationHost::assetPackageReady(package.c_str()))
        GAGCore::ApplicationHost::requestAssetPackage(package.c_str());
}
void Compositor::requestMapTerrain(const SceneMap& map)
{
    if (requestedMapIdentity != map.identity()) {
        requestedMapIdentity = map.identity();
        requestedTerrainPackages.clear();
        std::vector<bool> present(definitions.materials.size());
        std::bitset<1u << 16> seen;
        for (int y = 0; y < map.getH(); ++y)
            for (int x = 0; x < map.getW(); ++x) {
                const auto type = map.vertexTerrainAt(x, y);
                if (seen[type]) continue;
                seen.set(type);
                present[materialFor(map, type)] = true;
            }
        for (size_t id = 0; id < present.size(); ++id) {
            const auto& sprite = definitions.materials[id].sprite;
            if (present[id] && sprite.starts_with("data/gfx/terrain-"))
                requestedTerrainPackages.push_back("hd-" + sprite.substr(9));
        }
    }
    for (const auto& package : requestedTerrainPackages) requestTerrainPackage(package);
}
#endif
void Compositor::prepareVisible(bool hd, int time, const SceneMap &map,
                                int left, int top, int right, int bottom, bool nativeComposition,
                                bool decor, std::span<const SDL_Point> extraPages)
{
#ifdef __EMSCRIPTEN__
    if (hd && GAGCore::Sprite::highResolutionRequested()) requestMapTerrain(map);
#endif
    refreshSheets();
    advanceScan(map, 65536);
    auto usage = visibleUsage(map, left, top, right, bottom);
    for (const auto &page : extraPages)
        for (const auto entry : pageUsage(map, page.x & map.getMaskW(), page.y & map.getMaskH()))
            usage[entry >> 16][entry & 0xFFFF] = 1;
    if (hd && decor) collectVisibleDecor(map, left, top, right, bottom);
    prepareSelected(hd, time, &usage, nativeComposition, decor);
    visibleDecorSet = false;
}
bool Compositor::highResolutionPages(double density)
{
    if (pageDetail.update(GAGCore::HighResolutionPolicy::wantsHighResolution(density) ? 0 : 1, GAGCore::steadyClockNanoseconds(),
                          GAGCore::Sprite::highResolutionDetailDelay()) &&
        pageDetail.level())
        // Deferred banks retire their frames when the next window declares
        // none; whole sheets that a baseFrame demanded drop theirs here.
    {
        std::set<GAGCore::Sprite *> unique;
        for (unsigned id = 0; id < sprites.size(); ++id)
            if (auto *sprite = sheet(id)) unique.insert(sprite);
        for (auto *sprite : unique) sprite->releaseHighResolutionDemand();
    }
    return pageDetail.level() == 0;
}
void Compositor::declareVisible(int time, const SceneMap &map, int left, int top, int right, int bottom,
                                bool frames)
{
    refreshSheets();
    advanceScan(map, 65536);
    auto usage = visibleUsage(map, left, top, right, bottom);
    includeBoundarySources(usage);
    if (frames) collectVisibleDecor(map, left, top, right, bottom);
    declareWindows(frames, frames, time, &usage);
    visibleDecorSet = false;
    releasePreparedTextures();
}
const std::vector<Compositor::DecorLayer> &Compositor::pageDecor(const SceneMap &map, int px, int py) const
{
    constexpr int Page = 16;
    const auto key = std::uint64_t(unsigned(py)) << 32 | unsigned(px);
    if (auto found = decorPages.find(key); found != decorPages.end())
        return found->second;
    std::vector<DecorLayer> entries;
    const auto tiers = tierField(map);
    for (int y = 0; y < Page; ++y)
        for (int x = 0; x < Page; ++x)
            for (const auto &layer : decorLayers(map, px + x, py + y, tiers.get()))
                if (layer.sprite && layer.frame >= 0)
                    entries.push_back(layer);
    const auto order = [](const DecorLayer &a, const DecorLayer &b)
    { return std::tie(a.sprite, a.frame) < std::tie(b.sprite, b.frame); };
    std::sort(entries.begin(), entries.end(), order);
    entries.erase(std::unique(entries.begin(), entries.end(),
        [](const DecorLayer &a, const DecorLayer &b) { return a.sprite == b.sprite && a.frame == b.frame; }), entries.end());
    entries.shrink_to_fit();
    return decorPages.emplace(key, std::move(entries)).first->second;
}
void Compositor::collectVisibleDecor(const SceneMap &map, int left, int top, int right, int bottom)
{
    visibleDecor.clear();
    visibleDecorSet = true;
    if (!sharedDecorSprite && std::none_of(decorSprites.begin(), decorSprites.end(), [](auto *s) { return s; }))
        return;
    // Raised decor reaches past its cell: include the rows whose frames rise
    // into the view from below it, and those hanging into it from above.
    top -= (reach.down + 31) / 32;
    bottom += (reach.up + 31) / 32;
    left -= (reach.side + 31) / 32;
    right += (reach.side + 31) / 32;
    const int width = std::min(map.getW(), std::max(0, right - left + 1));
    const int height = std::min(map.getH(), std::max(0, bottom - top + 1));
    if (!width || !height)
        return;
    if (usageIdentity != map.identity() || usageRevision != map.terrainRevision() || usageSeed != map.terrainSeed())
    {
        usagePages.clear();
        decorPages.clear();
        usageIdentity = map.identity();
        usageRevision = map.terrainRevision();
        usageSeed = map.terrainSeed();
    }
    constexpr int Page = 16;
    const auto pageOf = [](int tile) { return int(std::floor(double(tile) / Page)); };
    const int px0 = pageOf(left), px1 = pageOf(left + width - 1);
    const int py0 = pageOf(top), py1 = pageOf(top + height - 1);
    const int pagesX = std::max(1, map.getW() / Page), pagesY = std::max(1, map.getH() / Page);
    for (int py = py0; py <= std::min(py1, py0 + pagesY - 1); ++py)
        for (int px = px0; px <= std::min(px1, px0 + pagesX - 1); ++px)
        {
            const auto &page = pageDecor(map, (px * Page) & map.getMaskW(), (py * Page) & map.getMaskH());
            visibleDecor.insert(visibleDecor.end(), page.begin(), page.end());
        }
}
void Compositor::releasePreparedTextures()
{
    if (!texturesPrepared) return;
    texturesPrepared = false;
    // Keep only source stamps, as eviction does.
    const auto release = [](auto &set) {
        for (auto &texture : set)
        {
            texture.pixels = MaterialPixels{};
            texture.size = 0;
        }
    };
    for (auto &set : textures) release(set);
    for (auto &animation : animationTextures)
    {
        release(animation.current);
        release(animation.next);
    }
    publishedCpu.reset();
    for (auto &published : publishedPixels) published.pixels.clear();
}
void Compositor::includeBoundarySources(Usage &usage) const
{
    for (unsigned id = 0; id < usage.size(); ++id)
    {
        const auto &material = definitions.materials[id];
        auto &variants = usage[id];
        if (std::none_of(variants.begin(), variants.end(), [](auto v) { return v != 0; }))
            continue;
        if (!material.periodicEdges)
            variants[0] = 1;
        else if (hasArtNeighbors(material))
        {
            const auto requested = variants;
            for (unsigned variant = 0; variant < requested.size(); ++variant)
                if (requested[variant])
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            variants[artNeighbor(material, variant, dx, dy)] = 1;
        }
    }
}
const std::vector<std::uint32_t> &Compositor::pageUsage(const SceneMap &map, int px, int py) const
{
    constexpr int Page = 16;
    const auto key = std::uint64_t(unsigned(py)) << 32 | unsigned(px);
    if (auto found = usagePages.find(key); found != usagePages.end())
        return found->second;
    std::vector<std::uint32_t> entries;
    const auto seed = map.terrainSeed();
    const auto use = [&](MaterialId id, int wx, int wy)
    {
        entries.push_back(std::uint32_t(id) << 16 | definitions.variantIndex(id, wx, wy, seed));
    };
    for (int y = 0; y < Page; ++y)
        for (int x = 0; x < Page; ++x)
        {
            const int wx = (px + x) & map.getMaskW(), wy = (py + y) & map.getMaskH();
            // Variant selection only needs corner materials and canonical
            // coordinates. Shoreline and halo data are needed at composition.
            for (const auto corner : map.cellCorners(wx, wy))
                use(materialFor(map, corner), wx, wy);
            // A stone border also draws stones reaching in from neighbouring
            // cells, other paths' stone layers and the ground a dropped stone
            // reveals, none of which need be a corner here. describeStones reads
            // them from the cell's 6x6 vertex window (-2..3), so near a stone
            // path prepare every material of that window at this cell's variant.
            std::array<MaterialId, 36> window;
            bool stone = false;
            for (int dy = -2; dy <= 3; ++dy)
                for (int dx = -2; dx <= 3; ++dx)
                {
                    const auto id = materialFor(map, map.vertexTerrainAt(wx + dx, wy + dy));
                    window[(dy + 2) * 6 + dx + 2] = id;
                    stone |= stoneMaps->has(id);
                }
            if (stone)
                for (const auto id : window)
                    use(id, wx, wy);
        }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    entries.shrink_to_fit();
    return usagePages.emplace(key, std::move(entries)).first->second;
}
Compositor::Usage Compositor::visibleUsage(const SceneMap &map, int left, int top, int right, int bottom) const
{
    Usage usage;
    for (const auto &m : definitions.materials) usage.emplace_back(m.variants.size(), 0);
    const int width = std::min(map.getW(), std::max(0, right - left + 1));
    const int height = std::min(map.getH(), std::max(0, bottom - top + 1));
    if (!width || !height)
        return usage;
    if (usageIdentity != map.identity() || usageRevision != map.terrainRevision() || usageSeed != map.terrainSeed())
    {
        usagePages.clear();
        decorPages.clear();
        usageIdentity = map.identity();
        usageRevision = map.terrainRevision();
        usageSeed = map.terrainSeed();
    }
    // Whole pages: a page partly in view prepares a few more variants than the
    // cells shown need, which composition of its page needs anyway.
    constexpr int Page = 16;
    const auto pageOf = [](int tile) { return int(std::floor(double(tile) / Page)); };
    const int px0 = pageOf(left), px1 = pageOf(left + width - 1);
    const int py0 = pageOf(top), py1 = pageOf(top + height - 1);
    const int pagesX = std::max(1, map.getW() / Page), pagesY = std::max(1, map.getH() / Page);
    for (int py = py0; py <= std::min(py1, py0 + pagesY - 1); ++py)
        for (int px = px0; px <= std::min(px1, px0 + pagesX - 1); ++px)
            for (const auto entry : pageUsage(map, (px * Page) & map.getMaskW(), (py * Page) & map.getMaskH()))
                usage[entry >> 16][entry & 0xFFFF] = 1;
    return usage;
}
void Compositor::declareWindows(bool materials, bool decor, int time, const Usage *requested)
{
    // A sprite can back several materials: declare the union before any reads.
    std::map<GAGCore::Sprite *, std::vector<unsigned>> windows;
    // Streamed animations keep later phases resident within a shared budget,
    // so advancing reuses decoded frames instead of dropping the old phase and
    // decoding the next one again. Endpoints are listed first and decode first.
    std::size_t lookaheadBytes = AnimationLookaheadBytes;
    refreshSheets();
    std::vector<unsigned> loading; // The window of a sheet still loading goes nowhere.
    for (unsigned id = 0; id < definitions.materials.size(); ++id)
    {
        const auto &m = definitions.materials[id];
        // Every loaded sheet gets a window, empty ones included: an empty
        // window retires the frames a previous view streamed.
        auto *materialSheet = sheet(id);
        loading.clear();
        auto &window = materialSheet ? windows[materialSheet] : loading;
        if (!materials && !decor) continue;
#ifdef __EMSCRIPTEN__
        if (materials && requested && GAGCore::Sprite::highResolutionRequested() && m.sprite.starts_with("data/gfx/terrain-") &&
            std::any_of((*requested)[id].begin(), (*requested)[id].end(), [](auto v) { return v != 0; }))
            requestTerrainPackage("hd-" + m.sprite.substr(9));
#endif
        const unsigned phase = unsigned(time) / m.animationTicks % m.animationFrames;
        const bool animated = m.animationFrames > 1 && m.animationTicks > 1;
        std::size_t count = 0;
        for (unsigned i = 0; i < m.variants.size(); ++i)
            if (!requested || (*requested)[id][i])
            {
                ++count;
                if (!materials) continue;
                window.push_back(m.variants[i].frame + phase * m.animationStride);
                if (animated)
                    window.push_back(m.variants[i].frame + (phase + 1) % m.animationFrames * m.animationStride);
            }
        // Decor (spires, masses, terraces) streams per material in view too:
        // the shared sheet is far larger than any one map needs. A view
        // declares only the frames its cells draw (collectVisibleDecor).
        if (decor && count && decorSprites[id] && !visibleDecorSet)
        {
            auto &decorWindow = windows[decorSprites[id]];
            for (const auto &frames : {m.decor.full, m.decor.edge, m.decor.connected, m.decor.connectedInterior,
                                       m.decor.connectedForeign})
                for (int frame : frames)
                    if (frame >= 0) decorWindow.push_back(unsigned(frame));
        }
        if (!materials || !animated || !count || !GAGCore::Sprite::highResolutionDeferred(m.sprite)) continue;
        constexpr std::size_t frameBytes = 128 * 128 * 4; // A 32-pixel cell at HD scale 4.
        const unsigned extra = unsigned(std::min<std::size_t>(m.animationFrames - 2,
                                                              lookaheadBytes / (count * frameBytes)));
        lookaheadBytes -= extra * count * frameBytes;
        for (unsigned ahead = 2; ahead < 2 + extra; ++ahead)
            for (unsigned i = 0; i < m.variants.size(); ++i)
                if (!requested || (*requested)[id][i])
                    window.push_back(m.variants[i].frame + (phase + ahead) % m.animationFrames * m.animationStride);
    }
    if (decor && visibleDecorSet)
    {
        // Every decor sheet declares a window, an empty one included, so
        // frames that left the view retire.
        for (auto *sprite : decorSprites)
            if (sprite) windows[sprite];
        for (const auto &layer : visibleDecor)
            windows[layer.sprite].push_back(unsigned(layer.frame));
    }
    for (auto &[sprite, window] : windows) sprite->setHighResolutionFrames(window);
}
void Compositor::prepareSelected(bool hd, int time, const Usage *requested, bool nativeComposition,
                                 bool decor)
{
    Usage normalized;
    if (requested)
    {
        normalized = *requested;
        includeBoundarySources(normalized);
        requested = &normalized;
    }
    declareWindows(hd, hd && decor, time, requested);
    // A renderer that never draws HD decor also drops windows declared before
    // it took over (a switch from legacy OpenGL), retiring their frames.
    if (!decor)
        for (auto *sprite : decorSprites)
            if (sprite && std::find(sprites.begin(), sprites.end(), sprite) == sprites.end())
                sprite->setHighResolutionFrames({});
    texturesPrepared = true;

    // Keep the existing stream window ready for the next zoom level while
    // reduced pages compose from the smaller native inputs.
    hd = hd && !nativeComposition;
	int nextResolution = 1;
	bool fallback = false;
	preparedTime = unsigned(time);
	for (unsigned id = 0; id < definitions.materials.size(); ++id)
	{
		const auto &m = definitions.materials[id];
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
			if (prepareMaterial(id, hd, phase, textures[id], wanted)) ++textureRevisions[id];
		}
		else
		{
			// Reuse the previous next phase as the current endpoint on ordinary advances.
			const unsigned first = wanted ? std::find(wanted->begin(), wanted->end(), 1) - wanted->begin() : 0;
            const int frame = m.variants[first].frame + phase * m.animationStride;
            auto *sprite = sheet(id);
			auto *source = !sprite ? nullptr : hd ? sprite->baseFrame(frame) : sprite->nativeFrame(frame);
			const bool advanced = animation.next[first].source == source;
			if (advanced) animation.current.swap(animation.next);
			const bool currentChanged = prepareMaterial(id, hd, phase, animation.current, wanted);
			const bool nextChanged = prepareMaterial(id, hd, (phase + 1) % m.animationFrames, animation.next, wanted);
			// Stepped crossfades hold each weight for several ticks; by default
			// every tick is its own step.
			const unsigned steps = m.animationBlendSteps ? unsigned(m.animationBlendSteps) : unsigned(m.animationTicks);
			const unsigned blend = unsigned(time) % m.animationTicks * steps / m.animationTicks;
            bool missingOutput = false;
            for (unsigned i = 0; i < m.variants.size(); ++i)
                if ((!wanted || (*wanted)[i]) && textures[id][i].pixels.empty()) missingOutput = true;
            const bool animationChanged = advanced || currentChanged || nextChanged || animation.blend != blend;
			if (animationChanged || missingOutput)
			{
                if (animationChanged) ++textureRevisions[id];
				animation.blend = blend;
				for (unsigned i = 0; i < m.variants.size(); ++i)
				{
					if (wanted && !(*wanted)[i]) continue;
					const auto &a = animation.current[i], &b = animation.next[i];
					auto &out = textures[id][i];
					out.size = std::max(a.size, b.size);
					auto &pixels = out.pixels.overwrite(std::size_t(out.size) * out.size);
					const unsigned weightB = (std::uint64_t(blend) * 65536 + steps / 2) / steps;
					const unsigned weightA = 65536 - weightB;
					out.opaque = a.opaque && b.opaque;
					if (out.opaque && a.size == out.size && b.size == out.size)
					{
						// Opaque endpoints at one size: a flat loop the compiler
						// vectorizes. Full alpha stays (255 * 65536 + 32768) >> 16.
						const auto *pa = a.pixels.front().data(), *pb = b.pixels.front().data();
						auto *po = pixels.front().data();
						const std::size_t bytes = out.pixels.size() * 4;
						for (std::size_t k = 0; k < bytes; ++k)
							po[k] = static_cast<unsigned char>((pa[k] * weightA + pb[k] * weightB + 32768) >> 16);
						continue;
					}
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
						auto &pixel = pixels[p];
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
		if (hd)
			for (unsigned i = 0; i < m.variants.size(); ++i)
				if (!wanted || (*wanted)[i])
				{
					// Either crossfade endpoint can still be a native source.
					int size = textures[id][i].size;
					if (!animation.current.empty())
						size = std::min({size, animation.current[i].size, animation.next[i].size});
					fallback |= highResolutionSizes[id] > size;
					highResolutionSizes[id] = std::max(highResolutionSizes[id], size);
				}
	}
	resolution = nextResolution;
	if (hd)
	{
		++coverage.prepares;
		coverage.fallbacks += fallback;
	}
}
MaterialId Compositor::materialFor(const SceneMap& map, TerrainType type) const {
    // Custom terrain with its own artwork binds by key; anything else draws its appearance.
    if (unsigned(type) >= TERRAIN_COUNT)
        if (const auto found = definitions.bindings.find(map.terrainRegistry().key(type)); found != definitions.bindings.end())
            return found->second;
    return terrainBindings[unsigned(map.terrainRegistry().appearance(type))];
}
// The kind of decor a material draws: 0 connected masses that stand on open
// ground, 1 connected foliage, 2 other obstacles, 3 none.
unsigned Compositor::decorSlot(MaterialId id) const
{
	const auto &decor = definitions.materials[id].decor;
	if (!decorSprites[id] || decor.full.empty())
		return 3;
	if (decor.connected.empty())
		return 2;
	return decor.connectedOpen ? 0 : 1;
}
bool Compositor::massWithin(const SceneMap &map, int x, int y, int reach) const
{
	// The cell's corners span (x,y)..(x+1,y+1); vertexTerrainAt wraps the torus.
	for (int dy = -reach; dy <= 1 + reach; ++dy)
		for (int dx = -reach; dx <= 1 + reach; ++dx)
			if (decorSlot(materialFor(map, map.vertexTerrainAt(x + dx, y + dy))) == 0)
				return true;
	return false;
}
bool Compositor::decorInFront(const SceneMap &map, int x, int y) const
{
	const auto corners = map.cellCorners(x & map.getMaskW(), y & map.getMaskH());
	// Corners 2 and 3 are the south ones; slot kind 3 is open ground.
	return decorSlot(materialFor(map, corners[2])) != 3 || decorSlot(materialFor(map, corners[3])) != 3;
}

std::array<Compositor::DecorLayer, Compositor::DecorSlots> Compositor::decorLayers(const SceneMap &map, int x, int y,
	const TierField *tiers) const
{
	x &= map.getMaskW();
	y &= map.getMaskH();
	std::array<DecorLayer, DecorSlots> layers{};
	const auto corners = map.cellCorners(x, y);
	std::array<MaterialId, 4> materials;
	std::array<unsigned, 4> kinds;
	bool openGround = false;
	for (unsigned k = 0; k < corners.size(); ++k)
	{
		materials[k] = materialFor(map, corners[k]);
		kinds[k] = decorSlot(materials[k]);
		openGround |= kinds[k] == 3;
	}
	if (std::all_of(kinds.begin(), kinds.end(), [](unsigned kind) { return kind == 3; }))
		return layers;
	const unsigned colours = cornerColours(x, y, map.getMaskW(), map.getMaskH(), map.terrainSeed());
	// Masses on open ground: each distinct one draws its own corners and stands
	// back from every other connected corner. Both tops rise alike, so where
	// two overlap the one whose footprint lies further south stands in front,
	// and a cell's layers draw north to south: by the mean row of their own
	// corners, then the lower id. That order depends only on each layer's own
	// corners, so neighbouring cells agree along the seam; an order that
	// differed between them would cut one mass's wall square at their shared
	// edge. A mass on two diagonal corners beside another mass has one piece
	// north of the other mass and one south of it, so it draws them as separate
	// layers: each piece's edges match the whole frame's, since along a cell
	// edge only that edge's two corners count.
	struct Piece
	{
		MaterialId id;
		unsigned own, foreign;
	};
	std::array<Piece, 4> pieces{};
	unsigned count = 0;
	for (unsigned k = 0; k < 4; ++k)
	{
		if (kinds[k] != 0 || std::any_of(pieces.begin(), pieces.begin() + count,
										 [&](const Piece &p) { return p.id == materials[k]; }))
			continue;
		const auto id = materials[k];
		unsigned own = 0, foreign = 0;
		for (unsigned c = 0; c < 4; ++c)
			if (materials[c] == id)
				own |= 1u << c;
			else if (kinds[c] < 2)
				foreign |= 1u << c;
		if (foreign && (own == 0b1001 || own == 0b0110) &&
			definitions.decorForeignFrame(id, own & 0b0011, foreign, colours) >= 0 &&
			definitions.decorForeignFrame(id, own & 0b1100, foreign, colours) >= 0)
		{
			pieces[count++] = {id, own & 0b0011, foreign};
			pieces[count++] = {id, own & 0b1100, foreign};
		}
		else
			pieces[count++] = {id, own, foreign};
	}
	// Mean row of a piece's own corners, compared without division.
	const auto south = [](unsigned own) { return unsigned(std::popcount(own >> 2)); };
	std::sort(pieces.begin(), pieces.begin() + count, [&](const Piece &a, const Piece &b) {
		const unsigned sa = south(a.own) * unsigned(std::popcount(b.own));
		const unsigned sb = south(b.own) * unsigned(std::popcount(a.own));
		return sa != sb ? sa < sb : a.id != b.id ? a.id < b.id : a.own < b.own;
	});
	// Every piece draws, piece m in slot m, or its corner would be an empty
	// notch cut square along the neighbouring cells, which draw it up to the
	// shared edge. Three or four pieces reach into the foliage and obstacle slots.
	for (unsigned m = 0; m < count; ++m)
	{
		const auto &[id, own, foreign] = pieces[m];
		int frame = foreign ? definitions.decorForeignFrame(id, own, foreign, colours) : -1;
		if (frame < 0)
			frame = definitions.decorFrame(id, x, y, own != 15, map.terrainSeed(), own, colours);
		if (frame >= 0)
			layers[m] = {decorSprites[id], frame};
	}
	// Foliage and other obstacles, as before: one material per slot by presentation hashing.
	// Choosing one of four corners gives 2+2 borders equal odds and 3+1
	// borders a three-to-one split. Presentation hashing never consumes game RNG.
	const unsigned pick = hash(x, y, mapSeedSalt(map.terrainSeed()) ^ 0xd3c04a71u) & 3u;
	for (unsigned kind = 1; kind <= 2 && !openGround; ++kind)
	{
		unsigned k = pick, tries = 0;
		while (kinds[k] != kind && tries++ < 4)
			k = (k + 1) & 3u;
		if (kinds[k] != kind)
			continue;
		// Obstacle clusters sit in the middle of the cell rather than on their
		// corners: where an open-ground mass holds most of the cell, one would
		// stand on top of it, so the mass's edge is drawn alone.
		if (kind == 2 && std::count(kinds.begin(), kinds.end(), 0u) > std::count(kinds.begin(), kinds.end(), 2u))
			continue;
		const auto chosen = materials[k];
		const auto &decor = definitions.materials[chosen].decor;
		// Spires and pillars keep clear of a mass's rim and terraces. The check
		// follows the hashed pick, so the slot stays empty even if another
		// obstacle corner of the cell would not need clearance.
		if (decor.clearOfMasses >= 0 && massWithin(map, x, y, decor.clearOfMasses))
			continue;
		unsigned mask = 0, count = 0;
		for (unsigned c = 0; c < 4; ++c)
		{
			count += materials[c] == chosen;
			// Connected foliage joins every foliage corner, so hedge and thicket
			// form one crown, but meets other kinds as an edge.
			if (materials[c] == chosen || (!decor.connected.empty() && kinds[c] == kind))
				mask |= 1u << c;
		}
		const int frame = definitions.decorFrame(chosen, x, y, count < 4, map.terrainSeed(), mask, colours);
		// Beside three masses foliage draws one slot later, after them all; no
		// obstacle draws there, since the masses hold most of the cell.
		unsigned slot = kind + 1;
		if (layers[slot].sprite)
			++slot;
		assert(slot < TierSlot && !layers[slot].sprite);
		if (frame >= 0)
			layers[slot] = {decorSprites[chosen], frame};
	}
	// Terraces: nested, so the first missing one ends the stack.
	if (tiers && count)
		for (int tier = 1; tier <= MaxDecorTiers; ++tier)
		{
			const int frame = decorTierFrame(map, *tiers, x, y, tier);
			if (frame < 0)
				break;
			layers[TierSlot + unsigned(tier - 1)] = {layers[0].sprite ? layers[0].sprite : sharedDecorSprite, frame};
		}
	return layers;
}
std::shared_ptr<const Compositor::TierField> Compositor::tierField(const SceneMap &map) const
{
	if (!tieredDecor)
		return nullptr;
	const int w = map.getW(), h = map.getH();
	std::lock_guard lock(tierMutex);
	if (tierCache && tierCache->revision == map.terrainRevision() && tierCache->registry == map.frozenTerrainRegistry().get() &&
		tierCache->w == w && tierCache->h == h && tierCache->seed == map.terrainSeed())
		return tierCache;
	auto field = std::make_shared<TierField>();
	field->revision = map.terrainRevision();
	field->registry = map.frozenTerrainRegistry().get();
	field->w = w;
	field->h = h;
	field->seed = map.terrainSeed();
	// Chebyshev distance, in vertices, to the nearest vertex of another
	// material, capped where the highest terrace needs no more: two raster
	// passes each way, run twice so distances carry around the torus.
	constexpr std::uint8_t cap = 2 * MaxDecorTiers + 2;
	std::vector<MaterialId> material(std::size_t(w) * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			material[std::size_t(y) * w + x] = materialFor(map, map.vertexTerrainAt(x, y));
	auto &depth = field->depth;
	depth.assign(material.size(), 0);
	const auto at = [&](int x, int y) { return std::size_t((y + h) % h) * w + std::size_t((x + w) % w); };
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const auto id = material[at(x, y)];
			if (!definitions.materials[id].decor.connectedTiers)
				continue;
			bool edge = false;
			for (int dy = -1; dy <= 1 && !edge; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					edge |= material[at(x + dx, y + dy)] != id;
			depth[at(x, y)] = edge ? 0 : cap;
		}
	const auto relax = [&](int x, int y, int dx, int dy) {
		auto &d = depth[at(x, y)];
		d = std::min<std::uint8_t>(d, std::uint8_t(depth[at(x + dx, y + dy)] + 1));
	};
	for (int round = 0; round < 2; ++round)
	{
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (depth[at(x, y)])
					relax(x, y, -1, 0), relax(x, y, -1, -1), relax(x, y, 0, -1), relax(x, y, 1, -1);
		for (int y = h - 1; y >= 0; --y)
			for (int x = w - 1; x >= 0; --x)
				if (depth[at(x, y)])
					relax(x, y, 1, 0), relax(x, y, 1, 1), relax(x, y, 0, 1), relax(x, y, -1, 1);
	}
	tierCache = field;
	return tierCache;
}
int Compositor::decorTierFrame(const SceneMap &map, const TierField &field, int x, int y, int tier) const
{
	x &= map.getMaskW();
	y &= map.getMaskH();
	if (tier < 1 || tier > MaxDecorTiers || field.w != map.getW() || field.h != map.getH())
		return -1;
	// A vertex joins terrace k when it lies 2k vertices deep, or 2k+1 by a
	// seeded per-vertex choice, so terraces wander like ledges rather than
	// tracing rings around the patch; each terrace sits inside the one below.
	const std::uint32_t salt = mapSeedSalt(map.terrainSeed()) ^ 0x7e55e5u;
	unsigned mask = 0;
	int deepest = -1;
	for (unsigned k = 0; k < 4; ++k)
	{
		const unsigned vx = unsigned(x + int(k & 1)) & unsigned(map.getMaskW());
		const unsigned vy = unsigned(y + int(k >> 1)) & unsigned(map.getMaskH());
		const int need = 2 * tier + int(hash(vx, vy, salt) & 1u);
		if (field.depth[std::size_t(vy) * field.w + vx] >= need)
			mask |= 1u << k, deepest = int(k);
	}
	if (!mask)
		return -1;
	// A deep corner's neighbours share its material, so every terraced corner
	// of the cell belongs to the same mass.
	const auto material = materialFor(map, map.cellCorners(x, y)[unsigned(deepest)]);
	return definitions.decorFrame(material, x, y, mask != 15, map.terrainSeed(), mask,
		cornerColours(x, y, map.getMaskW(), map.getMaskH(), map.terrainSeed()));
}
MaterialId Compositor::overlayMaterial(const SceneMap &map, int x, int y, bool &found) const
{
	const auto corners = map.cellCorners(x, y);
	std::array<MaterialId, 4> materials;
	found = false;
	for (unsigned k = 0; k < corners.size(); ++k)
	{
		materials[k] = materialFor(map, corners[k]);
		if (!overlaySprites[materials[k]])
			return 0;
	}
	found = true;
	// Only cells whose four corners all carry an overlay draw one, so it never
	// spills past its ground; a mixed cell picks a corner with presentation hashing.
	return materials[hash(x, y, mapSeedSalt(map.terrainSeed()) ^ 0x0e71a7u) & 3u];
}
GAGCore::Sprite *Compositor::overlaySprite(const SceneMap &map, int x, int y) const
{
	bool found;
	const auto material = overlayMaterial(map, x & map.getMaskW(), y & map.getMaskH(), found);
	return found ? overlaySprites[material] : nullptr;
}
int Compositor::overlayFrame(const SceneMap &map, int x, int y, int time, bool *animated) const
{
	x &= map.getMaskW();
	y &= map.getMaskH();
	bool found;
	const auto material = overlayMaterial(map, x, y, found);
	if (animated) *animated = false;
	return found ? definitions.overlayFrame(material, x, y, std::uint32_t(time), map.terrainSeed(), animated) : -1;
}
Recipe Compositor::describe(const SceneMap &map, int x, int y) const
{
	return describe(map, x, y, true);
}
Recipe Compositor::describeOverview(const SceneMap &map, int x, int y) const
{
	return describe(map, x, y, false);
}
std::array<unsigned char, 3> Compositor::overviewColor(const SceneMap &map, TerrainType type) const
{
	if (unsigned(type) >= TERRAIN_COUNT)
	{
		const auto color = map.terrainPresentation(type).overview;
		return {color.r, color.g, color.b};
	}
	return definitions.materials[materialFor(map, type)].preview;
}
Recipe Compositor::describe(const SceneMap &map, int x, int y, bool detailed) const
{
	auto r = describeCell(map, x, y, detailed);
	if (detailed)
	{
		describeStones(map, r);
		describeHalo(map, r);
	}
	return r;
}
Recipe Compositor::describeCell(const SceneMap &map, int x, int y, bool shoreCalm) const
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
	if (shoreCalm) describeShoreCalm(map, r);
	const auto first = r.corners[0];
	MaterialId other = first;
	for (const auto id : r.corners)
		if (id != first) other = id;
	if (other == first || !hasNeighborhoodProfiles)
		return r;
	// Version 4 smooth contacts read the halo of any mixed cell; earlier
	// catalogs only of two-material contextual cells, keeping their recipes.
	if (definitions.version < 4 && (!definitions.contextualFor(first, other) ||
		std::any_of(r.corners.begin(), r.corners.end(),
			[first, other](auto id) { return id != first && id != other; })))
		return r;
	r.hasNeighborhood = true;
	for (int y = -1; y <= 2; ++y)
		for (int x = -1; x <= 2; ++x)
			r.neighborhood[(y + 1) * 4 + x + 1] =
				materialFor(map, map.vertexTerrainAt(r.x + x, r.y + y));
	return r;
}
namespace
{
// Stone decisions of one cell depend only on its 4x4 vertex window, its
// position and the map seed, and up to nine cells ask for each, so a small
// per-thread cache makes each decision once. Keys carry the compositor's
// instance, so a different catalog never reuses them.
struct StoneDecisionKey
{
	std::uint64_t instance;
	int x, y, width, height;
	std::uint32_t seed;
	MaterialId stone;
	std::array<MaterialId, 16> window;
	bool operator==(const StoneDecisionKey &) const = default;
};
struct StoneDecisionHash
{
	std::size_t operator()(const StoneDecisionKey &k) const
	{
		std::uint64_t h = 1469598103934665603ull;
		const auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
		mix(k.instance); mix(unsigned(k.x)); mix(unsigned(k.y)); mix(unsigned(k.width));
		mix(unsigned(k.height)); mix(k.seed); mix(k.stone);
		for (const auto id : k.window) mix(id);
		return std::size_t(h);
	}
};
} // namespace
void Compositor::describeStones(const SceneMap &map, Recipe &r) const
{
	// Vertices -2..3 around the cell: each of the nine cells whose stones can
	// reach it reads its own 4x4 window from them.
	std::array<MaterialId, 36> vertices;
	// Stone paths whose stones can reach the cell, dominant first: where two meet,
	// both are layers, so whichever cell draws a stone decides it the same way.
	std::array<MaterialId, 4> paths{};
	unsigned pathCount = 0;
	for (int y = -2; y <= 3; ++y)
		for (int x = -2; x <= 3; ++x)
		{
			const auto id = materialFor(map, map.vertexTerrainAt(r.x + x, r.y + y));
			vertices[(y + 2) * 6 + x + 2] = id;
			// The whole window: a stone centred in a neighbouring cell reaches
			// this one from that cell's window, a vertex beyond this cell's own.
			if (stoneMaps->has(id) && pathCount < paths.size() &&
				std::find(paths.begin(), paths.begin() + pathCount, id) == paths.begin() + pathCount)
				paths[pathCount++] = id;
		}
	const auto vertex = [&](int x, int y) { return vertices[(y + 2) * 6 + x + 2]; };
	if (!pathCount)
		return;
	// Paths among the cell's own corners come first: they cover its pixels,
	// while the others only overhang from a neighbour. Then the dominant one.
	const auto corner = [&](MaterialId id) { return std::find(r.corners.begin(), r.corners.end(), id) != r.corners.end(); };
	std::sort(paths.begin(), paths.begin() + pathCount, [&](MaterialId a, MaterialId b)
	{
		if (corner(a) != corner(b)) return corner(a);
		return definitions.dominates(a, b) || (!definitions.dominates(b, a) && a < b);
	});
	const unsigned layers = std::min(pathCount, Recipe::MaxStoneLayers);
	bool varied = false;
	for (int y = -1; y <= 2 && !varied; ++y)
		for (int x = -1; x <= 2 && !varied; ++x)
			varied = vertex(x, y) != paths[0];
	if (!varied)
		return; // Inside the path.
	// A dropped stone reveals the most common land around the cell: never water,
	// lava, holes or chasms (Catalog::cutsStones), against which stones are kept
	// and cut by the smooth edge, and never another stone layer, so two paths do
	// not hand each other's dropped stones back and forth.
	// The nearest land: the cell's corners, its 4x4 window, else the 6x6 one that every
	// stone reaching it is decided from. Only land drops a stone, so without
	// land in the 6x6 window no stone is dropped and the ground is unused.
	std::array<MaterialId, 36> others;
	std::array<unsigned, 36> counts{};
	unsigned kinds = 0, best = 0;
	// Land among the cell's own corners first: it needs no palette slot.
	for (const auto id : r.corners)
		if (!stoneMaps->has(id) && !definitions.cutsStones(id))
		{
			unsigned k = 0;
			while (k < kinds && others[k] != id) ++k;
			if (k == kinds) others[kinds++] = id;
			if (++counts[k] > counts[best]) best = k;
		}
	for (const int reach : {1, 2})
	{
		if (kinds)
			break;
		for (int y = -reach; y <= reach + 1; ++y)
			for (int x = -reach; x <= reach + 1; ++x)
				if (const auto id = vertex(x, y); !stoneMaps->has(id) && !definitions.cutsStones(id))
				{
					unsigned k = 0;
					while (k < kinds && others[k] != id) ++k;
					if (k == kinds) others[kinds++] = id;
					if (++counts[k] > counts[best]) best = k;
				}
	}
	std::array<Recipe::StoneLayer, Recipe::MaxStoneLayers> layer{};
	for (unsigned l = 0; l < layers; ++l)
	{
		layer[l].material = paths[l];
		layer[l].ground = kinds ? others[best] : paths[l];
	}
	// The cell's palette holds four materials; a second layer that does not fit
	// is left out, and a first that does not fit keeps the smooth border.
	const auto fits = [&](unsigned count)
	{
		std::array<MaterialId, 4 + 2 * Recipe::MaxStoneLayers> wanted{r.corners[0], r.corners[1], r.corners[2], r.corners[3]};
		for (unsigned l = 0; l < count; ++l)
		{
			wanted[4 + 2 * l] = layer[l].material;
			wanted[5 + 2 * l] = layer[l].ground;
		}
		const unsigned size = 4 + 2 * count;
		unsigned distinct = 0;
		for (unsigned i = 0; i < size; ++i)
			distinct += std::find(wanted.begin(), wanted.begin() + i, wanted[i]) == wanted.begin() + i;
		return distinct <= 4;
	};
	unsigned count = layers;
	while (count && !fits(count))
		--count;
	if (!count)
		return;
	thread_local std::unordered_map<StoneDecisionKey, std::uint64_t, StoneDecisionHash> decided;
	if (decided.size() > 65536)
		decided.clear();
	std::array<std::array<std::uint64_t, 9>, Recipe::MaxStoneLayers> reachOf{};
	for (unsigned l = 0; l < count; ++l)
	{
		const auto stone = layer[l].material;
		const auto *stones = stoneMaps->get(stone);
		const unsigned wrap = unsigned(stones->tiles() - 1);
		const auto &reach = stones->reaching(int(unsigned(r.x) & wrap), int(unsigned(r.y) & wrap));
		for (unsigned slot = 0; slot < 9; ++slot)
			reachOf[l][slot] = reach[slot];
		auto &kept = layer[l].kept;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int slot = (dy + 1) * 3 + dx + 1;
				if (!reach[slot])
					continue; // No stone of that cell reaches this one.
				const MaterialId corners[4] = {vertex(dx, dy), vertex(dx + 1, dy), vertex(dx, dy + 1), vertex(dx + 1, dy + 1)};
				const auto own = std::count(std::begin(corners), std::end(corners), stone);
				if (own == 4)
				{
					kept[slot] = ~std::uint64_t(0);
					continue;
				}
				if (!own)
					continue;
				StoneDecisionKey key{instance, int(unsigned(r.x + dx) & unsigned(map.getMaskW())),
					int(unsigned(r.y + dy) & unsigned(map.getMaskH())), map.getW(), map.getH(), map.terrainSeed(), stone, {}};
				for (int y = -1; y <= 2; ++y)
					for (int x = -1; x <= 2; ++x)
						key.window[(y + 1) * 4 + x + 1] = vertex(dx + x, dy + y);
				if (const auto found = decided.find(key); found != decided.end())
				{
					kept[slot] = found->second;
					continue;
				}
				// Each stone follows the smooth border at its centre, which depends
				// only on the cell holding it: every cell it reaches agrees.
				const auto neighbour = describeCell(map, r.x + dx, r.y + dy, false);
				const PreparedCoverage prepared(definitions, neighbour, PreparedCoverage::Detail::Weights);
				const auto &centres = stones->centres(int(unsigned(r.x + dx) & wrap), int(unsigned(r.y + dy) & wrap));
				std::uint64_t bits = 0;
				for (std::size_t i = 0; i < centres.size(); ++i)
				{
					// Kept unless land covers more of its centre. Neither cutting
					// materials nor other stone paths drop a stone: water cuts it
					// along its edge, and where two paths meet the dominant one's
					// kept stones lie over the other's, leaving no gap to fill.
					const auto coverage = prepared.at(centres[i][0], centres[i][1]);
					unsigned mine = 0, other = 0;
					for (unsigned k = 0; k < 4; ++k)
						if (coverage.material[k] == stone)
							mine += coverage.weight[k];
						else if (!definitions.cutsStones(coverage.material[k]) && !stoneMaps->has(coverage.material[k]))
							other = std::max(other, coverage.weight[k]);
					if (mine >= other) bits |= std::uint64_t(1) << i;
				}
				decided.emplace(key, bits);
				kept[slot] = bits;
			}
		// Only reaching stones are ever looked up; the rest stay zero so equal
		// cells keep equal recipes.
		for (unsigned slot = 0; slot < 9; ++slot)
			kept[slot] &= reach[slot];
	}
	// A cell of one material whose reaching stones all agree with it draws
	// exactly as without stones, and keeps the uniform fast path.
	if (r.corners[1] == r.corners[0] && r.corners[2] == r.corners[0] && r.corners[3] == r.corners[0])
	{
		// Stones never cover a cutting material, so such a cell draws as before.
		if (definitions.cutsStones(r.corners[0]))
			return;
		bool agrees = true;
		for (unsigned l = 0; l < count && agrees; ++l)
		{
			const bool path = r.corners[0] == layer[l].material;
			for (unsigned slot = 0; slot < 9 && agrees; ++slot)
				agrees = (path ? (reachOf[l][slot] & ~layer[l].kept[slot]) : layer[l].kept[slot]) == 0;
		}
		if (agrees)
			return;
	}
	r.stones = true;
	r.stoneLayerCount = std::uint8_t(count);
	for (unsigned l = 0; l < count; ++l)
		r.stoneLayers[l] = layer[l];
	for (unsigned k = 0; k < 4; ++k)
		if (definitions.cutsStones(r.corners[k]))
			r.stoneCutCorners |= std::uint8_t(1u << k);
}
void Compositor::describeHalo(const SceneMap &map, Recipe &r) const
{
	if (definitions.version < 4 || !hasNeighborhoodProfiles)
		return;
	std::array<MaterialId, 4> palette;
	unsigned count = r.palette(palette);
	std::array<MaterialId, 16> window;
	for (int y = -1; y <= 2; ++y)
		for (int x = -1; x <= 2; ++x)
			window[(y + 1) * 4 + x + 1] = materialFor(map, map.vertexTerrainAt(r.x + x, r.y + y));
	// The window's nearest vertices first: those beside the tile's own edge.
	static constexpr std::array<unsigned, 16> order{5, 6, 9, 10, 1, 2, 4, 7, 8, 11, 13, 14, 0, 3, 12, 15};
	for (const auto i : order)
	{
		const auto id = window[i];
		// Every non-corner band material needs a field, even one a stone layer
		// already put in the palette: only corners and halo materials get one.
		if (std::find(r.corners.begin(), r.corners.end(), id) != r.corners.end() ||
			std::find(r.halo.begin(), r.halo.begin() + r.haloCount, id) != r.halo.begin() + r.haloCount)
			continue;
		if (std::none_of(r.corners.begin(), r.corners.end(),
				[&](MaterialId corner) { return definitions.bandsBetween(corner, id); }))
			continue;
		const bool listed = std::find(palette.begin(), palette.begin() + count, id) != palette.begin() + count;
		if (r.haloCount == Recipe::MaxHalo || (!listed && count == palette.size()))
			break; // The palette is full: these bands keep their previous reach.
		r.halo[r.haloCount++] = id;
		if (!listed) palette[count++] = id;
	}
	if (r.haloCount && !r.hasNeighborhood)
	{
		r.hasNeighborhood = true;
		r.neighborhood = window;
	}
}
void Compositor::describeShoreCalm(const SceneMap &map, Recipe &r) const
{
	if (std::none_of(r.corners.begin(), r.corners.end(),
					 [&](auto id) { return definitions.materials[id].shoreCalm; }))
		return;
	// Vertices around the cell, enough to reach land within the fade of every corner.
	constexpr int R = ShoreCalmSearch, W = 2 * R + 2;
	std::array<bool, W * W> land{};
	bool hasLand = false;
	for (int y = 0; y < W; ++y)
		for (int x = 0; x < W; ++x)
		{
			const bool dry = definitions.materials[materialFor(
				map, map.vertexTerrainAt(r.x + x - R, r.y + y - R))].surf.period == 0;
			land[y * W + x] = dry;
			hasLand |= dry;
		}
	// In open water every corner would search the same empty neighborhood.
	if (!hasLand)
		return;
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
void Compositor::boxFilter(const PreparedMaterialView &view, int size, MaterialPixels &out)
{
    // The same arithmetic as DirectTerrain's art tiles: premultiplied sums,
    // colour rounded over the summed alpha and alpha over the block.
    const int step = view.step;
    out.resize(std::size_t(size) * size);
    const unsigned count = unsigned(step * step);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            unsigned sum[4] = {};
            for (int dy = 0; dy < step; ++dy)
            {
                const auto *row = view.pixels.data() + std::size_t(y * step + dy) * view.size + x * step;
                for (int dx = 0; dx < step; ++dx)
                {
                    const auto &p = row[dx];
                    for (int k = 0; k < 3; ++k) sum[k] += unsigned(p[k]) * p[3];
                    sum[3] += p[3];
                }
            }
            auto &o = out[std::size_t(y) * size + x];
            for (int k = 0; k < 3; ++k) o[k] = (unsigned char)(sum[3] ? (sum[k] + sum[3] / 2) / sum[3] : 0);
            o[3] = (unsigned char)((sum[3] + count / 2) / count);
        }
}
Compositor::PreparedMaterialView Compositor::decimated(MaterialId id, const PreparedMaterialView &view,
                                                       int scale,
                                                       std::shared_ptr<const MaterialPixels> &hold) const
{
    const int size = 32 * scale;
    std::lock_guard lock(decimationMutex);
    auto &entry = decimations[{id, view.variant, scale}];
    if (!entry.pixels || entry.revision != view.revision || entry.source != view.pixels.data() ||
        entry.sourceSize != view.size)
    {
        if (!entry.pixels)
        {
            // Bound the copies; every entry is rebuilt on demand from its source.
            const auto bytes = std::size_t(size) * size * 4;
            if (decimationBytes + bytes > 16u * 1024 * 1024)
            {
                for (auto &[key, other] : decimations) other = Decimated{};
                decimationBytes = 0;
            }
            decimationBytes += bytes;
        }
        // Always a fresh copy: a composition in progress (another palette slot
        // of the same cell, or another thread) may still read the old one
        // through its hold, so neither eviction nor a rebuild frees it.
        auto pixels = std::make_shared<MaterialPixels>();
        boxFilter(view, size, *pixels);
        entry.pixels = std::move(pixels);
        entry.revision = view.revision;
        entry.source = view.pixels.data();
        entry.sourceSize = view.size;
    }
    hold = entry.pixels;
    return {view.variant, view.revision, size, *hold, 1};
}
Compositor::PreparedMaterialView Compositor::preparedMaterial(const Recipe &recipe, MaterialId id) const
{
    const unsigned variant = definitions.variantIndex(id, recipe.x, recipe.y, recipe.seed);
    const auto &texture = textures.at(id).at(variant);
    return {variant, textureRevisions.at(id), texture.size, texture.pixels};
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
    {
        std::array<MaterialId, 4> palette;
        for (unsigned i = 0, n = r.palette(palette); i < n; ++i)
            usage[palette[i]][definitions.variantIndex(palette[i], r.x, r.y, r.seed)] = 1;
    }
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
                        definitions.borderClasses.capacity() * sizeof(BorderClass) +
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
                 material.overlay.sprite.capacity() + 256;
    for (const auto &material : definitions.materials)
        for (const auto &cycle : material.overlay.cycles)
            bytes += sizeof(cycle) + cycle.capacity() * sizeof(int);
    for (const auto &binding : definitions.bindings)
        bytes += sizeof(binding) + binding.first.capacity() + 128;
    for (const auto &entry : definitions.borderClasses)
        bytes += entry.key.capacity() + 64;
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
    if (!usage && !publishedSubset && previous && previous->surfTime == preparedTime &&
        previous->included == included && publishedRevisions == textureRevisions)
        return previous;
    auto next = std::make_shared<CpuMaterials>();
    next->stones = stoneMaps;
    next->definitions = publishedCatalog.lock();
    if (!next->definitions)
    {
        // Copy only composition fields: even transient manifest-string copies
        // would exceed the slim snapshot's reserved allocation.
        auto copy = std::make_shared<Catalog>();
        copy->version = definitions.version;
        copy->boundaryWarp = definitions.boundaryWarp;
        copy->borderClasses = definitions.borderClasses;
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
        if (record.revision != textureRevisions[id])
        {
            record.pixels.assign(textures[id].size(), {});
            record.revision = textureRevisions[id];
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
            // The snapshot shares the prepared pixels; a later prepare copies
            // them before writing (SharedPixels::mutate).
            auto pixels = texture.pixels.share();
            record.pixels[variant] = pixels;
            next->textures[id].push_back({texture.size, std::move(pixels)});
        }
    }
    next->bytes = cpuMaterialBytesSelected(needed, usage);
    publishedSubset = usage != nullptr;
    next->surfTime = preparedTime;
    publishedRevisions = textureRevisions;
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
                                       CpuScratch &scratch, const CellMask *mask,
                                       const Refresh *refresh, int reduction) const
{
    scratch.sources.resize(definitions->materials.size());
    auto &sources = scratch.sources;
    std::array<MaterialId, 4> palette;
    for (unsigned i = 0, n = recipe.palette(palette); i < n; ++i)
    {
        const auto id = palette[i];
        const auto variant = definitions->variantIndex(id, recipe.x, recipe.y, recipe.seed);
        const auto &texture = textures.at(id).at(variant);
        if (!texture.pixels)
        {
            // A halo material (Recipe::halo) is listed for its bands and never
            // covers a sample, so its texture need not be prepared.
            sources[id] = {};
            continue;
        }
        sources[id] = {variant, 0, texture.size, *texture.pixels,
                       texture.size % (32 * scale) ? 0 : texture.size / (32 * scale)};
        if (sources[id].step > 1)
        {
            auto &filtered = scratch.filtered[i];
            boxFilter(sources[id], 32 * scale, filtered);
            sources[id] = {variant, 0, 32 * scale, filtered, 1};
        }
    }
    if (!mask && !recipe.uniform())
    {
        buildMask(*definitions, *stones, recipe, scale, scratch.mask);
        mask = &scratch.mask;
    }
    composePixels(*definitions, *stones, recipe, target, 0, 0, scale, sources, mask, surfTime,
                  mask ? refresh : nullptr, reduction);
}
Compositor::CellMask Compositor::CpuMaterials::mask(const Recipe &recipe, int scale) const
{
    CellMask result;
    buildMask(*definitions, *stones, recipe, scale, result);
    return result;
}
void Compositor::compose(const Recipe &r, SDL_Surface *target, int ox, int oy, int scale,
						 const CellMask *mask, const Refresh *refresh, int reduction) const
{
    thread_local std::vector<PreparedMaterialView> sources;
    sources.resize(definitions.materials.size());
    std::array<MaterialId, 4> palette;
    // Keeps decimated copies alive while this cell reads them.
    std::array<std::shared_ptr<const MaterialPixels>, 4> decimatedPixels;
    for (unsigned i = 0, n = r.palette(palette); i < n; ++i)
    {
        const auto id = palette[i];
        sources[id] = preparedMaterial(r, id);
        sources[id].step = sources[id].size % (32 * scale) ? 0 : sources[id].size / (32 * scale);
        if (sources[id].step > 1)
            sources[id] = decimated(id, sources[id], scale, decimatedPixels[i]);
    }
    composePixels(definitions, *stoneMaps, r, target, ox, oy, scale, sources, mask, preparedTime,
                  mask ? refresh : nullptr, reduction);
}
namespace
{
// A mixed-cell sample's weights per palette slot, from its mask encoding.
struct SampleWeights
{
	unsigned weight[4];
	unsigned largest;
	explicit SampleWeights(const Compositor::CellMask::Sample &sample)
	{
		largest = sample.slots & 3;
		unsigned rest = 65536;
		for (unsigned slot = 0, other = 0; slot < 4; ++slot)
			if (slot != largest)
			{
				weight[slot] = sample.others[other++];
				rest -= weight[slot];
			}
		weight[largest] = rest;
	}
};
// The seam shade and fringe tint of a sample (see composePixels).
std::pair<int, int> seamTone(const Material &dominant, const Material &neighbor, int margin)
{
	const auto &self = dominant.seam;
	const auto &other = neighbor.seam;
	int shade = 256, tint = 0;
	// Connected canopies supply their own raised edge and contact shadow.
	// Casting again from the rough undergrowth contour makes teeth and
	// hairlines protrude beyond the foliage.
	if (neighbor.decor.connected.empty() && other.cast && other.height > self.height &&
		margin < other.castWidth)
		shade = 256 - other.cast * (other.castWidth - margin) / other.castWidth;
	if (other.fringe && margin < other.fringeWidth)
		tint = other.fringe * (other.fringeWidth - margin) / other.fringeWidth;
	return {shade, tint};
}
bool surfReaches(const Material &dominant, const Material &neighbor, int margin)
{
	const bool surfSide = dominant.surf.period > 0;
	return surfSide != (neighbor.surf.period > 0) &&
		margin < surfReach(surfSide ? dominant.surf : neighbor.surf, surfSide);
}
// animatedSamples entries: the sample index, then flags.
constexpr std::uint32_t AnimatedIndexMask = 0xffffff, AnimatedSingle = 1u << 24;
constexpr int AnimatedSlotShift = 25;
} // namespace
unsigned Compositor::animatedSlots(const CellMask &cell) const
{
	unsigned slots = 0;
	for (unsigned slot = 0; slot < cell.colors; ++slot)
		slots |= unsigned(animated(cell.palette[slot])) << slot;
	return slots;
}
std::vector<std::uint32_t> Compositor::animatedSamples(const CellMask &cell) const
{
	std::vector<std::uint32_t> result;
	const unsigned animatedMask = animatedSlots(cell);
	for (std::size_t i = 0; i < cell.samples.size(); ++i)
	{
		const auto &sample = cell.samples[i];
		const SampleWeights weights(sample);
		const unsigned dominantSlot = sample.slots >> 2 & 3, neighborSlot = sample.slots >> 4 & 3;
		const auto &dominant = definitions.materials[cell.palette[dominantSlot]];
		const auto &neighbor = definitions.materials[cell.palette[neighborSlot]];
		const bool surf = surfReaches(dominant, neighbor, sample.margin);
		bool textured = false;
		for (unsigned slot = 0; slot < cell.colors; ++slot)
			textured |= weights.weight[slot] && (animatedMask >> slot & 1);
		if (!surf && !textured)
			continue;
		// One material covering the sample without seam toning or surf: its
		// (calmed) texel is the pixel whenever that texel is opaque.
		const bool single = !surf && weights.weight[weights.largest] == 65536 &&
			(dominantSlot == neighborSlot ||
			 seamTone(dominant, neighbor, sample.margin) == std::pair<int, int>{256, 0});
		result.push_back(std::uint32_t(i) | (single ? AnimatedSingle : 0) |
						 std::uint32_t(weights.largest) << AnimatedSlotShift);
	}
	return result;
}
void Compositor::composePixels(const Catalog &definitions, const StoneMaps &stones, const Recipe &r,
                               SDL_Surface *target,
                               int ox, int oy, int scale,
                               std::span<const PreparedMaterialView> prepared, const CellMask *mask,
                               unsigned surfTime, const Refresh *refresh, int reduction)
{
	if (target->format != SDL_PIXELFORMAT_ARGB8888)
		throw std::runtime_error("Terrain compositor requires ARGB8888");
	const int size = 32 * scale;
	if (reduction > 1 && (r.uniform() || refresh || size % reduction))
		throw std::logic_error("Only a mixed terrain cell composes reduced");
	// Resolve immutable pixel storage once per tile, rather than through
	// vector/array accessors for every channel of every instrumented pixel.
    const auto *sources = prepared.data();
	const bool uniform = r.uniform();
	// applyShoreCalm's `keep` for each sample of a calmed cell (shoreCalmKeep,
	// which the portable renderer's calm overlays also compose from).
	thread_local std::vector<unsigned> keepStorage;
	const auto prepareKeep = [&]
	{
		keepStorage.resize(std::size_t(size) * size);
		shoreCalmKeep(r.calm, size, keepStorage.data());
		return keepStorage.data();
	};
	const auto calmed = [](unsigned value, unsigned keep, unsigned mean)
	{ return (value * keep + mean * (256 - keep) + 128) / 256; };
	if (uniform)
	{
		const auto &texture = sources[r.corners[0]];
		const auto &material = definitions.materials[r.corners[0]];
		if (material.shoreCalm && r.calmed() && !texture.pixels.empty())
		{
			const unsigned *keep = prepareKeep();
			const auto &mean = material.calmMean;
			for (int y = 0; y < size; ++y)
			{
				auto *row = reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(target->pixels) +
					(oy + y) * target->pitch) + ox;
				for (int x = 0; x < size; ++x)
				{
					const int sx = texture.step ? x * texture.step : x * texture.size / size;
					const int sy = texture.step ? y * texture.step : y * texture.size / size;
					const auto *p = texture.pixels[sy * texture.size + sx].data();
					const unsigned k = keep[y * size + x];
					row[x] = (unsigned(p[3]) << 24) | (calmed(p[0], k, mean[0]) << 16) |
						(calmed(p[1], k, mean[1]) << 8) | calmed(p[2], k, mean[2]);
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
        buildMask(definitions, stones, r, scale, scratch);
	const auto &cell = mask ? *mask : scratch;
	if (cell.recipe != r || cell.scale != scale)
		throw std::runtime_error("Terrain cell mask does not match its recipe");
	// Per-cell constants, hoisted from the per-sample arithmetic below. Every
	// sample still evaluates the same integer expressions as applyShoreCalm,
	// shoreCalm and applySurf, so pixels are identical.
	struct Slot
	{
		const PreparedMaterialView *texture = nullptr; // Null: no pixels to blend.
		const Material *material = nullptr;
		bool calm = false;
		int surfPhase = 0;
	};
	std::array<Slot, 4> slots{};
	bool anyCalm = false;
	for (unsigned slot = 0; slot < cell.colors; ++slot)
	{
		const auto id = cell.palette[slot];
		auto &info = slots[slot];
		info.material = &definitions.materials[id];
		info.texture = sources[id].pixels.empty() ? nullptr : &sources[id];
		info.calm = info.material->shoreCalm && r.calmed();
		anyCalm |= info.calm;
		if (info.material->surf.period > 0)
			info.surfPhase = surfPhase(info.material->surf, surfTime);
	}
	const unsigned *const keep = anyCalm ? prepareKeep() : nullptr;
	const auto texel = [&](unsigned slot, int x, int y)
	{
		const auto &t = *slots[slot].texture;
		const int sx = t.step ? x * t.step : x * t.size / size;
		const int sy = t.step ? y * t.step : y * t.size / size;
		return t.pixels[sy * t.size + sx].data();
	};
	const auto source = [&](unsigned slot, const unsigned char *p, int x, int y, unsigned out[3])
	{
		if (slots[slot].calm)
		{
			const auto &mean = slots[slot].material->calmMean;
			const unsigned k = keep[y * size + x];
			for (int c = 0; c < 3; ++c)
				out[c] = calmed(p[c], k, mean[c]);
		}
		else
			for (int c = 0; c < 3; ++c)
				out[c] = p[c];
	};
	const auto pixel = [&](int x, int y) -> Uint32 &
	{
		return reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(target->pixels) +
										  (oy + y) * target->pitch)[ox + x];
	};
	constexpr unsigned opaqueAlpha = 255u * 65536;
	// A sample's blend of its palette textures: each texel premultiplied by
	// its Q16 weight times its alpha, summed into rgb and alpha.
	const auto blendSample = [&](int x, int y, const SampleWeights &weights, std::uint64_t (&rgb)[3], auto &alpha)
	{
		for (unsigned slot = 0; slot < cell.colors; ++slot)
			if (weights.weight[slot] && slots[slot].texture)
			{
				const auto *p = texel(slot, x, y);
				const unsigned a = weights.weight[slot] * p[3];
				alpha += a;
				unsigned channels[3];
				source(slot, p, x, y, channels);
				for (int k = 0; k < 3; ++k)
					rgb[k] += std::uint64_t(channels[k]) * a;
			}
	};
	// The seam and surf of the sample at (x, y) over a blend, written with
	// the blend's alpha to output pixel (outX, outY).
	const auto finishSample = [&](int x, int y, const CellMask::Sample &sample, const std::uint64_t (&rgb)[3],
								  auto alpha, unsigned outAlpha, int outX, int outY)
	{
		const unsigned dominantSlot = sample.slots >> 2 & 3, neighborSlot = sample.slots >> 4 & 3;
		const auto &dominantMaterial = *slots[dominantSlot].material;
		const auto &neighborMaterial = *slots[neighborSlot].material;
		const int margin = sample.margin;
		// Seam treatment: the dominant material darkens under a higher
		// neighbor's edge and takes that neighbor's fringe tint. Textures are
		// never blended across the seam; only the contact band is toned.
		int shade = 256, tint = 0;
		if (dominantSlot != neighborSlot)
			std::tie(shade, tint) = seamTone(dominantMaterial, neighborMaterial, margin);
		const auto &fringe = neighborMaterial.seam.fringeColor;
		unsigned value[3];
		for (int k = 0; k < 3; ++k)
		{
			// Opaque materials have a constant denominator. Keep the exact
			// integer result while avoiding three variable-width divisions
			// per sample when an animated terrain page is re-blended.
			const unsigned blended = alpha == opaqueAlpha ? unsigned(rgb[k] / opaqueAlpha)
				: unsigned(alpha ? rgb[k] / alpha : 0);
			const unsigned shaded = blended * shade >> 8;
			value[k] = shaded + (unsigned(fringe[k]) - shaded) * tint / 256;
		}
		if (surfReaches(dominantMaterial, neighborMaterial, margin))
		{
			// Q8 world pixels of the sample centre, as the coverage samples it.
			const bool surfSide = dominantMaterial.surf.period > 0;
			const auto &liquid = surfSide ? dominantMaterial : neighborMaterial;
			const auto land = (surfSide ? neighborMaterial : dominantMaterial).shoreWash;
			applySurfAt(liquid.surf, surfSide, margin, r.x * 8192 + (2 * x + 1) * 4096 / size,
						r.y * 8192 + (2 * y + 1) * 4096 / size,
						slots[surfSide ? dominantSlot : neighborSlot].surfPhase, value, land);
		}
		pixel(outX, outY) = (outAlpha << 24) | (value[0] << 16) | (value[1] << 8) | value[2];
	};
	// The complete sample: blend, seam, then surf.
	const auto composeSample = [&](int x, int y, const CellMask::Sample &sample, const SampleWeights &weights)
	{
		std::uint64_t rgb[3] = {};
		unsigned alpha = 0;
		blendSample(x, y, weights, rgb, alpha);
		finishSample(x, y, sample, rgb, alpha, (alpha + 32768) / 65536, x, y);
	};
	if (reduction > 1)
	{
		// A reduced cell, as the direct pass draws it at that density: each
		// output pixel is the alpha-weighted mean blend of its block, toned
		// by the seam and surf of the block's centre sample, so contact lines
		// and surf bands keep their width rather than being averaged with
		// their surroundings into a wider, paler band.
		const int out = size / reduction;
		const std::uint64_t count = std::uint64_t(reduction) * reduction;
		for (int by = 0; by < out; ++by)
			for (int bx = 0; bx < out; ++bx)
			{
				std::uint64_t rgb[3] = {}, alpha = 0;
				for (int dy = 0; dy < reduction; ++dy)
					for (int dx = 0; dx < reduction; ++dx)
					{
						const int x = bx * reduction + dx, y = by * reduction + dy;
						blendSample(x, y, SampleWeights(cell.samples[std::size_t(y) * size + x]), rgb, alpha);
					}
				const int cx = bx * reduction + reduction / 2, cy = by * reduction + reduction / 2;
				finishSample(cx, cy, cell.samples[std::size_t(cy) * size + cx], rgb, alpha,
							 unsigned((alpha / count + 32768) / 65536), bx, by);
			}
		return;
	}
	if (refresh && refresh->samples)
	{
		// Only the samples animation can change, prepared from this same mask.
		for (const auto entry : *refresh->samples)
		{
			const unsigned index = entry & AnimatedIndexMask;
			const int x = int(index % unsigned(size)), y = int(index / unsigned(size));
			const unsigned slot = entry >> AnimatedSlotShift & 3;
			if ((entry & AnimatedSingle) && slots[slot].texture)
			{
				const auto *p = texel(slot, x, y);
				if (p[3] == 255)
				{
					// The weighted mean of one full opaque weight is its texel.
					unsigned value[3];
					source(slot, p, x, y, value);
					pixel(x, y) = 0xff000000u | (value[0] << 16) | (value[1] << 8) | value[2];
					continue;
				}
			}
			const auto &sample = cell.samples[index];
			composeSample(x, y, sample, SampleWeights(sample));
		}
		return;
	}
	const auto *sample = cell.samples.data();
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x, ++sample)
		{
			const SampleWeights weights(*sample);
			if (refresh)
			{
				// Surf and textures that did not change leave the pixel as it is.
				const unsigned dominantSlot = sample->slots >> 2 & 3, neighborSlot = sample->slots >> 4 & 3;
				const auto &dominant = *slots[dominantSlot].material;
				const auto &neighbor = *slots[neighborSlot].material;
				const unsigned surfSlot = dominant.surf.period > 0 ? dominantSlot : neighborSlot;
				bool changed = (refresh->surf >> surfSlot & 1) && surfReaches(dominant, neighbor, sample->margin);
				for (unsigned slot = 0; slot < cell.colors && !changed; ++slot)
					changed = weights.weight[slot] && (refresh->textures >> slot & 1);
				if (!changed)
					continue;
			}
			composeSample(x, y, *sample, weights);
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
    buildMask(definitions, *stoneMaps, r, scale, cell);
}
void Compositor::buildMask(const Catalog &definitions, const StoneMaps &stones, const Recipe &r, int scale,
                           CellMask &cell)
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
	cell.colors = r.palette(cell.palette);
	const int size = 32 * scale;
	cell.samples.resize(std::size_t(size) * size);
	auto *sample = cell.samples.data();
	const PreparedCoverage prepared(definitions, r);
	std::array<const StoneMap *, Recipe::MaxStoneLayers> stoneMap{};
	for (unsigned l = 0; l < r.stoneLayerCount; ++l)
		stoneMap[l] = stones.get(r.stoneLayers[l].material);
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x, ++sample)
		{
			const int px = (x * 256 + 128) / scale, py = (y * 256 + 128) / scale;
			auto coverage = prepared.at(px, py);
			// The other path first, so the dominant one's kept stones lie on top.
			for (unsigned l = r.stoneLayerCount; l-- > 0;)
				applyStones(r, l, *stoneMap[l], px, py, coverage);
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
								 int samples, const CornerColors *cornerColors) const
{
	composeOverview(definitions, r, target, ox, oy, samples, cornerColors);
}

void Compositor::composeOverview(const Catalog &definitions, const Recipe &r, SDL_Surface *target,
								 int ox, int oy, int samples, const CornerColors *cornerColors)
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
		// A plain fill, so presentation workers can compose disjoint cells of one page.
		const Uint32 color = packed(colorOf(r.corners[0]));
		for (int y = 0; y < samples; ++y)
			std::fill_n(reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) + (oy + y) * target->pitch) + ox,
						samples, color);
		return;
	}
	const PreparedCoverage prepared(definitions, r, PreparedCoverage::Detail::Weights);
	for (int y = 0; y < samples; ++y)
		for (int x = 0; x < samples; ++x)
		{
			const auto mask = prepared.at((x * 8192 + 4096) / samples, (y * 8192 + 4096) / samples);
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
