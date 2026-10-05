// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#pragma once

#include <cstdint>
#include <cassert>
#include <cstddef>
#include <vector>

class Map;
struct TerrainProperties;

/// Deterministic terrain ecology. Contributions and inhibition are averaged by
/// a weighted coupled kernel; runtime growth samples the resulting exact rates
/// rather than probing neighboring terrain on every visit.
namespace Fertility
{
	/// A tile surrounded entirely by qualifying water reaches exactly this.
	constexpr std::uint32_t kScale = 65536;

    // Fields retain Q16, but rates use a common denominator divisible by three
    // so even a one-point fertile wheat tile has positive exact probability.
    constexpr std::uint32_t kRateScale=3u*kScale;
    constexpr std::uint32_t kRateDrawLimit=std::uint32_t((std::uint64_t{1}<<32)/kRateScale*kRateScale);

    /// Draw must return a uniform full-width uint32. Reject the incomplete top
    /// bucket (1/65536 of draws) before modulo; fractional rates are unbiased.
    template<class Draw> unsigned growthOpportunities(std::uint32_t rate,Draw&& draw)
    {
        assert(rate<=4u*kRateScale);
        unsigned result=rate/kRateScale;
        const auto fraction=rate%kRateScale;
        if(fraction)
        {
            std::uint32_t random;
            do { random=draw(); } while(random>=kRateDrawLimit);
            result+=(random%kRateScale)<fraction;
        }
        return result;
    }

	enum class Path
	{
		Adaptive,           ///< choose the cheaper sparse correction or donor accumulation
		SandCorrection,     ///< linear donor convolution minus opposite inhibition
		WaterSplat          ///< accumulate coupled weights from nonzero donors
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

		void rebuildWeighted(int width, int height,
			const std::vector<std::int16_t>& contributionQ8,
			const std::vector<std::uint16_t>& inhibitionQ8,Path path=Path::Adaptive);
		void multiplyLocal(const std::vector<std::uint16_t>& growthQ8);

	private:
		int width = 0, height = 0;
		int waterTiles = 0, sandTiles = 0;
		Path usedPath = Path::SandCorrection;
		std::vector<std::uint32_t> fertility;
	};

	/// Same triangular offsets as land growth, pairing each donor with shoreline
	/// at the rotated doubled offset. Weighted inputs are Q8; results are Q16.
	std::vector<std::uint32_t> shoreGrowthField(int width,int height,
		const std::vector<std::int16_t>& contributionQ8,
		const std::vector<std::uint16_t>& supportQ8);

	class GrowthCache
	{
	public:
		void invalidate() { ready = false; }
		/// Preserve cached fields when only habitat permissions or non-ecology
		/// capabilities change. Map replacement always calls invalidate().
		void terrainChanged(std::size_t index, const TerrainProperties& before,
			const TerrainProperties& after);
		bool validFor(const Map& map) const;
		void rebuild(const Map& map);
		/// Expected opportunities per visit divided by kRateScale; bonuses permit up to four.
		std::uint32_t rate(std::size_t index, int resourceType) const;
		const Field& landField() const { return land; }
		const std::vector<std::uint32_t>& aquaticField() const { return aquatic; }
	private:
		bool ready = false;
		Field land;
		std::vector<std::uint32_t> aquatic;
		std::vector<std::uint16_t> localGrowth;
		std::vector<std::uint16_t> growthHabitats;
	};

	/// Uses canonical terrain properties and the shared cached field. Tiles that no wheat
	/// or wood deposit can reach over crop habitat score 0 whatever their surroundings: growth
	/// spreads from an existing deposit, so a tile none can reach never grows anything.
	Field forMap(const Map& map, bool gateOnReachableDeposits = true);

	/// Executes a selected tile's bounded rate, including the fractional draw.
	void applyGrowthOpportunities(Map& map,int x,int y,std::uint32_t rate,int scarcity);

	/// Throughput a tile can actually sustain: the growth chance, the deposit already
	/// there and the room it has to spread into, all of which must be non-zero to matter.
	std::uint32_t usefulExpansionCapacity(std::uint32_t fertility, int amount,
		int availableNeighbors, bool wheat);

	bool withinPercentBand(std::uint32_t fertility, int minimumPercent, int maximumPercent);
}
