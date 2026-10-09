// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushSwatches.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"
#include "map/TerrainRegistry.h"
#include "render/ResourceSprites.h"
#include "render/terrain/TerrainCompositor.h"
#include "resource/ResourceRegistry.h"
#include <algorithm>

using GAGCore::DrawableSurface;

BrushSwatches::BrushSwatches() = default;
BrushSwatches::~BrushSwatches() = default;

void BrushSwatches::bind(std::shared_ptr<const TerrainRegistry> terrain,
						 std::shared_ptr<const ResourceRegistry> resources,
						 std::shared_ptr<const MapAssetBundle> bundle)
{
	std::string next = (terrain ? terrain->digest() : std::string()) + "/" +
					   (resources ? resources->digest() : std::string());
	terrainRegistry = std::move(terrain);
	resourceRegistry = std::move(resources);
	if (next != digest || assets != bundle)
	{
		digest = std::move(next);
		assets = std::move(bundle);
		clear();
	}
}

void BrushSwatches::clear()
{
	cache.clear();
}

bool BrushSwatches::usable()
{
	if (!globalContainer || !globalContainer->gfx || !terrainRegistry)
		return false;
	// Composed pixels outlive a device reset, but drop them anyway so a lost
	// context never leaves stale textures bound to cached swatches.
	const auto generation = GAGCore::GraphicContext::renderResetGeneration();
	if (generation != resetGeneration)
	{
		resetGeneration = generation;
		clear();
	}
	return true;
}

GAGCore::DrawableSurface *BrushSwatches::get(const BrushEntry &entry, int px)
{
	switch (entry.swatch.kind)
	{
	case BrushSwatch::Kind::Terrain:
		return terrain(entry.swatch.terrain, px);
	case BrushSwatch::Kind::Resource:
		return resource(entry.swatch.resource, entry.swatch.terrain, px);
	default:
		return nullptr;
	}
}

GAGCore::DrawableSurface *BrushSwatches::terrain(TerrainType type, int px)
{
	if (px <= 0 || !usable() || !terrainRegistry->valid(type))
		return nullptr;
	const auto key = std::make_pair("terrain/" + std::to_string(unsigned(type)), px);
	if (auto found = cache.find(key); found != cache.end())
		return found->second.get();
	auto surface = composeTerrain(type, px);
	return cache.emplace(key, std::move(surface)).first->second.get();
}

GAGCore::DrawableSurface *BrushSwatches::resource(ResourceId id, TerrainType backdrop, int px)
{
	if (px <= 0 || !usable() || !resourceRegistry || !resourceRegistry->valid(id) ||
		!terrainRegistry->valid(backdrop))
		return nullptr;
	const auto key = std::make_pair("resource/" + std::to_string(resourceIndex(id)) + "/" +
										std::to_string(unsigned(backdrop)),
									px);
	if (auto found = cache.find(key); found != cache.end())
		return found->second.get();
	auto surface = composeResource(id, backdrop, px);
	return cache.emplace(key, std::move(surface)).first->second.get();
}

namespace
{
// Every pixel opaque: presentations draw swatches over arbitrary backgrounds.
void makeOpaque(DrawableSurface &surface)
{
	auto *sdl = surface.getSDLSurface();
	if (!sdl || sdl->format != SDL_PIXELFORMAT_ARGB8888)
		return;
	for (int y = 0; y < sdl->h; ++y)
	{
		auto *row =
			reinterpret_cast<Uint32 *>(static_cast<unsigned char *>(sdl->pixels) + y * sdl->pitch);
		for (int x = 0; x < sdl->w; ++x)
			row[x] |= 0xff000000u;
	}
	surface.markPixelsChanged();
}

// Scale a sprite frame's own pixels into a rectangle. The plain drawSurface
// overloads sample from the frame's atlas offset, which only addresses the
// shared GPU sheet: on a frame's own CPU pixels it points past the image and
// atlased sprites (resources, decor) would draw nothing.
void drawFrame(DrawableSurface &target, int x, int y, int w, int h, DrawableSurface *frame)
{
	const auto *pixels = frame->getSDLSurface();
	target.drawSurface(x, y, w, h, frame, 0, 0, pixels->w, pixels->h);
}

} // namespace

