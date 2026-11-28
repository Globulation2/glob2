// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShadowMask.h"

#include "Building.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "ResourceSprites.h"
#include "Sunlight.h"
#include "Team.h"
#include "building/hud/BuildingPresentation.h"
#include "scene/Scene.h"

#include <RenderResources.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using GAGCore::DrawableSurface;
using GAGCore::Sprite;

namespace
{
// A composed chunk: black, its alpha the shadow coverage. Masks composed at
// two pixels per map pixel sample linearly, like HD frames.
class MaskSurface : public DrawableSurface
{
public:
	MaskSurface(SDL_Surface *pixels, bool linear) : DrawableSurface(pixels, AdoptPixels{}, true)
	{
		highResolutionSampling = linear;
	}
};

// The masks kept, those of the view and some room to pan: the render budget's
// shadow limit (GAGCore::RenderBudget::Terrain::shadows: 64 MiB on desktop,
// 16 MiB on phones and in the browser), so it scales with the device's budget.
std::size_t byteBudget() { return GAGCore::platformRenderBudget().terrain.shadows; }
// Far out, shadows are a few pixels: draw none rather than compose a map's worth.
constexpr std::size_t MaximumVisibleChunks = 160;

int wrap(int value, int size)
{
	value %= size;
	return value < 0 ? value + size : value;
}

// The shortest signed distance from `from` to `to` on a torus of `size`.
int wrapDelta(int to, int from, int size)
{
	int delta = wrap(to - from, size);
	return delta >= size / 2 ? delta - size : delta;
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value)
{
	hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
	return hash;
}

// The shadow layer drawn for a frame at this mask scale, and its own scale.
std::pair<DrawableSurface *, int> shadowLayer(Sprite *sprite, int frame, int scale)
{
	if (!sprite || frame < 0)
		return {nullptr, 0};
	auto *native = sprite->shadowFrame(unsigned(frame));
	if (!native || !native->getSDLSurface())
		return {nullptr, 0};
	if (scale > 1)
		if (auto *hd = sprite->highResolutionShadowFrame(unsigned(frame)); hd && hd->getSDLSurface())
			return {hd, 4};
	return {native, 1};
}
} // namespace

ShadowMask::ShadowMask() = default;
ShadowMask::~ShadowMask() = default;

void ShadowMask::gather(const PresentationFrame &scene, int chunkX, int chunkY, int scale, int localTeam,
						bool wholeMap, const BuildingGuiStateMap *buildingGuiState)
{
	casters.clear();
	const auto &map = scene.map;
	const auto &entities = scene.entities;
	const int w = map.getW(), h = map.getH();
	const int originX = chunkX * ChunkTiles, originY = chunkY * ChunkTiles;
	const int width = std::min(ChunkTiles, w - originX) * 32, height = std::min(ChunkTiles, h - originY) * 32;
	// The same visibility each layer's own draw applies.
	Uint32 resourceTeams = Team::teamNumberToMask(localTeam);
	Uint32 buildingTeams = entities.teams[localTeam].mask;
	if (globalContainer->isViewingGame())
		resourceTeams = buildingTeams = globalContainer->replayVisibleTeams;
	const auto add = [&](Sprite *sprite, int frame, int x, int y) {
		const auto [layer, sourceScale] = shadowLayer(sprite, frame, scale);
		if (!layer)
			return;
		const int sw = layer->getW() / sourceScale, sh = layer->getH() / sourceScale;
		if (x >= width || y >= height || x + sw <= 0 || y + sh <= 0)
			return;
		casters.push_back({layer, x, y, sourceScale});
	};

	// Buildings stand anchored at their footprint's bottom-right corner
	// (buildingSpriteOffset); their shadow layer shares the sprite's corner.
	std::vector<Uint16> seen;
	auto &catalog = ResourceSprites::resolve(map.frozenResourceRegistry(), map.frozenAssetBundle());
	for (int ty = originY - ReachTiles; ty < originY + height / 32 + ReachTiles; ++ty)
		for (int tx = originX - ReachTiles; tx < originX + width / 32 + ReachTiles; ++tx)
		{
			const int mx = wrap(tx, w), my = wrap(ty, h);
			const Uint16 gid = map.getBuilding(mx, my);
			if (gid != NOGBID && std::find(seen.begin(), seen.end(), gid) == seen.end())
			{
				seen.push_back(gid);
				const SnapshotBuilding *building = entities.building(gid);
				if (building &&
					(wholeMap || Building::GIDtoTeam(gid) == localTeam || (building->seenByMask & buildingTeams) ||
					 map.isFOWDiscovered(mx, my, buildingTeams)))
				{
					const BuildingType *type = entities.type(*building);
					Sprite *sprite = type->gameSpritePtr;
					if (sprite)
					{
						const int frame =
							buildingSpriteFrame(*type, building->hp, building->maxHp, entities.connections(*building));
						const Sint32 posX = buildingGuiState
							? displayedPosX(*buildingGuiState, building->gid, building->posX) : building->posX;
						const Sint32 posY = buildingGuiState
							? displayedPosY(*buildingGuiState, building->gid, building->posY) : building->posY;
						const auto offset = buildingSpriteOffset(*type, *sprite, frame);
						add(sprite, frame, wrapDelta(posX, originX, w) * 32 + offset.dx,
							wrapDelta(posY, originY, h) * 32 + offset.dy);
					}
				}
			}
			// Resources are centred on their cell (drawMapResources).
			const auto &resource = map.getResource(mx, my);
			if (resource.type == NO_RES_TYPE)
				continue;
			if (!wholeMap && !map.isMapPartiallyDiscovered(mx - 1, my - 1, mx + 1, my + 1, resourceTeams))
				continue;
			Sprite *sprite = catalog.sprite(static_cast<ResourceId>(resource.type));
			if (!sprite)
				continue;
			const int frame = map.resourceRegistry()
				.presentation(static_cast<ResourceId>(resource.type))
				.frame(resource.amount, mx, my, map.tick);
			if (frame < 0 || frame >= sprite->getFrameCount())
				continue;
			add(sprite, frame, (tx - originX) * 32 - ((sprite->getW(frame) - 32) >> 1),
				(ty - originY) * 32 - ((sprite->getH(frame) - 32) >> 1));
		}
}

