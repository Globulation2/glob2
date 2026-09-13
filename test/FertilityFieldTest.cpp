// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "FertilityFieldTest.h"

#include "Map.h"
#include "map/FertilityField.h"

#include <array>
#include <cstdint>
#include <vector>

CPPUNIT_TEST_SUITE_REGISTRATION( FertilityFieldTest );

namespace
{
	using Mask = std::vector<std::uint8_t>;

	int wrap(int v, int n) { return (v % n + n) % n; }

	/// The weight Map::growResources gives an offset: the number of (a,b) pairs in
	/// {0..15} with a-b == offset, which is 16-|offset|.
	std::uint32_t weight(int offset) { return offset >= 16 || offset <= -16 ? 0 : 16 - std::abs(offset); }

	/// growResources' test, evaluated tap by tap: water at the offset, and the tile
	/// mirrored through the candidate not sand.
	std::uint32_t directKernel(const Mask& water, const Mask& sand, int w, int h, int x, int y)
	{
		std::uint32_t total = 0;
		for (int dy = -15; dy <= 15; ++dy)
			for (int dx = -15; dx <= 15; ++dx)
			{
				const size_t towards = size_t(wrap(y + dy, h)) * w + wrap(x + dx, w);
				const size_t away = size_t(wrap(y - dy, h)) * w + wrap(x - dx, w);
				if (water[towards] && !sand[away])
					total += weight(dx) * weight(dy);
			}
		return total;
	}

	void assertMatchesDirect(const Mask& water, const Mask& sand, int w, int h,
		Fertility::Path path)
	{
		Fertility::Field field;
		field.rebuild(w, h, water, sand, path);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				CPPUNIT_ASSERT_EQUAL(directKernel(water, sand, w, h, x, y), field.at(x, y));
	}

	/// Deterministic so a failure is reproducible; syncRand is unavailable here anyway.
	struct Lcg
	{
		std::uint32_t state;
		explicit Lcg(std::uint32_t seed): state(seed) {}
		std::uint32_t next() { state = state * 1664525u + 1013904223u; return state >> 16; }
		bool chance(int percent) { return int(next() % 100) < percent; }
	};

	void fillRandom(Mask& water, Mask& sand, int w, int h, std::uint32_t seed,
		int waterPercent, int sandPercent)
	{
		Lcg rng(seed);
		water.assign(size_t(w) * h, 0);
		sand.assign(size_t(w) * h, 0);
		for (size_t i = 0; i < water.size(); ++i)
		{
			water[i] = rng.chance(waterPercent);
			if (!water[i])
				sand[i] = rng.chance(sandPercent);
		}
	}

	constexpr int kMapDec = 5;   // 32x32

	// Minimal Map for the forMap() gate, following the MapQueryTest pattern: setSize()
	// would allocate a real Sector[] and drag most of the game into the link.
	struct TinyMap : Map
	{
		TinyMap()
		{
			wDec = hDec = kMapDec;
			w = h = 1 << kMapDec;
			wMask = hMask = w - 1;
			size = size_t(w) * h;
			tiles.assign(size, Tile());
		}
		~TinyMap() { w = h = wMask = hMask = wDec = hDec = 0; size = 0; }

		void makeWater(int x, int y) { tiles[coordToIndex(x, y)].terrain = 256; }
		void makeSand(int x, int y) { tiles[coordToIndex(x, y)].terrain = 128; }
		void putResource(int x, int y, int type)
		{
			Resource& r = tiles[coordToIndex(x, y)].resource;
			r.type = type;
			r.amount = 1;
		}
	};
}

void FertilityFieldTest::testTriangularWeightsMatchGrowResourcesRng()
{
	// growResources draws dwax = (syncRand()&0xF) - (syncRand()&0xF). Enumerating both
	// draws gives the kernel the field is built from.
	std::array<int, 31> histogram{};
	for (int a = 0; a <= 15; ++a)
		for (int b = 0; b <= 15; ++b)
			++histogram[a - b + 15];
	for (int offset = -15; offset <= 15; ++offset)
		CPPUNIT_ASSERT_EQUAL(int(weight(offset)), histogram[offset + 15]);

	// Both axes together span exactly the scale the field is expressed in.
	std::uint32_t total = 0;
	for (int dy = -15; dy <= 15; ++dy)
		for (int dx = -15; dx <= 15; ++dx)
			total += weight(dx) * weight(dy);
	CPPUNIT_ASSERT_EQUAL(Fertility::kScale, total);
}

void FertilityFieldTest::testConvolutionMatchesDirectKernelWithoutSand()
{
	Mask water, sand;
	fillRandom(water, sand, 32, 32, 7u, 30, 0);
	assertMatchesDirect(water, sand, 32, 32, Fertility::Path::SandCorrection);
	assertMatchesDirect(water, sand, 32, 32, Fertility::Path::WaterSplat);
}

void FertilityFieldTest::testSandCorrectionPathMatchesDirectKernel()
{
	Mask water, sand;
	for (std::uint32_t seed : {1u, 2u, 3u})
	{
		fillRandom(water, sand, 32, 32, seed, 25, 40);
		assertMatchesDirect(water, sand, 32, 32, Fertility::Path::SandCorrection);
	}
}

void FertilityFieldTest::testWaterSplatPathMatchesDirectKernel()
{
	Mask water, sand;
	for (std::uint32_t seed : {1u, 2u, 3u})
	{
		fillRandom(water, sand, 32, 32, seed, 25, 40);
		assertMatchesDirect(water, sand, 32, 32, Fertility::Path::WaterSplat);
	}
}

