// SPDX-License-Identifier: GPL-3.0-or-later
#include "FertilityField.h"
#include "Map.h"
#include "TerrainProperties.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <utility>

namespace
{
int wrap(int value, int size) { const int r = value % size; return r < 0 ? r + size : r; }

// Four running-box passes produce the triangular kernel in O(width*height).
// Signed 64-bit intermediates cover the largest map and every allowed Q8 value.
// Each line starts with a short fixed-size sum; no per-cell division/modulo is
// needed in the inner loops, including maps smaller than the kernel radius.
template<class T>
std::vector<std::int64_t> triangular(const std::vector<T>& input, int width, int height, int radius)
{
	const int box = radius + 1;
	const std::size_t size = std::size_t(width) * height;
	std::vector<std::int64_t> a(input.begin(), input.end()), b(size);
	for (int axis = 0; axis < 2; ++axis)
		for (int pass = 0; pass < 2; ++pass)
		{
			const int length = axis ? height : width, lines = axis ? width : height;
			const int stride = axis ? width : 1;
			const int begin = pass ? -radius : 0;
			for (int line = 0; line < lines; ++line)
			{
				const std::size_t base = axis ? line : std::size_t(line) * width;
				std::int64_t sum = 0;
				for (int d = 0; d < box; ++d) sum += a[base + wrap(begin+d, length)*stride];
				int remove = wrap(begin, length), add = wrap(begin+box, length);
				for (int p = 0; p < length; ++p)
				{
					b[base + p*stride] = sum;
					sum += a[base + add*stride] - a[base + remove*stride];
					if (++add == length) add = 0;
					if (++remove == length) remove = 0;
				}
			}
			a.swap(b);
		}
	return a;
}
// Only rebuilds need wrapped indexes. Runtime growth reads the cached fields.
// Algae's rotated doubled offset reaches +/-45 relative to a donor/shore cell.
struct WrappedIndexes
{
    int width,height;
    std::vector<int> xs,ys;
    WrappedIndexes(int w,int h):width(w),height(h),xs(91*w),ys(91*h)
    {
        for(int d=-45;d<=45;++d)
        {
            const int bx=d%w,by=d%h;
            for(int x=0;x<w;++x) xs[(d+45)*w+x]=(x+bx+w)%w;
            for(int y=0;y<h;++y) ys[(d+45)*h+y]=(y+by+h)%h;
        }
    }
    int x(int position,int delta) const { return xs[(delta+45)*width+position]; }
    int y(int position,int delta) const { return ys[(delta+45)*height+position]; }
};
constexpr int offsetWeight(int offset) { return 16-(offset<0 ? -offset : offset); }

std::uint32_t q16(std::int64_t value)
{
	return static_cast<std::uint32_t>(std::clamp<std::int64_t>(value, 0, Fertility::kScale));
}
}

const Fertility::GrowthCache& Map::resourceGrowthField() const
{
	std::lock_guard<std::mutex> lock(growthCacheMutex);
	if (!growthCache.validFor(*this)) growthCache.rebuild(*this);
	return growthCache;
}

namespace Fertility
{
void Field::rebuild(int w, int h, const std::vector<std::uint8_t>& water,
	const std::vector<std::uint8_t>& sand, Path path)
{
	std::vector<std::int16_t> contributions(water.size());
	std::vector<std::uint16_t> inhibition(sand.size());
	for (std::size_t i=0; i<water.size(); ++i) contributions[i] = water[i] ? 256 : 0;
	for (std::size_t i=0; i<sand.size(); ++i) inhibition[i] = sand[i] ? 256 : 0;
	rebuildWeighted(w, h, contributions, inhibition, path);
}

void Field::rebuildWeighted(int w, int h, const std::vector<std::int16_t>& contributionQ8,
    const std::vector<std::uint16_t>& inhibitionQ8,Path path)
{
    assert(w>0 && h>0 && contributionQ8.size()==std::size_t(w)*h && inhibitionQ8.size()==contributionQ8.size());
    width=w;height=h;
    waterTiles=std::count_if(contributionQ8.begin(),contributionQ8.end(),[](auto v){return v!=0;});
    sandTiles=std::count_if(inhibitionQ8.begin(),inhibitionQ8.end(),[](auto v){return v!=0;});
    const auto size=contributionQ8.size();
    usedPath=path==Path::Adaptive
        ? (4*std::uint64_t(size)+961*std::uint64_t(sandTiles)<=961*std::uint64_t(waterTiles)
            ? Path::SandCorrection : Path::WaterSplat) : path;
    std::vector<std::int64_t> totals(size,0);
    const WrappedIndexes wrapped(w,h);
    if(usedPath==Path::SandCorrection)
    {
        totals=triangular(contributionQ8,w,h,15);
        for(auto& value:totals)value*=256;
        // A donor and its opposite inhibitor act on the same weighted offset.
        // Iterate only inhibitors; no grid-sized 961-tap scan is needed.
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int inhibition=std::min<int>(inhibitionQ8[sy*w+sx],256);
            if(!inhibition)continue;
            int targetX[31],donorX[31];
            for(int dx=-15;dx<=15;++dx)
            {targetX[dx+15]=wrapped.x(sx,dx);donorX[dx+15]=wrapped.x(sx,2*dx);}
            for(int dy=-15;dy<=15;++dy)
            {
                auto* target=&totals[wrapped.y(sy,dy)*w];
                const auto* donor=&contributionQ8[wrapped.y(sy,2*dy)*w];
                const std::int64_t scale=inhibition*offsetWeight(dy);
                for(int dx=-15;dx<=15;++dx)
                    target[targetX[dx+15]]-=scale*offsetWeight(dx)*donor[donorX[dx+15]];
            }
        }
    }
    else
    {
        // Sparse donor alternative; signed contributions remain signed until
        // every pair is accumulated, so nearby deficits cancel bonuses exactly.
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int donor=contributionQ8[sy*w+sx];
            if(!donor)continue;
            int targetX[31],inhibitorX[31];
            for(int dx=-15;dx<=15;++dx)
            {targetX[dx+15]=wrapped.x(sx,-dx);inhibitorX[dx+15]=wrapped.x(sx,-2*dx);}
            for(int dy=-15;dy<=15;++dy)
            {
                auto* target=&totals[wrapped.y(sy,-dy)*w];
                const auto* inhibitors=&inhibitionQ8[wrapped.y(sy,-2*dy)*w];
                const std::int64_t scale=donor*offsetWeight(dy);
                for(int dx=-15;dx<=15;++dx)
                    target[targetX[dx+15]]+=scale*offsetWeight(dx)*(256-std::min<int>(inhibitors[inhibitorX[dx+15]],256));
            }
        }
    }
    fertility.resize(size);
    for(std::size_t i=0;i<size;++i)fertility[i]=q16(totals[i]/(256*256));
}