void ShadowMask::compose(Chunk &chunk, int width, int height, int scale)
{
	chunkBytes -= chunk.bytes;
	chunk.bytes = 0;
	if (casters.empty())
	{
		chunk.image.reset();
		return;
	}
	const int pw = width * scale, ph = height * scale;
	coverage.assign(std::size_t(pw) * ph, 0);
	for (const auto &caster : casters)
	{
		const SDL_Surface *source = caster.layer->getSDLSurface();
		if (source->format != SDL_PIXELFORMAT_ARGB8888)
			continue;
		const auto alpha = [&](int sx, int sy) {
			return Uint8(static_cast<const Uint32 *>(
				static_cast<const void *>(static_cast<const Uint8 *>(source->pixels) + sy * source->pitch))[sx] >> 24);
		};
		const int ratio = caster.sourceScale / scale; // 2 (HD into a 2x mask), 1 or 0 (native into 2x)
		const int sw = source->w / caster.sourceScale * scale, sh = source->h / caster.sourceScale * scale;
		const int x0 = caster.x * scale, y0 = caster.y * scale;
		for (int v = std::max(0, -y0); v < sh && y0 + v < ph; ++v)
		{
			Uint8 *row = coverage.data() + std::size_t(y0 + v) * pw;
			for (int u = std::max(0, -x0); u < sw && x0 + u < pw; ++u)
			{
				int a;
				if (ratio >= 2)
					a = (alpha(u * 2, v * 2) + alpha(u * 2 + 1, v * 2) + alpha(u * 2, v * 2 + 1) +
						 alpha(u * 2 + 1, v * 2 + 1) + 2) >> 2;
				else if (ratio == 1)
					a = alpha(u, v);
				else
					a = alpha(u * caster.sourceScale / scale, v * caster.sourceScale / scale);
				Uint8 &target = row[x0 + u];
				target = std::max(target, Uint8(a));
			}
		}
	}
	SDL_Surface *pixels = chunk.image ? chunk.image->getSDLSurface() : nullptr;
	if (!pixels || pixels->w != pw || pixels->h != ph)
	{
		chunk.image.reset();
		pixels = SDL_CreateSurface(pw, ph, SDL_PIXELFORMAT_ARGB8888);
		if (!pixels)
			throw std::bad_alloc();
		chunk.image = std::make_unique<MaskSurface>(pixels, scale > 1);
	}
	for (int y = 0; y < ph; ++y)
	{
		auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + y * pixels->pitch);
		const Uint8 *source = coverage.data() + std::size_t(y) * pw;
		for (int x = 0; x < pw; ++x)
			row[x] = Uint32(source[x]) << 24;
	}
	chunk.image->markPixelsChanged();
	chunk.bytes = std::size_t(pw) * ph * 4 * 2; // CPU pixels and the texture
	chunkBytes += chunk.bytes;
}