std::unique_ptr<DrawableSurface> BrushSwatches::composeTerrain(TerrainType type, int px)
{
	auto &compositor = globalContainer->terrainCompositor(assets);
	// Inspecting another set can replace the global custom compositor. Prepare
	// the current instance for each new swatch, rather than retaining a flag
	// tied to the previous instance. Cached swatches need no texture work.
	const bool gpu = globalContainer->gfx->getOptionFlags() &
					 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	compositor.prepare(gpu, 0);
	const auto &catalog = compositor.catalog();
	// A custom material binding takes precedence over the imported base look.
	const auto appearance = terrainRegistry->appearance(type);
	const bool builtinLook = unsigned(appearance) < TERRAIN_COUNT;
	auto binding = catalog.bindings.find(terrainRegistry->key(type));
	if (binding == catalog.bindings.end() && builtinLook)
		binding = catalog.bindings.find(terrainPresentation(appearance).name);
	// Two by two map cells, as the map shows them at 100% zoom, at an integer
	// scale at least as large as the swatch: neighbouring cells pick their own
	// texture variants.
	const int scale = std::clamp((px + 63) / 64, 1, 8);
	const int cell = 32 * scale, size = 2 * cell;
	DrawableSurface base(size, size);
	// An opaque floor in case artwork is missing: the material's preview colour,
	// or the type's own saved colour.
	const auto &colours = terrainRegistry->presentation(type);
	GAGCore::Color fill(colours.preview.r, colours.preview.g, colours.preview.b);
	if (binding != catalog.bindings.end())
	{
		const auto &material = catalog.materials[binding->second];
		fill = GAGCore::Color(material.preview[0], material.preview[1], material.preview[2]);
	}
	base.drawFilledRect(0, 0, size, size, fill);
	if (binding != catalog.bindings.end())
	{
		DrawableSurface texture(size, size);
		TerrainVisual::Recipe recipe;
		recipe.corners.fill(binding->second);
		recipe.width = recipe.height = 2;
		for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 2; ++x)
			{
				recipe.x = x;
				recipe.y = y;
				compositor.compose(recipe, texture.getSDLSurface(), x * cell, y * cell, scale);
			}
		texture.markPixelsChanged();
		base.drawSurface(0, 0, &texture);
		// Raised decor (boulders, hedges, rock) over each cell, as the map draws
		// it for cells surrounded by the same material.
		if (auto *sprite = compositor.decorSprite(binding->second))
			for (int y = 0; y < 2; ++y)
				for (int x = 0; x < 2; ++x)
				{
					const int frame = catalog.decorFrame(binding->second, x, y, false);
					if (frame < 0)
						continue;
					auto *source = sprite->nativeFrame(frame);
					if (source && source->getSDLSurface())
					{
						const int w = source->getW() * scale, h = source->getH() * scale;
						drawFrame(base, x * cell + cell / 2 - w / 2, y * cell + cell / 2 - h / 2, w,
								  h, source);
					}
				}
	}
	makeOpaque(base);
	auto result = std::make_unique<DrawableSurface>(px, px);
	result->drawSurface(0, 0, px, px, &base);
	makeOpaque(*result);
	return result;
}

std::unique_ptr<DrawableSurface> BrushSwatches::composeResource(ResourceId id, TerrainType backdrop,
																int px)
{
	auto result = std::make_unique<DrawableSurface>(px, px);
	if (auto *ground = terrain(backdrop, px))
		result->drawSurface(0, 0, ground);
	const auto &presentation = resourceRegistry->presentation(id);
	const auto &sprites = ResourceSprites::resolve(resourceRegistry, assets);
	auto *sprite =
		resourceIndex(id) < sprites.sprites.size() ? sprites.sprites[resourceIndex(id)] : nullptr;
	if (sprite && !presentation.levels.empty())
	{
		// The fullest stage, the look an author expects of a freshly painted deposit.
		const unsigned frame = presentation.frame(presentation.levels.back().stock, 0, 0, 0);
		auto *source = sprite->baseFrame(frame);
		if (!source || !source->getSDLSurface())
			source = sprite->nativeFrame(frame);
		if (source && source->getSDLSurface())
			drawFrame(*result, 0, 0, px, px, source);
	}
	makeOpaque(*result);
	return result;
}