std::vector<std::uint32_t> shoreGrowthField(int w,int h,
    const std::vector<std::int16_t>& contributionQ8,const std::vector<std::uint16_t>& supportQ8)
{
    const auto size=contributionQ8.size();
    assert(w>0 && h>0 && size==std::size_t(w)*h && supportQ8.size()==size);
    const auto donors=std::count_if(contributionQ8.begin(),contributionQ8.end(),[](auto v){return v!=0;});
    const auto shores=std::count_if(supportQ8.begin(),supportQ8.end(),[](auto v){return v!=0;});
    std::vector<std::uint32_t> result(size,0);
    if(!donors || !shores)return result;
    std::vector<std::int64_t> totals(size,0);
    const WrappedIndexes wrapped(w,h);
    if(shores<=donors)
    {
        // Shore at (x+2dy,y+2dx), donor at (x+dx,y+dy). Invert the
        // former relation to stamp only from nonzero support cells.
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int support=supportQ8[sy*w+sx];
            if(!support)continue;
            int targetX[31];
            for(int dy=-15;dy<=15;++dy)targetX[dy+15]=wrapped.x(sx,-2*dy);
            for(int dx=-15;dx<=15;++dx)
            {
                auto* target=&totals[wrapped.y(sy,-2*dx)*w];
                const std::int64_t scale=support*offsetWeight(dx);
                for(int dy=-15;dy<=15;++dy)
                    target[targetX[dy+15]]+=scale*offsetWeight(dy)*
                        contributionQ8[wrapped.y(sy,dy-2*dx)*w+wrapped.x(sx,dx-2*dy)];
            }
        }
    }
    else
    {
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int donor=contributionQ8[sy*w+sx];
            if(!donor)continue;
            int targetX[31];
            for(int dx=-15;dx<=15;++dx)targetX[dx+15]=wrapped.x(sx,-dx);
            for(int dy=-15;dy<=15;++dy)
            {
                auto* target=&totals[wrapped.y(sy,-dy)*w];
                const std::int64_t scale=donor*offsetWeight(dy);
                for(int dx=-15;dx<=15;++dx)
                    target[targetX[dx+15]]+=scale*offsetWeight(dx)*
                        supportQ8[wrapped.y(sy,2*dx-dy)*w+wrapped.x(sx,2*dy-dx)];
            }
        }
    }
    for(std::size_t i=0;i<size;++i)result[i]=q16(totals[i]/(256*256));
    return result;
}

void Field::multiplyLocal(const std::vector<std::uint16_t>& growthQ8)
{
	assert(growthQ8.size()==fertility.size());
	for (std::size_t i=0; i<fertility.size(); ++i)
		fertility[i]=static_cast<std::uint32_t>(std::uint64_t(fertility[i])*growthQ8[i]/256);
}

bool GrowthCache::validFor(const Map& map) const
{
	return land.getW()==map.getW() && land.getH()==map.getH() &&
		generation==map.terrainGeneration() && aquatic.size()==std::size_t(map.getW())*map.getH();
}

