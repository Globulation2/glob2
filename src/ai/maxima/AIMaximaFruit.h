#pragma once

#include <array>
#include <vector>

namespace AIMaximaFruit
{
// A decision-local observation: no RNG, persisted cache or scheduling state.
// Rebuild when collecting supply so new swimming routes and lost vision apply.
struct Tile
{
	bool passable=false;
	bool visible=false;
	bool buildingVision=false;
	int variety=-1;
};

struct Supply
{
	int varietyCount=0;
	unsigned available=0;
	unsigned collectable=0;
	unsigned covered=0;
	std::array<int,3> distance{{-1,-1,-1}};
	std::array<int,3> source{{-1,-1,-1}};
};

struct Field
{
	int width=0, height=0;
	std::vector<Tile> tiles;
	std::array<std::vector<int>,3> distances, sources;

	int index(int x,int y) const
	{
		return ((y%height+height)%height)*width+(x%width+width)%width;
	}

	void build()
	{
		for(int variety=0;variety<3;++variety)
		{
			distances[variety].assign(tiles.size(),-1);
			sources[variety].assign(tiles.size(),-1);
			std::vector<int> queue;
			// Row-major sources and neighbour order make equal routes stable.
			for(int at=0;at<int(tiles.size());++at)
			{
				if(tiles[at].variety!=variety)continue;
				distances[variety][at]=0;
				sources[variety][at]=at;
				queue.push_back(at);
			}
			for(std::size_t head=0;head<queue.size();++head)
			{
				const int at=queue[head], x=at%width, y=at/width;
				for(int dy=-1;dy<=1;++dy)
					for(int dx=-1;dx<=1;++dx)
					{
						if(!dx && !dy)continue;
						const int next=index(x+dx,y+dy);
						if(!tiles[next].passable || distances[variety][next]>=0)continue;
						distances[variety][next]=distances[variety][at]+1;
						sources[variety][next]=sources[variety][at];
						queue.push_back(next);
					}
			}
		}
	}

	Supply assessBuilding(int x,int y,int w,int h) const
	{
		Supply result;
		if(tiles.empty())return result;
		// Workers approach an inn's perimeter; its footprint blocks transit.
		for(int dy=-1;dy<=h;++dy)
			for(int dx=-1;dx<=w;++dx)
			{
				if(dx>=0 && dx<w && dy>=0 && dy<h)continue;
				const int at=index(x+dx,y+dy);
				for(int variety=0;variety<3;++variety)
				{
					const int distance=distances[variety][at];
					if(distance<0 || (result.distance[variety]>=0
						&& distance>=result.distance[variety]))continue;
					result.distance[variety]=distance;
					result.source[variety]=sources[variety][at];
				}
			}
		for(int variety=0;variety<3;++variety)
		{
			if(result.source[variety]<0)continue;
			const Tile& tile=tiles[result.source[variety]];
			++result.varietyCount;
			result.available|=1u<<variety;
			if(tile.visible)result.collectable|=1u<<variety;
			if(tile.buildingVision)result.covered|=1u<<variety;
		}
		return result;
	}
};
}