void ShadowMask::evict(std::size_t keepBytes)
{
	while (chunkBytes > keepBytes)
	{
		auto oldest = chunks.end();
		for (auto it = chunks.begin(); it != chunks.end(); ++it)
			if (it->second.used != use && (oldest == chunks.end() || it->second.used < oldest->second.used))
				oldest = it;
		if (oldest == chunks.end())
			return; // everything left is on screen
		chunkBytes -= oldest->second.bytes;
		chunks.erase(oldest);
	}
}

void ShadowMask::draw(const PresentationFrame &scene, int left, int top, int right, int bottom, int viewportX,
					  int viewportY, int localTeam, bool wholeMap, const BuildingGuiStateMap *buildingGuiState,
					  float opacity)
{
	const auto &map = scene.map;
	const int alpha = int(std::lround(255.f * Sunlight::groundShadowOpacity * std::clamp(opacity, 0.f, 1.f)));
	if (alpha <= 0 || map.getW() <= 0 || map.getH() <= 0)
		return;
	if (map.identity() != mapIdentity)
	{
		chunks.clear();
		chunkBytes = 0;
		mapIdentity = map.identity();
	}
	struct Visible { int chunkX, chunkY, screenX, screenY, width, height; };
	std::vector<Visible> visible;
	for (int y = top; y <= bottom;)
	{
		const int my = wrap(y + viewportY, map.getH());
		const int chunkY = my / ChunkTiles, firstY = y - my % ChunkTiles;
		const int height = std::min(ChunkTiles, map.getH() - chunkY * ChunkTiles);
		for (int x = left; x <= right;)
		{
			const int mx = wrap(x + viewportX, map.getW());
			const int chunkX = mx / ChunkTiles, firstX = x - mx % ChunkTiles;
			const int width = std::min(ChunkTiles, map.getW() - chunkX * ChunkTiles);
			visible.push_back({chunkX, chunkY, firstX, firstY, width, height});
			x = firstX + width;
		}
		y = firstY + height;
	}
	if (visible.size() > MaximumVisibleChunks)
		return;
	// The 2x mask only when the view's chunks fit the budget (a phone or
	// HiDPI view may not); otherwise the native mask, a quarter the bytes.
	int scale = Sprite::highResolutionShown() ? 2 : 1;
	if (scale > 1)
	{
		std::size_t viewBytes = 0;
		for (const auto &cell : visible)
			viewBytes += std::size_t(cell.width * 32 * scale) * (cell.height * 32 * scale) * 4 * 2;
		if (viewBytes > byteBudget())
			scale = 1;
	}
	++use;
	auto *gfx = globalContainer->gfx;
	for (const auto &cell : visible)
	{
		const std::uint64_t key = (std::uint64_t(unsigned(cell.chunkX)) << 33) |
			(std::uint64_t(unsigned(cell.chunkY)) << 1) | (scale > 1 ? 1u : 0u);
		auto &chunk = chunks[key];
		if (chunk.used != use)
		{
			chunk.used = use;
			gather(scene, cell.chunkX, cell.chunkY, scale, localTeam, wholeMap, buildingGuiState);
			std::uint64_t signature = mix(0x5eed, std::uint64_t(scale));
			for (const auto &caster : casters)
			{
				signature = mix(signature, reinterpret_cast<std::uintptr_t>(caster.layer));
				signature = mix(signature, std::uint64_t(std::uint32_t(caster.x)) << 32 | std::uint32_t(caster.y));
				signature = mix(signature, std::uint64_t(caster.sourceScale));
			}
			signature = mix(signature, casters.size());
			if (signature != chunk.signature || (!chunk.image && !casters.empty()))
			{
				chunk.signature = signature;
				compose(chunk, cell.width * 32, cell.height * 32, scale);
			}
		}
		if (chunk.image)
			gfx->drawSurface(float(cell.screenX * 32), float(cell.screenY * 32), float(cell.width * 32),
							 float(cell.height * 32), chunk.image.get(), Uint8(alpha));
	}
	evict(byteBudget());
}
