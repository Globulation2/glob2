// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#pragma once

#include <cstdint>
#include <vector>

class Map;

/// Exact per-tile growth probability for the resources whose spread is limited by
/// nearby underground, scaled by kScale.
///
/// Map::growResources decides whether a wheat or wood tile expands with
///
///     dwax = (syncRand()&0xF) - (syncRand()&0xF);   // likewise dway
///     expand = isWater(x+dwax, y+dway) && !isSand(x-dwax, y-dway);
///
/// The difference of two uniform draws from {0..15} has PMF (16-|d|)/256, so the
/// chance a tile expands is the triangular kernel below summed over every water tile
/// whose mirror through the tile is not sand. That sum is what this computes, in
/// closed form, over the whole map.
namespace Fertility
{
	/// A tile surrounded entirely by qualifying water reaches exactly this.
	constexpr std::uint32_t kScale = 65536;

	enum class Path
	{
		Adaptive,           ///< whichever of the two below costs less on this map
		SandCorrection,     ///< convolve water, then subtract the sand-blocked pairs
		WaterSplat          ///< accumulate straight from the water tiles
	};

	/// Masks are row-major y*width+x. Toroidal, matching the engine's wrapping.
	class Field
	{
	public:
		void rebuild(int width, int height, const std::vector<std::uint8_t>& water,
			const std::vector<std::uint8_t>& sand, Path path = Path::Adaptive);

		/// Zeroes every tile the mask does not keep. Masks are row-major y*width+x.
		void gate(const std::vector<std::uint8_t>& keep);

		std::uint32_t at(int x, int y) const;
		const std::vector<std::uint32_t>& values() const { return fertility; }
		int getW() const { return width; }
		int getH() const { return height; }
		Path pathUsed() const { return usedPath; }
		int waterCount() const { return waterTiles; }
		int sandCount() const { return sandTiles; }

	private:
		int width = 0, height = 0;
		int waterTiles = 0, sandTiles = 0;
		Path usedPath = Path::Adaptive;
		std::vector<std::uint32_t> fertility, first, second;
		std::vector<int> wrappedX, wrappedY;
		int wx(int x, int offset) const;
		int wy(int y, int offset) const;
		void buildWrappedIndexes();
		void buildWaterConvolution(const std::vector<std::uint8_t>& water);
	};

	/// Builds the masks from the undermap and computes the field. Tiles that no wheat
	/// or wood deposit can reach over grass score 0 whatever their surroundings: growth
	/// spreads from an existing deposit, so a tile none can reach never grows anything.
	Field forMap(const Map& map, bool gateOnReachableDeposits = true);

	/// Throughput a tile can actually sustain: the growth chance, the deposit already
	/// there and the room it has to spread into, all of which must be non-zero to matter.
	std::uint32_t usefulExpansionCapacity(std::uint32_t fertility, int amount,
		int availableNeighbors, bool wheat);

	bool withinPercentBand(std::uint32_t fertility, int minimumPercent, int maximumPercent);
}
