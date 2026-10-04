#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Glob2Test.h"
#include <cmath>
#include <cstdlib>
#include "../../src/ai/maxima/AIMaximaFarming.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdint.h>
#include <vector>

namespace
{
using namespace AIMaxima::Farming;

static double processCpuMicroseconds()
{
#ifdef _WIN32
	// The Windows CRT clock() reports elapsed time, not process CPU time.
	FILETIME creation, exit, kernel, user;
	if(!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
		return -1;
	const uint64_t kernelTicks=(uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime;
	const uint64_t userTicks=(uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime;
	return double(kernelTicks+userTicks)/10.0;
#else
	const std::clock_t ticks=std::clock();
	return ticks==std::clock_t(-1) ? -1 : 1000000.0*double(ticks)/CLOCKS_PER_SEC;
#endif
}

static uint32_t direct(int w, int h, const std::vector<uint8_t>& water,
	const std::vector<uint8_t>& sand, int x, int y)
{
	uint32_t total=0;
	for(int dy=-15; dy<=15; ++dy)
		for(int dx=-15; dx<=15; ++dx)
		{
			const int wx=(x+dx%w+w)%w, wy=(y+dy%h+h)%h;
			const int sx=(x-dx%w+w)%w, sy=(y-dy%h+h)%h;
			if(water[wy*w+wx] && !sand[sy*w+sx])
				total+=uint32_t(16-std::abs(dx))*uint32_t(16-std::abs(dy));
		}
	return total;
}
}

TEST_SUITE("Maxima.Farming")
{
	TEST_CASE("corridor protection and fertility caches")
	{
		// The center cannot close a corridor, including a diagonal corridor.
		REQUIRE(canProtectWithoutSplittingAccess(0x1ff));
		REQUIRE(!canProtectWithoutSplittingAccess((1<<3)|(1<<4)|(1<<5)));
		REQUIRE(!canProtectWithoutSplittingAccess((1<<0)|(1<<4)|(1<<8)));
		REQUIRE(!canProtectWithoutSplittingAccess(1<<4));
		REQUIRE(canProtectWithoutSplittingAccess((1<<0)|(1<<1)|(1<<2)|(1<<4)));

		{
			const int w=8,h=6;
			std::vector<uint8_t> water(w*h,1),sand(w*h,0);
			ExactFertilityCache cache;
			cache.rebuild(w,h,water,sand,SandCorrectionFertilityPath);
			for(auto value:cache.values())REQUIRE(value==65536u);
			std::fill(sand.begin(),sand.end(),1);
			cache.rebuild(w,h,water,sand,WaterSplatFertilityPath);
			for(auto value:cache.values())REQUIRE(value==0);
		}
		uint32_t random=0x5eed1234u;
		for(int sample=0; sample<32; ++sample)
		{
			const int w=7+sample%9, h=8+(sample*5)%9;
			std::vector<uint8_t> water(w*h), sand(w*h);
			for(int i=0; i<w*h; ++i)
			{
				random=random*1664525u+1013904223u; water[i]=(random>>29)==0;
				random=random*1664525u+1013904223u; sand[i]=(random>>30)==0;
			}
			ExactFertilityCache corrected, splatted, adaptive;
			corrected.rebuild(w,h,water,sand,SandCorrectionFertilityPath);
			splatted.rebuild(w,h,water,sand,WaterSplatFertilityPath);
			adaptive.rebuild(w,h,water,sand,AdaptiveFertilityPath);
			for(int y=0; y<h; ++y) for(int x=0; x<w; ++x)
			{
				const uint32_t expected=direct(w,h,water,sand,x,y);
				REQUIRE(corrected.at(x,y)==expected);
				REQUIRE(splatted.at(x,y)==expected);
				REQUIRE(adaptive.at(x,y)==expected);
				// Classifiers query neighbors across both map seams. Exercise
				// negative and oversized coordinates against the independent oracle.
				for(int wrapY=-2;wrapY<=2;++wrapY) for(int wrapX=-2;wrapX<=2;++wrapX)
				{
					REQUIRE(corrected.at(x+wrapX*w,y+wrapY*h)==expected);
					REQUIRE(splatted.at(x+wrapX*w,y+wrapY*h)==expected);
					REQUIRE(adaptive.at(x+wrapX*w,y+wrapY*h)==expected);
				}
			}
		}
		for(int amount=1; amount<=5; ++amount) for(int neighbors=0; neighbors<=8; ++neighbors)
		{
			const uint32_t value=65536u*amount*neighbors/64u;
			REQUIRE(usefulExpansionCapacity(65536u,amount,neighbors,false)==value);
			REQUIRE(usefulExpansionCapacity(65536u,amount,neighbors,true)==value/3u);
		}
		int last=-1; uint32_t threshold=0;
		for(int failures=0; failures<=4; ++failures)
		{
			const int pressure=woodClearPressure(35,80,failures,50,50,2,3,3,4);
			REQUIRE(pressure>=last);
			REQUIRE(minimumWoodFertility(pressure,15,20)>=threshold);
			last=pressure; threshold=minimumWoodFertility(pressure,15,20);
		}
		REQUIRE(openingSpaceConstrained(34,35,0,3,2000,1500));
		REQUIRE(openingSpaceConstrained(80,35,3,3,1500,1500));
		REQUIRE(!openingSpaceConstrained(80,35,3,3,1501,1500));
		REQUIRE(!openingSpaceConstrained(80,35,2,3,100,1500));
		REQUIRE(fertilityWithinPercentBand(3277u,5,14));
		REQUIRE(fertilityWithinPercentBand(9175u,5,14));
		REQUIRE(!fertilityWithinPercentBand(3276u,5,14));
		REQUIRE(!fertilityWithinPercentBand(9831u,5,14));
		{
			const int w=5, h=4;
			std::vector<uint8_t> protectedWheat(w*h, 0);
			protectedWheat[0]=1;
			REQUIRE(hasAdjacentProtectedWheat(protectedWheat,w,h,1,0));
			REQUIRE(hasAdjacentProtectedWheat(protectedWheat,w,h,4,3));
			REQUIRE(!hasAdjacentProtectedWheat(protectedWheat,w,h,2,2));
			REQUIRE(!hasAdjacentProtectedWheat(protectedWheat,w,h,0,0));
		}
		{
			// Wheat and wood remain harvestable in a parcel ring when any ordinary
			// entrance is open.
			const int w=7, h=7;
			std::vector<int> footprint;
			for(int y=2; y<=4; ++y) for(int x=2; x<=4; ++x)
				footprint.push_back(y*w+x);
			std::vector<int> circulation;
			for(int x=2; x<=4; ++x)
			{
				circulation.push_back(w+x);
				circulation.push_back(5*w+x);
			}
			for(int y=2; y<=4; ++y)
			{
				circulation.push_back(y*w+1);
				circulation.push_back(y*w+5);
			}
			std::vector<uint8_t> preserved(w*h, 0);
			std::vector<int> burden(w*h, 0);
			std::vector<uint8_t> reachable(w*h,1);
			for(int index:footprint)reachable[index]=0;
			for(int index:circulation)reachable[index]=0;
			preserved[w+2]=1; burden[w+2]=7;
			preserved[w+3]=1; burden[w+3]=3;
			ReservationClearingSelection open=
				selectResourcePreservingCirculation(w,h,footprint,circulation,
					preserved,burden,reachable);
			REQUIRE(open.preservedResourceTiles==2);
			REQUIRE(open.fallbackEntranceTile<0);
			REQUIRE(std::find(open.tiles.begin(),open.tiles.end(),w+2)==open.tiles.end());
			REQUIRE(std::find(open.tiles.begin(),open.tiles.end(),w+3)==open.tiles.end());

			// When resources seal the full boundary, only the lowest-burden tile is
			// admitted to the clearing contract. Ties resolve deterministically.
			for(size_t i=0; i<circulation.size(); ++i)
			{
				preserved[circulation[i]]=1;
				burden[circulation[i]]=5;
			}
			burden[5*w+3]=1;
			ReservationClearingSelection sealed=
				selectResourcePreservingCirculation(w,h,footprint,circulation,
					preserved,burden,reachable);
			REQUIRE(sealed.preservedResourceTiles==int(circulation.size()));
			REQUIRE(sealed.fallbackEntranceTile==5*w+3);
			REQUIRE(sealed.tiles.size()==1);
			REQUIRE(sealed.tiles[0]==5*w+3);
		}
		{
			// A locally empty entrance behind two resource layers must not be
			// mistaken for a route. Repeat with wrapping and a blocked cheap branch.
			for(int shift:{0,12})
			{
				const int w=16,h=16;
				auto index=[&](int x,int y){return ((y+shift)%h)*w+(x+shift)%w;};
				std::vector<int> footprint={index(5,5),index(6,5),index(5,6),index(6,6)};
				std::vector<int> lane;
				std::vector<uint8_t> preserved(w*h,0),reachable(w*h,0);
				std::vector<int> burden(w*h,1);
				// Only the far end is connected to a worker. The cheap empty
				// neighboring pocket is enclosed and must not short-circuit the path.
				for(int x=7;x<=10;++x){lane.push_back(index(x,5));preserved[index(x,5)]=1;}
				lane.push_back(index(5,4));
				reachable[index(11,5)]=1;
				auto result=selectResourcePreservingCirculation(w,h,footprint,lane,preserved,burden,reachable);
				for(int x=7;x<=10;++x)
					REQUIRE(std::find(result.tiles.begin(),result.tiles.end(),index(x,5))!=result.tiles.end());
				REQUIRE(result.fallbackEntranceTile==index(7,5));
				// Once cleared, preserve the entire route; do not cut other resources.
				for(int x=7;x<=10;++x){preserved[index(x,5)]=0;reachable[index(x,5)]=1;}
				result=selectResourcePreservingCirculation(w,h,footprint,lane,preserved,burden,reachable);
				REQUIRE(result.fallbackEntranceTile<0);
				// A missing lane tile is a hard obstruction: never cross it.
				for(int x=7;x<=10;++x){preserved[index(x,5)]=1;reachable[index(x,5)]=0;}
				lane.erase(std::find(lane.begin(),lane.end(),index(9,5)));
				result=selectResourcePreservingCirculation(w,h,footprint,lane,preserved,burden,reachable);
				REQUIRE(result.fallbackEntranceTile<0);
				REQUIRE(result.tiles.size()==1);
			}
		}

	    // The protected boundary checkerboard contains every interior seed, so
	    // expansion transitions never release one, on both map seams.
	    for(int y=0;y<16;++y)for(int x=0;x<16;++x)
	    {
	        if((x&1) && (y&1))
	        {
	            REQUIRE(isInteriorSeed(x,y));
	            REQUIRE(isExpansionCell(x,y));
	        }
	        REQUIRE(isExpansionCell(x,y)==isExpansionCell((x+16)%16,(y+16)%16));
	    }
		const int size=512;
		std::vector<uint8_t> water(size*size), sand(size*size);
		for(int i=0; i<size*size; ++i)
		{
			random=random*1664525u+1013904223u; water[i]=(random%100)<35;
			random=random*1664525u+1013904223u; sand[i]=(random%100)<4;
		}
		ExactFertilityCache benchmark;
		const std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now();
		const double cpuBegin=processCpuMicroseconds();
		REQUIRE(cpuBegin>=0);
		benchmark.rebuild(size,size,water,sand,AdaptiveFertilityPath);
		const double cpuEnd=processCpuMicroseconds();
		REQUIRE(cpuEnd>=cpuBegin);
		const long long microseconds=std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now()-begin).count();
		const double cpuMicroseconds=cpuEnd-cpuBegin;
		std::cout << "fertility_512_us=" << microseconds
			<< " fertility_512_cpu_us=" << cpuMicroseconds << '\n';
		// Keep the same work budget; elapsed time also counts unrelated builds
		// and other processes that prevent this test from getting CPU time.
		REQUIRE(cpuMicroseconds>=0);
		// The release suite enforces the production CPU budget. O0 source
		// coverage adds counters to every branch, so it records the same work
		// and verifies its results without comparing instrumented CPU cost.
#ifndef GLOB2_TEST_COVERAGE
		REQUIRE(cpuMicroseconds<100000);
#endif
	}
}
