// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "FertilityFieldTest.h"
#include <utility>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Map.h"
#include "map/FertilityField.h"

#include <array>
#include <cstdint>
#include <vector>

TEST_SUITE("FertilityField")
{
	TEST_CASE_FIXTURE(FertilityFieldTest, "TriangularWeightsMatchGrowResourcesRng") { testTriangularWeightsMatchGrowResourcesRng(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "ConvolutionMatchesDirectKernelWithoutSand") { testConvolutionMatchesDirectKernelWithoutSand(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "SandCorrectionPathMatchesDirectKernel") { testSandCorrectionPathMatchesDirectKernel(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "WaterSplatPathMatchesDirectKernel") { testWaterSplatPathMatchesDirectKernel(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "AdaptivePathMatchesExplicitPaths") { testAdaptivePathMatchesExplicitPaths(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "OpenWaterReachesFullScale") { testOpenWaterReachesFullScale(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "SandOppositeWaterRemovesCredit") { testSandOppositeWaterRemovesCredit(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "NonPowerOfTwoDimensionsWrap") { testNonPowerOfTwoDimensionsWrap(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "ForMapZeroesNonGrass") { testForMapZeroesNonGrass(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "ForMapZeroesGrassNoDepositReaches") { testForMapZeroesGrassNoDepositReaches(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "ForMapUngatedKeepsUnreachableGrass") { testForMapUngatedKeepsUnreachableGrass(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "UsefulExpansionCapacityFolds") { testUsefulExpansionCapacityFolds(); }
	TEST_CASE_FIXTURE(FertilityFieldTest, "WithinPercentBand") { testWithinPercentBand(); }
}

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
				CHECK_EQ(directKernel(water, sand, w, h, x, y), field.at(x, y));
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
			importLegacyTerrain();
		}
		~TinyMap() { w = h = wMask = hMask = wDec = hDec = 0; size = 0; }

		void makeWater(int x, int y) { setCellTerrain(x,y,WATER); }
		void makeSand(int x, int y) { setCellTerrain(x,y,SAND); }
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
		CHECK_EQ(int(weight(offset)), histogram[offset + 15]);

	// Both axes together span exactly the scale the field is expressed in.
	std::uint32_t total = 0;
	for (int dy = -15; dy <= 15; ++dy)
		for (int dx = -15; dx <= 15; ++dx)
			total += weight(dx) * weight(dy);
	CHECK_EQ(Fertility::kScale, total);
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
		CHECK(adaptive.pathUsed() != Fertility::Path::Adaptive);
		CHECK(correction.values() == splat.values());
		CHECK(adaptive.values() == correction.values());
	}
}

void FertilityFieldTest::testOpenWaterReachesFullScale()
{
	const Mask water(32 * 32, 1), sand(32 * 32, 0);
	Fertility::Field field;
	field.rebuild(32, 32, water, sand, Fertility::Path::SandCorrection);
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
			CHECK_EQ(Fertility::kScale, field.at(x, y));
}

void FertilityFieldTest::testSandOppositeWaterRemovesCredit()
{
	// One water tile three east of the centre contributes weight(3)*weight(0) = 13*16.
	Mask water(32 * 32, 0), sand(32 * 32, 0);
	water[size_t(16) * 32 + 19] = 1;
	Fertility::Field bare;
	bare.rebuild(32, 32, water, sand, Fertility::Path::SandCorrection);
	CHECK_EQ(std::uint32_t(13 * 16), bare.at(16, 16));

	// An inhibitor at the opposite offset cancels this precise contribution.
	sand[size_t(16) * 32 + 13] = 1;
	Fertility::Field corrected;
	corrected.rebuild(32, 32, water, sand, Fertility::Path::SandCorrection);
	CHECK_EQ(directKernel(water,sand,32,32,16,16), corrected.at(16,16));
	CHECK_EQ(std::uint32_t(0),corrected.at(16,16));
	CHECK_EQ(directKernel(water,sand,32,32,17,16), corrected.at(17,16));
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
	map.putResource(10, 10, WHEAT);
	const Fertility::Field field = Fertility::forMap(map);
	CHECK_EQ(std::uint32_t(0), field.at(4, 4));
	CHECK_EQ(std::uint32_t(0), field.at(6, 6));
	CHECK(field.at(5, 4) > 0u);
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
	map.putResource(25, 25, WHEAT);
	const Fertility::Field field = Fertility::forMap(map);
	CHECK_EQ(std::uint32_t(0), field.at(14, 14));
	CHECK(field.at(25, 25) > 0u);
}

void FertilityFieldTest::testForMapUngatedKeepsUnreachableGrass()
{
	TinyMap map;
	map.makeWater(4, 4);
	// No deposit anywhere: the gated field is empty, the ungated one is not.
	CHECK_EQ(std::uint32_t(0), Fertility::forMap(map).at(5, 4));
	CHECK(Fertility::forMap(map, false).at(5, 4) > 0u);
}

void FertilityFieldTest::testUsefulExpansionCapacityFolds()
{
	// Wood: fertility * amount/8 * neighbours/8. Wheat additionally clears
	// WHEAT_GROWTH_DIVISOR only one time in three.
	CHECK_EQ(std::uint32_t(65536), Fertility::usefulExpansionCapacity(65536, 8, 8, false));
	CHECK_EQ(std::uint32_t(65536 / 3), Fertility::usefulExpansionCapacity(65536, 8, 8, true));
	CHECK_EQ(std::uint32_t(0), Fertility::usefulExpansionCapacity(65536, 0, 8, false));
	CHECK_EQ(std::uint32_t(0), Fertility::usefulExpansionCapacity(65536, 8, 0, false));
	// Out-of-range inputs clamp rather than overflow the fold.
	CHECK_EQ(Fertility::usefulExpansionCapacity(1024, 8, 8, false), Fertility::usefulExpansionCapacity(1024, 99, 99, false));
	CHECK_EQ(std::uint32_t(0), Fertility::usefulExpansionCapacity(1024, -5, 8, false));
}

void FertilityFieldTest::testWithinPercentBand()
{
	CHECK(Fertility::withinPercentBand(Fertility::kScale / 2, 40, 60));
	CHECK(!Fertility::withinPercentBand(Fertility::kScale / 2, 60, 90));
	CHECK(Fertility::withinPercentBand(0, 0, 10));
	// Inclusive at both ends, and a reversed band is read as written.
	CHECK(Fertility::withinPercentBand(Fertility::kScale, 100, 100));
	CHECK(Fertility::withinPercentBand(Fertility::kScale / 2, 60, 40));
}

TEST_SUITE("FertilityField")
{
TEST_CASE("weighted ecology handles signed donors, inhibition and local growth")
{
    Fertility::Field field;
    std::vector<std::int16_t> donors(12,256);
    std::vector<std::uint16_t> inhibition(12,128),local(12,512);
    field.rebuildWeighted(4,3,donors,inhibition);
    for(auto value:field.values()) CHECK(value==32768);
    field.multiplyLocal(local);
    for(auto value:field.values()) CHECK(value==65536);
    std::fill(donors.begin(),donors.end(),-256);
    field.rebuildWeighted(4,3,donors,inhibition);
    for(auto value:field.values()) CHECK(value==0);
    std::fill(donors.begin(),donors.end(),1024);
    std::fill(inhibition.begin(),inhibition.end(),0);
    field.rebuildWeighted(4,3,donors,inhibition);
    for(auto value:field.values()) CHECK(value==65536);
    std::fill(inhibition.begin(),inhibition.end(),512);
    field.rebuildWeighted(4,3,donors,inhibition);
    for(auto value:field.values()) CHECK(value==0);
}

TEST_CASE("cached ecology changes after canonical terrain mutation")
{
    TinyMap map;
    const auto initial=map.resourceGrowthField().landField().at(8,8);
    CHECK(initial==0);
    map.makeWater(9,8);
    const auto watered=map.resourceGrowthField().landField().at(8,8);
    CHECK(watered>initial);
    map.setCellTerrain(8,8,TRAIL);
    CHECK(map.resourceGrowthField().rate(map.coordToIndex(8,8),WHEAT)==0);
    map.setCellTerrain(8,8,GRASS);
    CHECK(map.resourceGrowthField().rate(map.coordToIndex(8,8),WHEAT)>0);
}

TEST_CASE("growth throughput includes multiple opportunities before occupancy")
{
    CHECK(Fertility::usefulExpansionCapacity(4*Fertility::kScale,8,8,false)==4*Fertility::kScale);
    CHECK(Fertility::usefulExpansionCapacity(4*Fertility::kScale,4,8,false)==2*Fertility::kScale);
    CHECK(Fertility::usefulExpansionCapacity(4*Fertility::kScale,8,8,true)==4*Fertility::kScale/3);
    CHECK(Fertility::usefulExpansionCapacity(2*Fertility::kScale,8,8,true)==2*Fertility::kScale/3);
}

TEST_CASE("weighted coupled land and shoreline kernels match independent probes")
{
    for(const auto dimensions:{std::pair{1,1},std::pair{7,5},std::pair{32,16}})
        for(int sparse: {0,1,2})
        {
            const auto [w,h]=dimensions;
            std::vector<std::int16_t> donors(w*h);
            std::vector<std::uint16_t> inhibition(w*h),shore(w*h);
            Lcg random(919+sparse);
            for(int i=0;i<w*h;++i)
            {
                donors[i]=random.chance(sparse==1?10:70)?int(random.next()%1537)-512:0;
                inhibition[i]=random.chance(sparse==2?10:70)?random.next()%1025:0;
                shore[i]=inhibition[i];
            }
            Fertility::Field correction,splat;
            correction.rebuildWeighted(w,h,donors,inhibition,Fertility::Path::SandCorrection);
            splat.rebuildWeighted(w,h,donors,inhibition,Fertility::Path::WaterSplat);
            REQUIRE(correction.values()==splat.values());
            const auto aquatic=Fertility::shoreGrowthField(w,h,donors,shore);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            {
                std::int64_t land=0,water=0;
                for(int dy=-15;dy<=15;++dy)for(int dx=-15;dx<=15;++dx)
                {
                    const auto donor=donors[wrap(y+dy,h)*w+wrap(x+dx,w)];
                    const int attenuation=256-std::min<int>(256,inhibition[wrap(y-dy,h)*w+wrap(x-dx,w)]);
                    const int support=shore[wrap(y+2*dx,h)*w+wrap(x+2*dy,w)];
                    const auto weighted=std::int64_t(donor)*weight(dx)*weight(dy);
                    land+=weighted*attenuation;water+=weighted*support;
                }
                const auto clamp=[](std::int64_t value) {
                    return std::uint32_t(std::clamp<std::int64_t>(value/(256*256),0,Fertility::kScale));
                };
                CHECK(correction.at(x,y)==clamp(land));
                CHECK(aquatic[y*w+x]==clamp(water));
            }
        }
}

TEST_CASE("one-point wheat fertility retains exact positive growth potential")
{
    TinyMap map;
    map.makeWater(15,15);
    const auto& field=map.resourceGrowthField();
    REQUIRE(field.landField().at(0,0)==1);
    CHECK(field.rate(map.coordToIndex(0,0),WHEAT)==1);
    CHECK(field.rate(map.coordToIndex(0,0),WOOD)==3);
    unsigned draws=0;
    CHECK(Fertility::growthOpportunities(field.rate(0,WHEAT),[&]{++draws;return 0u;})==1);
    CHECK(draws==1);
}

TEST_CASE("fractional growth rejects incomplete RNG bucket and preserves whole bonuses")
{
    static_assert(Fertility::kRateDrawLimit==4294901760u);
    static_assert(std::uint64_t(Fertility::kRateDrawLimit)%Fertility::kRateScale==0);
    unsigned draws=0;
    const auto counted=[&]{++draws;return 0u;};
    CHECK(Fertility::growthOpportunities(0,counted)==0);
    CHECK(Fertility::growthOpportunities(4*Fertility::kRateScale,counted)==4);
    CHECK(draws==0);
    const std::array<std::uint32_t,3> sequence={UINT32_MAX,Fertility::kRateDrawLimit,0};
    CHECK(Fertility::growthOpportunities(1,[&]{return sequence.at(draws++);})==1);
    CHECK(draws==3);
    CHECK(Fertility::growthOpportunities(1,[]{return 1u;})==0);
    CHECK(Fertility::growthOpportunities(Fertility::kRateScale+1,[]{return Fertility::kRateScale;})==2);
}

TEST_CASE("rational growth rates preserve exact mass across complete random buckets")
{
    for(const std::uint32_t rate:{0u,1u,2u,Fertility::kScale,Fertility::kRateScale-1,
        Fertility::kRateScale+1,4*Fertility::kRateScale-1,4*Fertility::kRateScale})
    {
        std::uint64_t firstBucket=0,lastBucket=0;
        for(std::uint32_t residue=0;residue<Fertility::kRateScale;++residue)
        {
            firstBucket+=Fertility::growthOpportunities(rate,[=]{return residue;});
            lastBucket+=Fertility::growthOpportunities(rate,[=]{return Fertility::kRateDrawLimit-Fertility::kRateScale+residue;});
        }
        CHECK_EQ(firstBucket,rate);
        CHECK_EQ(lastBucket,rate);
    }
}
}