void FertilityFieldTest::testAdaptivePathMatchesExplicitPaths()
{
	Mask water, sand;
	// Little water, much sand, and the reverse: the cost rule should pick a different
	// path for each, and both must land on the same numbers.
	for (auto [waterPercent, sandPercent] : {std::pair{5, 60}, std::pair{60, 5}})
	{
		fillRandom(water, sand, 32, 32, 11u, waterPercent, sandPercent);
		Fertility::Field adaptive, correction, splat;
		adaptive.rebuild(32, 32, water, sand, Fertility::Path::Adaptive);
		correction.rebuild(32, 32, water, sand, Fertility::Path::SandCorrection);
		splat.rebuild(32, 32, water, sand, Fertility::Path::WaterSplat);
		CPPUNIT_ASSERT(adaptive.pathUsed() != Fertility::Path::Adaptive);
		CPPUNIT_ASSERT(correction.values() == splat.values());
		CPPUNIT_ASSERT(adaptive.values() == correction.values());
	}
}

void FertilityFieldTest::testOpenWaterReachesFullScale()
{
	const Mask water(32 * 32, 1), sand(32 * 32, 0);
	Fertility::Field field;
	field.rebuild(32, 32, water, sand, Fertility::Path::SandCorrection);
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
			CPPUNIT_ASSERT_EQUAL(Fertility::kScale, field.at(x, y));
}

void FertilityFieldTest::testSandOppositeWaterRemovesCredit()
{
	// One water tile three east of the centre contributes weight(3)*weight(0) = 13*16.
	Mask water(32 * 32, 0), sand(32 * 32, 0);
	water[size_t(16) * 32 + 19] = 1;
	Fertility::Field bare;
	bare.rebuild(32, 32, water, sand, Fertility::Path::SandCorrection);
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(13 * 16), bare.at(16, 16));

	// Sand on the far side of the centre from that water cancels it, and only there.
	sand[size_t(16) * 32 + 13] = 1;
	Fertility::Field corrected;
	corrected.rebuild(32, 32, water, sand, Fertility::Path::SandCorrection);
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), corrected.at(16, 16));
	CPPUNIT_ASSERT_EQUAL(bare.at(17, 16), corrected.at(17, 16));
}

void FertilityFieldTest::testNonPowerOfTwoDimensionsWrap()
{
	// The field is used at generation time too, where dimensions need not be masks.
	Mask water, sand;
	fillRandom(water, sand, 23, 17, 5u, 25, 35);
	assertMatchesDirect(water, sand, 23, 17, Fertility::Path::SandCorrection);
	assertMatchesDirect(water, sand, 23, 17, Fertility::Path::WaterSplat);
}

void FertilityFieldTest::testForMapZeroesNonGrass()
{
	TinyMap map;
	map.makeWater(4, 4);
	map.makeSand(6, 6);
	map.putResource(10, 10, CORN);
	const Fertility::Field field = Fertility::forMap(map);
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), field.at(4, 4));
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), field.at(6, 6));
	CPPUNIT_ASSERT(field.at(5, 4) > 0u);
}

void FertilityFieldTest::testForMapZeroesGrassNoDepositReaches()
{
	// An island of grass ringed by water, with the only deposit outside the ring.
	TinyMap map;
	for (int d = -2; d <= 2; ++d)
	{
		map.makeWater(14 + d, 14 - 2);
		map.makeWater(14 + d, 14 + 2);
		map.makeWater(14 - 2, 14 + d);
		map.makeWater(14 + 2, 14 + d);
	}
	map.putResource(25, 25, CORN);
	const Fertility::Field field = Fertility::forMap(map);
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), field.at(14, 14));
	CPPUNIT_ASSERT(field.at(25, 25) > 0u);
}

void FertilityFieldTest::testForMapUngatedKeepsUnreachableGrass()
{
	TinyMap map;
	map.makeWater(4, 4);
	// No deposit anywhere: the gated field is empty, the ungated one is not.
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), Fertility::forMap(map).at(5, 4));
	CPPUNIT_ASSERT(Fertility::forMap(map, false).at(5, 4) > 0u);
}

void FertilityFieldTest::testUsefulExpansionCapacityFolds()
{
	// Wood: fertility * amount/8 * neighbours/8. Wheat additionally clears
	// CORN_GROWTH_DIVISOR only one time in three.
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(65536), Fertility::usefulExpansionCapacity(65536, 8, 8, false));
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(65536 / 3), Fertility::usefulExpansionCapacity(65536, 8, 8, true));
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), Fertility::usefulExpansionCapacity(65536, 0, 8, false));
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), Fertility::usefulExpansionCapacity(65536, 8, 0, false));
	// Out-of-range inputs clamp rather than overflow the fold.
	CPPUNIT_ASSERT_EQUAL(Fertility::usefulExpansionCapacity(1024, 8, 8, false),
		Fertility::usefulExpansionCapacity(1024, 99, 99, false));
	CPPUNIT_ASSERT_EQUAL(std::uint32_t(0), Fertility::usefulExpansionCapacity(1024, -5, 8, false));
}

void FertilityFieldTest::testWithinPercentBand()
{
	CPPUNIT_ASSERT(Fertility::withinPercentBand(Fertility::kScale / 2, 40, 60));
	CPPUNIT_ASSERT(!Fertility::withinPercentBand(Fertility::kScale / 2, 60, 90));
	CPPUNIT_ASSERT(Fertility::withinPercentBand(0, 0, 10));
	// Inclusive at both ends, and a reversed band is read as written.
	CPPUNIT_ASSERT(Fertility::withinPercentBand(Fertility::kScale, 100, 100));
	CPPUNIT_ASSERT(Fertility::withinPercentBand(Fertility::kScale / 2, 60, 40));
}
