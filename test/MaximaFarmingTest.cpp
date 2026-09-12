#include "MaximaFarmingTest.h"
#include "../src/AIMaximaFarming.h"

#include <algorithm>
#include <stdint.h>
#include <vector>

CPPUNIT_TEST_SUITE_REGISTRATION(MaximaFarmingTest);

namespace
{
	uint32_t directFertility(int w, int h, const std::vector<uint8_t>& water,
		const std::vector<uint8_t>& sand, int x, int y)
	{
		uint32_t total=0;
		for(int dy=-15; dy<=15; ++dy)
			for(int dx=-15; dx<=15; ++dx)
			{
				const int waterX=(x+dx%w+w)%w;
				const int waterY=(y+dy%h+h)%h;
				const int sandX=(x-dx%w+w)%w;
				const int sandY=(y-dy%h+h)%h;
				if(water[waterY*w+waterX] && !sand[sandY*w+sandX])
					total+=uint32_t(16-(dx<0 ? -dx : dx))
						*uint32_t(16-(dy<0 ? -dy : dy));
			}
		return total;
	}
}

void MaximaFarmingTest::testExactFertilityTargetedCases()
{
	using namespace AIMaxima::Farming;
	const int w=8, h=6;
	std::vector<uint8_t> water(w*h, 1), sand(w*h, 0);
	ExactFertilityCache cache;
	cache.rebuild(w, h, water, sand, SandCorrectionFertilityPath);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
			CPPUNIT_ASSERT_EQUAL(uint32_t(65536), cache.at(x, y));
	std::fill(sand.begin(), sand.end(), 1);
	cache.rebuild(w, h, water, sand, WaterSplatFertilityPath);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
			CPPUNIT_ASSERT_EQUAL(uint32_t(0), cache.at(x, y));

	std::fill(water.begin(), water.end(), 0);
	std::fill(sand.begin(), sand.end(), 0);
	water[0]=1;
	sand[h/2*w+w/2]=1;
	cache.rebuild(w, h, water, sand, AdaptiveFertilityPath);
	for(int y=0; y<h; ++y)
		for(int x=0; x<w; ++x)
			CPPUNIT_ASSERT_EQUAL(directFertility(w, h, water, sand, x, y),
				cache.at(x, y));
}

void MaximaFarmingTest::testAdaptivePathsMatchDirectToroidalRule()
{
	using namespace AIMaxima::Farming;
	uint32_t random=0x12345678u;
	for(int sample=0; sample<20; ++sample)
	{
		const int w=7+sample%5;
		const int h=9+(sample*3)%5;
		std::vector<uint8_t> water(w*h, 0), sand(w*h, 0);
		for(int i=0; i<w*h; ++i)
		{
			random=random*1664525u+1013904223u;
			water[i]=(random>>29)==0;
			random=random*1664525u+1013904223u;
			sand[i]=(random>>30)==0;
		}
		ExactFertilityCache corrections, splats, adaptive;
		corrections.rebuild(w, h, water, sand, SandCorrectionFertilityPath);
		splats.rebuild(w, h, water, sand, WaterSplatFertilityPath);
		adaptive.rebuild(w, h, water, sand, AdaptiveFertilityPath);
		for(int y=0; y<h; ++y)
			for(int x=0; x<w; ++x)
			{
				const uint32_t direct=directFertility(w, h, water, sand, x, y);
				CPPUNIT_ASSERT_EQUAL(direct, corrections.at(x, y));
				CPPUNIT_ASSERT_EQUAL(direct, splats.at(x, y));
				CPPUNIT_ASSERT_EQUAL(direct, adaptive.at(x, y));
			}
	}
}

void MaximaFarmingTest::testExpansionCapacity()
{
	using AIMaxima::Farming::usefulExpansionCapacity;
	for(int amount=1; amount<=5; ++amount)
		for(int neighbors=0; neighbors<=8; ++neighbors)
		{
			const uint32_t normal=65536u*amount*neighbors/64u;
			CPPUNIT_ASSERT_EQUAL(normal,
				usefulExpansionCapacity(65536u, amount, neighbors, false));
			CPPUNIT_ASSERT_EQUAL(normal/3u,
				usefulExpansionCapacity(65536u, amount, neighbors, true));
		}
	CPPUNIT_ASSERT_EQUAL(uint32_t(0),
		usefulExpansionCapacity(65536u, 5, 0, false));
}

void MaximaFarmingTest::testWoodPressureIsMonotone()
{
	using namespace AIMaxima::Farming;
	int previous=-1;
	uint32_t previousThreshold=0;
	for(int failures=0; failures<=4; ++failures)
	{
		const int pressure=woodClearPressure(35, 80, failures, 50,
			50, 2, 3, 3, 4);
		const uint32_t threshold=minimumWoodFertility(pressure, 15, 20);
		CPPUNIT_ASSERT(pressure>=previous);
		CPPUNIT_ASSERT(threshold>=previousThreshold);
		previous=pressure;
		previousThreshold=threshold;
	}
	CPPUNIT_ASSERT(woodClearPressure(20, 90, 2, 50, 50, 2, 3, 3, 4)
		>=woodClearPressure(80, 20, 0, 50, 50, 2, 3, 3, 4));
}

void MaximaFarmingTest::testProtectedWheatAdjacencyWraps()
{
	using AIMaxima::Farming::hasAdjacentProtectedWheat;
	const int w=5, h=4;
	std::vector<uint8_t> protectedWheat(w*h, 0);
	protectedWheat[0]=1;
	CPPUNIT_ASSERT(hasAdjacentProtectedWheat(protectedWheat, w, h, 1, 0));
	CPPUNIT_ASSERT(hasAdjacentProtectedWheat(protectedWheat, w, h, 4, 3));
	CPPUNIT_ASSERT(!hasAdjacentProtectedWheat(protectedWheat, w, h, 2, 2));
	// A marked tile is not adjacent to itself; only its surrounding ring clears.
	CPPUNIT_ASSERT(!hasAdjacentProtectedWheat(protectedWheat, w, h, 0, 0));
}