GAGCore::DrawableSurface *BrushSwatches::terrainScene(TerrainType type, TerrainType neighbor,
													  unsigned phase, unsigned variation, int px)
{
	if (!usable() || px <= 0 || px > 768 || !terrainRegistry->valid(type) ||
		!terrainRegistry->valid(neighbor))
		return nullptr;
	const auto key = std::make_pair("scene/" + std::to_string(unsigned(type)) + "/" +
										std::to_string(unsigned(neighbor)) + "/" +
										std::to_string(phase) + "/" + std::to_string(variation),
									px);
	if (auto it = cache.find(key); it != cache.end())
		return it->second.get();
	auto &compositor = globalContainer->terrainCompositor(assets);
	const auto &catalog = compositor.catalog();
	auto materialFor = [&](TerrainType id)
	{
		auto found = catalog.bindings.find(terrainRegistry->key(id));
		if (found == catalog.bindings.end())
			found =
				catalog.bindings.find(terrainPresentation(terrainRegistry->appearance(id)).name);
		return found == catalog.bindings.end() ? TerrainVisual::MaterialId(0) : found->second;
	};
	const auto material = materialFor(type), other = materialFor(neighbor);
	compositor.prepare(false, int(phase * catalog.materials[material].animationTicks));
	auto source = std::make_unique<DrawableSurface>(192, 192);
	auto cellMaterial = [&](int x, int y)
	{
		x = (x + 6) % 6;
		y = (y + 6) % 6;
		// An isolated cell, a one-cell path, bends and a concave pond edge.
		return ((x == 1 && y == 1) || (x == 3 && y >= 1 && y <= 4) ||
				(y == 4 && x >= 1 && x <= 4) || (x >= 4 && y <= 1))
				   ? material
				   : other;
	};
	for (int y = 0; y < 6; ++y)
		for (int x = 0; x < 6; ++x)
		{
			TerrainVisual::Recipe recipe;
			recipe.x = x;
			recipe.y = y;
			recipe.width = recipe.height = 6;
			recipe.seed = variation;
			recipe.corners = {cellMaterial(x, y), cellMaterial(x + 1, y), cellMaterial(x, y + 1),
							  cellMaterial(x + 1, y + 1)};
			recipe.hasNeighborhood = true;
			for (int dy = -1; dy <= 2; ++dy)
				for (int dx = -1; dx <= 2; ++dx)
					recipe.neighborhood[(dy + 1) * 4 + dx + 1] = cellMaterial(x + dx, y + dy);
			compositor.compose(recipe, source->getSDLSurface(), x * 32, y * 32, 1);
		}
	source->markPixelsChanged();
	// Decor uses the same per-material sprite and coordinate selection as map rows.
	for (int y = 0; y < 6; ++y)
		for (int x = 0; x < 6; ++x)
		{
			const std::array<TerrainVisual::MaterialId, 4> corners = {
				cellMaterial(x, y), cellMaterial(x + 1, y), cellMaterial(x, y + 1),
				cellMaterial(x + 1, y + 1)};
			TerrainVisual::MaterialId decorated = 0;
			unsigned count = 0;
			for (auto id : corners)
			{
				if (catalog.materials[id].decor.full.empty())
					continue;
				const auto same = unsigned(std::count(corners.begin(), corners.end(), id));
				if (same > count)
				{
					decorated = id;
					count = same;
				}
			}
			if (count < 2)
				continue;
			const bool edge = count < 4;
			if (auto *sprite = compositor.decorSprite(decorated))
			{
				const int frame = catalog.decorFrame(decorated, x, y, edge, variation);
				if (frame >= 0)
					if (auto *image = sprite->nativeFrame(frame))
						drawFrame(*source, x * 32 + 16 - image->getW() / 2,
								  y * 32 + 16 - image->getH() / 2, image->getW(), image->getH(),
								  image);
			}
		}
	makeOpaque(*source);
	auto result = std::make_unique<DrawableSurface>(px, px);
	result->drawSurface(0, 0, px, px, source.get());
	makeOpaque(*result);
	return cache.emplace(key, std::move(result)).first->second.get();
}
GAGCore::DrawableSurface *BrushSwatches::resourceStage(ResourceId id, TerrainType backdrop,
													   unsigned stock, unsigned phase,
													   unsigned variation, int px)
{
	if (!usable() || px <= 0 || px > 512 || !resourceRegistry || !resourceRegistry->valid(id) ||
		!terrainRegistry->valid(backdrop))
		return nullptr;
	const auto key =
		std::make_pair("stage/" + std::to_string(resourceIndex(id)) + "/" +
						   std::to_string(unsigned(backdrop)) + "/" + std::to_string(stock) + "/" +
						   std::to_string(phase) + "/" + std::to_string(variation),
					   px);
	if (auto it = cache.find(key); it != cache.end())
		return it->second.get();
	auto result = std::make_unique<DrawableSurface>(px, px);
	if (auto *ground = terrain(backdrop, px))
		result->drawSurface(0, 0, ground);
	const auto &presentation = resourceRegistry->presentation(id);
	const auto &sprites = ResourceSprites::resolve(resourceRegistry, assets);
	auto *sprite =
		resourceIndex(id) < sprites.sprites.size() ? sprites.sprites[resourceIndex(id)] : nullptr;
	if (sprite && !presentation.levels.empty())
	{
		const ResourceSpriteLevel *level = &presentation.levels.front();
		for (const auto &candidate : presentation.levels)
			if (candidate.stock <= stock)
				level = &candidate;
		if (!level->variants.empty())
		{
			const unsigned frame =
				level->variants[variation % level->variants.size()].frame +
				(phase % presentation.animationFrames) * presentation.animationStride;
			auto *image = sprite->nativeFrame(frame);
			if (image && image->getSDLSurface())
				drawFrame(*result, 0, 0, px, px, image);
		}
	}
	makeOpaque(*result);
	return cache.emplace(key, std::move(result)).first->second.get();
}
