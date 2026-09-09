#include "../src/AIMaximaFarming.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdint.h>
#include <vector>

using namespace AIMaxima::Farming;

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

int main()
{
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
			assert(corrected.at(x,y)==expected);
			assert(splatted.at(x,y)==expected);
			assert(adaptive.at(x,y)==expected);
			// Classifiers query neighbors across both map seams. Exercise
			// negative and oversized coordinates against the independent oracle.
			for(int wrapY=-2;wrapY<=2;++wrapY) for(int wrapX=-2;wrapX<=2;++wrapX)
			{
				assert(corrected.at(x+wrapX*w,y+wrapY*h)==expected);
				assert(splatted.at(x+wrapX*w,y+wrapY*h)==expected);
				assert(adaptive.at(x+wrapX*w,y+wrapY*h)==expected);
			}
		}
	}
	for(int amount=1; amount<=5; ++amount) for(int neighbors=0; neighbors<=8; ++neighbors)
	{
		const uint32_t value=65536u*amount*neighbors/64u;
		assert(usefulExpansionCapacity(65536u,amount,neighbors,false)==value);
		assert(usefulExpansionCapacity(65536u,amount,neighbors,true)==value/3u);
	}
	int last=-1; uint32_t threshold=0;
	for(int failures=0; failures<=4; ++failures)
	{
		const int pressure=woodClearPressure(35,80,failures,50,50,2,3,3,4);
		assert(pressure>=last);
		assert(minimumWoodFertility(pressure,15,20)>=threshold);
		last=pressure; threshold=minimumWoodFertility(pressure,15,20);
	}
	assert(openingSpaceConstrained(34,35,0,3,2000,1500));
	assert(openingSpaceConstrained(80,35,3,3,1500,1500));
	assert(!openingSpaceConstrained(80,35,3,3,1501,1500));
	assert(!openingSpaceConstrained(80,35,2,3,100,1500));
	assert(fertilityWithinPercentBand(3277u,5,14));
	assert(fertilityWithinPercentBand(9175u,5,14));
	assert(!fertilityWithinPercentBand(3276u,5,14));
	assert(!fertilityWithinPercentBand(9831u,5,14));
	{
		const int w=5, h=4;
		std::vector<uint8_t> protectedWheat(w*h, 0);
		protectedWheat[0]=1;
		assert(hasAdjacentProtectedWheat(protectedWheat,w,h,1,0));
		assert(hasAdjacentProtectedWheat(protectedWheat,w,h,4,3));
		assert(!hasAdjacentProtectedWheat(protectedWheat,w,h,2,2));
		assert(!hasAdjacentProtectedWheat(protectedWheat,w,h,0,0));
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
		assert(open.preservedResourceTiles==2);
		assert(open.fallbackEntranceTile<0);
		assert(std::find(open.tiles.begin(),open.tiles.end(),w+2)==open.tiles.end());
		assert(std::find(open.tiles.begin(),open.tiles.end(),w+3)==open.tiles.end());

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
		assert(sealed.preservedResourceTiles==int(circulation.size()));
		assert(sealed.fallbackEntranceTile==5*w+3);
		assert(sealed.tiles.size()==1);
		assert(sealed.tiles[0]==5*w+3);
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
				assert(std::find(result.tiles.begin(),result.tiles.end(),index(x,5))!=result.tiles.end());
			assert(result.fallbackEntranceTile==index(7,5));
			// Once cleared, preserve the entire route; do not cut other resources.
			for(int x=7;x<=10;++x){preserved[index(x,5)]=0;reachable[index(x,5)]=1;}
			result=selectResourcePreservingCirculation(w,h,footprint,lane,preserved,burden,reachable);
			assert(result.fallbackEntranceTile<0);
			// A missing lane tile is a hard obstruction: never cross it.
			for(int x=7;x<=10;++x){preserved[index(x,5)]=1;reachable[index(x,5)]=0;}
			lane.erase(std::find(lane.begin(),lane.end(),index(9,5)));
			result=selectResourcePreservingCirculation(w,h,footprint,lane,preserved,burden,reachable);
			assert(result.fallbackEntranceTile<0);
			assert(result.tiles.size()==1);
		}
	}

	{
		const int w=20, h=20;
		std::vector<std::vector<int> > previous;
		previous.push_back(std::vector<int>(1, 2*w+2));
		previous.push_back(std::vector<int>(1, 15*w+15));
		assert(gatePairRelocationDistance(previous,
			previous[0], previous[1], w, h)==0);
		assert(gatePairRelocationDistance(previous,
			previous[1], previous[0], w, h)==0);
		assert(gatePairRelocationDistance(previous,
			std::vector<int>(1, 3*w+3), std::vector<int>(1, 15*w+17), w, h)==3);
		assert(gateRelocationDistance(previous,
			std::vector<int>(1, 3*w+3), w, h)==1);
		assert(gateRelocationDistance(previous,
			std::vector<int>(1, 15*w+17), w, h)==2);
		std::vector<std::vector<int> > wrapped;
		wrapped.push_back(std::vector<int>(1, 0));
		wrapped.push_back(std::vector<int>(1, 10*w+10));
		assert(gatePairRelocationDistance(wrapped,
			std::vector<int>(1, 19*w+19), wrapped[1], w, h)==1);
	}
	{
		// A continuous protected ring around an island interior becomes porous.
		const int w=9, h=9;
		std::vector<uint8_t> land(w*h, 0), shore(w*h, 0), interior(w*h, 0);
		std::vector<uint8_t> coastalFarm(w*h, 0), protectedTiles(w*h, 0);
		std::vector<uint8_t> protectedWheat(w*h, 0);
		for(int y=2; y<=6; ++y) for(int x=2; x<=6; ++x)
		{
			land[y*w+x]=1;
			shore[y*w+x]=(x==2 || x==6 || y==2 || y==6);
		}
		interior[4*w+4]=1;
		for(int y=3; y<=5; ++y) for(int x=3; x<=5; ++x)
			if(x==3 || x==5 || y==3 || y==5)
				coastalFarm[y*w+x]=protectedTiles[y*w+x]
					=protectedWheat[y*w+x]=1;
		CoastalBarrierPorosityResult porous=
			makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
				coastalFarm,coastalFarm,protectedTiles,protectedWheat);
		assert(porous.sealedComponents==1);
		assert(porous.restoredComponents==1);
		assert(porous.openedTiles>0);
		assert(std::count(protectedTiles.begin(),protectedTiles.end(),uint8_t(1))>0);
		// The simple ring exposes the requested alternating coastal contour.
		assert(protectedTiles[3*w+3]);
		assert(!protectedTiles[3*w+4]);
		assert(protectedTiles[3*w+5]);

		// An existing deliberate gate reaches the interior, so the rest of the
		// coastal barrier must stay exactly as it was.
		protectedTiles=coastalFarm;
		protectedWheat=coastalFarm;
		protectedTiles[3*w+4]=protectedWheat[3*w+4]=0;
		const std::vector<uint8_t> before=protectedTiles;
		CoastalBarrierPorosityResult preserved=
			makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
				coastalFarm,coastalFarm,protectedTiles,protectedWheat);
		assert(preserved.sealedComponents==0);
		assert(preserved.restoredComponents==0);
		assert(preserved.openedTiles==0);
		assert(protectedTiles==before);
	}
	{
		// A narrow irregular neck can contain no useful lattice opening. The
		// residual repair opens the minimum one-tile cardinal coastal channel.
		const int w=7, h=5;
		std::vector<uint8_t> land(w*h,0), shore(w*h,0), interior(w*h,0);
		std::vector<uint8_t> coastalFarm(w*h,0), protectedTiles(w*h,0);
		std::vector<uint8_t> protectedWheat(w*h,0);
		land[2*w+2]=land[2*w+3]=land[2*w+4]=1;
		shore[2*w+2]=1;
		interior[2*w+4]=1;
		coastalFarm[2*w+3]=protectedTiles[2*w+3]
			=protectedWheat[2*w+3]=1;
		CoastalBarrierPorosityResult repaired=
			makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
				coastalFarm,coastalFarm,protectedTiles,protectedWheat);
		assert(repaired.sealedComponents==1);
		assert(repaired.restoredComponents==1);
		assert(repaired.openedTiles==1);
		assert(!protectedTiles[2*w+3]);
	}
	{
		// A diagonal contact is not a gate: both cardinal approaches to the
		// interior are protected, so the verifier must open one of them.
		const int w=5, h=5;
		std::vector<uint8_t> land(w*h,0), shore(w*h,0), interior(w*h,0);
		std::vector<uint8_t> coastalFarm(w*h,0), protectedTiles(w*h,0);
		std::vector<uint8_t> protectedWheat(w*h,0);
		land[w+1]=land[w+2]=land[2*w+1]=land[2*w+2]=1;
		shore[w+1]=1;
		interior[2*w+2]=1;
		coastalFarm[w+2]=coastalFarm[2*w+1]=1;
		protectedTiles=protectedWheat=coastalFarm;
		CoastalBarrierPorosityResult diagonal=
			makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
				coastalFarm,coastalFarm,protectedTiles,protectedWheat);
		assert(diagonal.sealedComponents==1);
		assert(diagonal.restoredComponents==1);
		assert(diagonal.openedTiles>=1);
		assert(diagonal.openedTiles<=2);
		assert(!protectedTiles[w+2] || !protectedTiles[2*w+1]);
	}
	{
		// A thick strategic envelope receives only its cheapest access channel,
		// rather than a periodic grid cut through unrelated farm cells.
		const int w=11, h=11;
		std::vector<uint8_t> land(w*h,0), shore(w*h,0), interior(w*h,0);
		std::vector<uint8_t> coastalFarm(w*h,0), strategicEnvelope(w*h,0);
		std::vector<uint8_t> protectedTiles(w*h,0), protectedWheat(w*h,0);
		for(int y=1; y<=9; ++y) for(int x=1; x<=9; ++x)
		{
			land[y*w+x]=1;
			shore[y*w+x]=(x==1 || x==9 || y==1 || y==9);
		}
		interior[5*w+5]=1;
		for(int y=2; y<=8; ++y) for(int x=2; x<=8; ++x)
			if(x==2 || x==8 || y==2 || y==8)
				coastalFarm[y*w+x]=strategicEnvelope[y*w+x]
					=protectedTiles[y*w+x]=protectedWheat[y*w+x]=1;
		CoastalBarrierPorosityResult thick=
			makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
				coastalFarm,strategicEnvelope,protectedTiles,protectedWheat);
		assert(thick.sealedComponents==1);
		assert(thick.restoredComponents==1);
		assert(thick.openedTiles>=10);
		assert(thick.openedTiles<=13);
		assert(std::count(protectedTiles.begin(),protectedTiles.end(),uint8_t(1))
			>0);
	}

	{
		// Two independent pockets require residual channels: neither wall is
		// on the checkerboard opening phase. Repairing one cannot hide the other.
		const int w=9,h=7;
		std::vector<uint8_t> land(w*h,0),shore(w*h,0),interior(w*h,0),coast(w*h,0);
		for(int y:{2,4}) {
			for(int x=3;x<=5;++x)land[y*w+x]=1;
			shore[y*w+3]=interior[y*w+3]=1;
			interior[y*w+5]=1;coast[y*w+4]=1;
		}
		land[3*w+3]=1;
		auto protection=coast,wheat=coast;
		auto result=makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
			coast,coast,protection,wheat);
		assert(result.sealedComponents==2 && result.restoredComponents==2);
		assert(result.openedTiles==2 && !protection[2*w+4] && !protection[4*w+4]);
	}

	for(int offset:{0,19})
	{
		// Two trapped farms and one open farm share a single landmass. An
		// accessible interior marker elsewhere must not hide either pocket.
		const int w=24,h=12;
		auto tile=[&](int x,int y){return y*w+(x+offset)%w;};
		std::vector<uint8_t> land(w*h,1),shore(w*h,0),interior(w*h,0);
		std::vector<uint8_t> coast(w*h,0),protection(w*h,0),wheat(w*h,0);
		for(int x=0;x<w;++x)shore[x]=1;
		interior[tile(1,1)]=1;
		for(int center:{4,10,16})
		{
			interior[tile(center,6)]=1;
			for(int y=5;y<=7;++y)for(int x=center-1;x<=center+1;++x)
				if(y!=6 || x!=center)coast[tile(x,y)]=1;
		}
		protection=wheat=coast;
		protection[tile(16,5)]=wheat[tile(16,5)]=0;
		const auto before=protection;
		auto result=makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
			coast,coast,protection,wheat);
		assert(result.sealedComponents==2 && result.restoredComponents==2);
		for(int y=5;y<=7;++y)for(int x=15;x<=17;++x)
			assert(protection[tile(x,y)]==before[tile(x,y)]);
		// Independently flood from the exterior and require both targets.
		std::vector<int> queue;
		std::vector<uint8_t> reached(w*h,0);
		for(int x=0;x<w;++x){queue.push_back(x);reached[x]=1;}
		for(size_t head=0;head<queue.size();++head)
			for(int d:{-w,-1,1,w})
			{
				int x=queue[head]%w,y=queue[head]/w;
				if(d==-1)--x;else if(d==1)++x;else y+=d/w;
				int next=((y+h)%h)*w+(x+w)%w;
				if(!protection[next]&&!reached[next]){reached[next]=1;queue.push_back(next);}
			}
		assert(reached[tile(4,6)] && reached[tile(10,6)]);
		assert(protection==wheat);
		auto again=makeSealedCoastalFarmBarriersPorous(w,h,land,shore,interior,
			coast,coast,protection,wheat);
		assert(again.sealedComponents==0 && again.openedTiles==0);
	}


    // The protected boundary checkerboard contains every interior seed, while
    // the passive coastal openings avoid those seeds on both map seams.
    for(int y=0;y<16;++y)for(int x=0;x<16;++x)
    {
        if((x&1) && (y&1))
        {
            assert(isInteriorSeed(x,y));
            assert(isExpansionCell(x,y));
            assert(!isCoastalOpeningCell(x,y));
        }
        assert(isExpansionCell(x,y)==isExpansionCell((x+16)%16,(y+16)%16));
    }
    {
        // One changed neighbor used to reverse every opening on this contour.
        const int w=10,h=6;
        std::vector<uint8_t> land(w*h),shore(w*h),interior(w*h),coast(w*h);
        for(int y=1;y<=4;++y)for(int x=2;x<=6;++x)
        {
            land[y*w+x]=1;shore[y*w+x]=x==2;
            interior[y*w+x]=x==6;coast[y*w+x]=x==4;
        }
        auto evaluate=[&]()
        {
            auto protectedTiles=coast,protectedWheat=coast;
            auto result=makeSealedCoastalFarmBarriersPorous(w,h,land,shore,
                interior,coast,coast,protectedTiles,protectedWheat);
            assert(result.restoredComponents==1);
            assert(protectedTiles==protectedWheat);
            return protectedTiles;
        };
        const auto before=evaluate();
        land[2*w+5]=0;
        assert(evaluate()==before);
        land[2*w+5]=1;
        assert(evaluate()==before);
        for(int y=1;y<=4;++y)assert(bool(before[y*w+4])==(y%2==0));
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
	benchmark.rebuild(size,size,water,sand,AdaptiveFertilityPath);
	const long long microseconds=std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now()-begin).count();
	std::cout << "fertility_512_us=" << microseconds << '\n';
	assert(microseconds<100000);
	return 0;
}
