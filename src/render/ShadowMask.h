// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <GraphicContext.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "BuildingGuiState.h"

struct PresentationFrame;

//! The ground shadows that buildings and resources cast (their sprites' shadow
//! layers, Sprite::shadowFrame), drawn as one pass over the terrain and under
//! every resource, unit and building, so nothing standing in a shadow is
//! darkened by it and a building never shadows its neighbour's roof.
//!
//! Overlapping shadows do not stack: each map chunk of ChunkTiles x ChunkTiles
//! tiles composes the coverage of every shadow reaching it with a maximum, on
//! the CPU, into one mask, and the pass draws the visible chunk masks at the
//! sun's fixed opacity (Sunlight::groundShadowOpacity). Every renderer draws
//! the same masks, the software one included. A chunk recomposes only when the
//! shadows reaching it change (a building built, upgraded or damaged, a
//! resource harvested or growing, an HD layer arriving); otherwise it is
//! drawn from its cached texture.
//!
//! Masks are composed at one pixel per map pixel while the HD sprite policy
//! shows native artwork and two while it shows HD (the HD shadow layers are
//! four, area-averaged down; shadows are soft, and two keeps a chunk at 1 MiB).
//! Shadow layers and masks stay RGBA: the layers are read on the CPU and the
//! masks change on the render thread, so neither is block compressed, and the
//! coverage stays exact. Masks off screen are kept within the render budget's
//! shadow limit (GAGCore::RenderBudget::Terrain::shadows).
class ShadowMask
{
public:
	static constexpr int ChunkTiles = 8;
	//! Tiles around a chunk searched for shadows reaching into it: the widest
	//! sprite overhang plus the longest shadow (about 20 native pixels).
	static constexpr int ReachTiles = 3;

	ShadowMask();
	~ShadowMask();

	//! Draws the shadows over screen tiles left..right x top..bottom (as
	//! drawMapResources numbers them), at `opacity` times the sun's.
	void draw(const PresentationFrame &scene, int left, int top, int right, int bottom, int viewportX, int viewportY,
			  int localTeam, bool wholeMap, const BuildingGuiStateMap *buildingGuiState, float opacity);

	std::size_t bytes() const { return chunkBytes; }

private:
	struct Chunk
	{
		std::uint64_t signature = 0;
		std::unique_ptr<GAGCore::DrawableSurface> image; // null: no shadow reaches it
		std::uint64_t used = 0;
		std::size_t bytes = 0;
	};
	//! One shadow layer reaching a chunk, at its top-left corner in chunk pixels.
	struct Caster
	{
		GAGCore::DrawableSurface *layer;
		int x, y;
		int sourceScale; // pixels per map pixel of `layer`: 1 native, 4 HD
	};
	std::unordered_map<std::uint64_t, Chunk> chunks;
	std::size_t chunkBytes = 0;
	std::uint64_t use = 0;
	std::uint64_t mapIdentity = 0;
	std::vector<Caster> casters;
	std::vector<std::uint8_t> coverage;
	void gather(const PresentationFrame &scene, int chunkX, int chunkY, int scale, int localTeam,
				bool wholeMap, const BuildingGuiStateMap *buildingGuiState);
	void compose(Chunk &chunk, int width, int height, int scale);
	void evict(std::size_t keepBytes);
};