void GrowthCache::rebuild(const Map& map)
{
	const int w=map.getW(), h=map.getH();
	const std::size_t size=std::size_t(w)*h;
	std::vector<std::int16_t> contribution(size);
	std::vector<std::uint16_t> inhibition(size), shore(size);
	localGrowth.resize(size);
	growthHabitats.resize(size);
	for (std::size_t i=0; i<size; ++i)
	{
		const auto& p=map.terrainPropertiesAt(i);
		contribution[i]=p.fertilitySource ? p.fertilityQ8 : 0;
		inhibition[i]=p.inhibitionQ8;
		shore[i]=p.shoreSupportQ8;
		localGrowth[i]=p.growthQ8;
		growthHabitats[i]=p.resourcesGrow ? p.allowedResources : 0;
	}
	land.rebuildWeighted(w,h,contribution,inhibition);
	aquatic=shoreGrowthField(w,h,contribution,shore);
	land.multiplyLocal(localGrowth);
	generation=map.terrainGeneration();
}

std::uint32_t GrowthCache::rate(std::size_t index, int resourceType) const
{
	assert(index<localGrowth.size());
	if (resourceType<0 || resourceType>=MAX_RESOURCES ||
		!(growthHabitats[index] & (1u<<resourceType))) return 0;
	std::uint64_t value;
	if (resourceType==WHEAT || resourceType==WOOD)
		value=land.values()[index];
	else if (resourceType==ALGA)
		value=std::uint64_t(aquatic[index])*localGrowth[index]/256;
	else if (resourceType==STONE || resourceType==NO_RES_TYPE)
		return 0;
	else
		value=std::uint64_t(kScale)*localGrowth[index]/256;
	if (resourceType!=WHEAT) value*=3;
	return static_cast<std::uint32_t>(std::min<std::uint64_t>(value,4u*kRateScale));
}

void Field::gate(const std::vector<std::uint8_t>& keep)
{
	assert(keep.size() == fertility.size());
	for (size_t i = 0; i < fertility.size(); ++i)
		if (!keep[i])
			fertility[i] = 0;
}

std::uint32_t Field::at(int x, int y) const
{
	assert(fertility.size() == size_t(width) * height);
	if (x < 0 || x >= width) x = (x % width + width) % width;
	if (y < 0 || y >= height) y = (y % height + height) % height;
	return fertility[y * width + x];
}

namespace
{
	/// 8-connected over crop habitat from every takeable wheat or wood tile.
	std::vector<std::uint8_t> depositReach(const Map& map)
	{
		const int w = map.getW(), h = map.getH();
		std::vector<std::uint8_t> reached(size_t(w) * h, 0);
		std::queue<std::pair<int, int>> frontier;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (map.isResourceTakeable(x, y, WHEAT) || map.isResourceTakeable(x, y, WOOD))
				{
					reached[size_t(y) * w + x] = 1;
					frontier.emplace(x, y);
				}
		while (!frontier.empty())
		{
			const auto [px, py] = frontier.front();
			frontier.pop();
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					const int nx = map.normalizeX(px + dx), ny = map.normalizeY(py + dy);
					std::uint8_t& cell = reached[size_t(ny) * w + nx];
					if (!cell && (map.terrainPropertiesAt(nx, ny).allowedResources & (1u << WHEAT)))
					{
						cell = 1;
						frontier.emplace(nx, ny);
					}
				}
		}
		return reached;
	}
}

Field forMap(const Map& map, bool gateOnReachableDeposits)
{
	const int w=map.getW(), h=map.getH();
	Field field=map.resourceGrowthField().landField();
	if (gateOnReachableDeposits)
	{
		auto keep=depositReach(map);
		for (std::size_t i=0; i<keep.size(); ++i)
			if ((map.terrainPropertiesAt(i).allowedResources & (1u<<WHEAT))==0) keep[i]=0;
		field.gate(keep);
	}
	return field;
}

std::uint32_t usefulExpansionCapacity(std::uint32_t fertility, int amount,
	int availableNeighbors, bool wheat)
{
	amount = std::max(0, std::min(8, amount));
	availableNeighbors = std::max(0, std::min(8, availableNeighbors));
	// amount/8 * neighbours/8, and wheat only clears WHEAT_GROWTH_DIVISOR one time in three.
	const std::uint32_t divisor = wheat ? 192u : 64u;
	return fertility * std::uint32_t(amount) * std::uint32_t(availableNeighbors) / divisor;
}

bool withinPercentBand(std::uint32_t fertility, int minimumPercent, int maximumPercent)
{
	minimumPercent = std::max(0, std::min(100, minimumPercent));
	maximumPercent = std::max(0, std::min(100, maximumPercent));
	if (minimumPercent > maximumPercent)
		std::swap(minimumPercent, maximumPercent);
	const std::uint64_t scaled = std::uint64_t(fertility) * 100u;
	return scaled >= std::uint64_t(minimumPercent) * kScale
		&& scaled <= std::uint64_t(maximumPercent) * kScale;
}

}
