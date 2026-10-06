// SPDX-License-Identifier: GPL-3.0-or-later
#include "FertilityField.h"
#include "Map.h"
#include "TerrainProperties.h"

#include <algorithm>
#include <array>
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
    std::array<int,91> xs{},ys{};
    WrappedIndexes(int w,int h):width(w),height(h)
    {
        for(int d=-45;d<=45;++d)
        {
            xs[d+45]=wrap(d,w);
            ys[d+45]=wrap(d,h);
        }
    }
    int x(int position,int delta) const
    { const int value=position+xs[delta+45];return value>=width?value-width:value; }
    int y(int position,int delta) const
    { const int value=position+ys[delta+45];return value>=height?value-height:value; }
};
constexpr int offsetWeight(int offset) { return 16-(offset<0 ? -offset : offset); }
constexpr auto offsetWeights=[] {
    std::array<int,31> result{};
    for(int i=0;i<31;++i)result[i]=offsetWeight(i-15);
    return result;
}();

// Split a 31-tap stamp only where its destination crosses the torus seam.
// Within a span every write has a fixed stride and a distinct destination;
// this remains true on one-cell and other narrower-than-kernel maps.
template<int Step,class Apply> void wrappedSpans(int width,int start,Apply&& apply)
{
    for(int offset=0;offset<31;)
    {
        const int count=std::min(31-offset,(width-1-start)/Step+1);
        apply(start,offset,count);
        offset+=count;
        start+=Step*count;
        if(start>=width)start-=width;
        if constexpr(Step==2)if(start>=width)start-=width;
    }
}

template<class T> class PaddedRows
{
    int stride;
    std::vector<T> values;
public:
    PaddedRows(const std::vector<T>& source,int w,int h):stride(w+60),values(std::size_t(h)*stride)
    {
        for(int y=0;y<h;++y)
        {
            auto* target=values.data()+std::size_t(y)*stride;
            const auto* row=source.data()+std::size_t(y)*w;
            int x=wrap(-30,w);
            for(int i=0;i<stride;++i)
            {target[i]=row[x];if(++x==w)x=0;}
        }
    }
    const T* at(int row,int start) const
    {return values.data()+std::size_t(row)*stride+(start+30);}
};

// Aquatic offsets follow diagonals (x-2d,y+d) or (x+d,y-2d).
// Lay those diagonals out as contiguous lines. Padding keeps logical coordinates
// across the torus seam, including maps whose width and height differ: wrapping
// one coordinate must not reset the other coordinate's diagonal phase.
template<class T> class DiagonalSamples
{
    int stride;
    std::vector<T> values;
public:
    DiagonalSamples(const std::vector<T>& source,int w,int h,bool alongX)
        :stride((alongX?w:h)+90),values(std::size_t(alongX?h:w)*stride)
    {
        const int length=alongX?w:h,lines=alongX?h:w,step=2%lines;
        for(int line=0;line<lines;++line)
        {
            int forward=wrap(-45,length),other=wrap(line+90,lines);
            auto* target=values.data()+std::size_t(line)*stride;
            for(int i=0;i<stride;++i)
            {
                target[i]=alongX?source[other*w+forward]:source[forward*w+other];
                if(++forward==length)forward=0;
                other-=step;
                if(other<0)other+=lines;
            }
        }
    }
    const T* at(int line,int start) const
    { return values.data()+std::size_t(line)*stride+(start+45); }
};

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
    if(!waterTiles)
    {
        fertility.assign(size,0);
        return;
    }
    std::vector<std::int64_t> totals;
    const WrappedIndexes wrapped(w,h);
    if(usedPath==Path::SandCorrection)
    {
        totals=triangular(contributionQ8,w,h,15);
        for(auto& value:totals)value*=256;
        const PaddedRows<std::int16_t> samples(contributionQ8,w,h);
        // A donor and its opposite inhibitor act on the same weighted offset.
        // Iterate only inhibitors; no grid-sized 961-tap scan is needed.
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int inhibition=std::min<int>(inhibitionQ8[sy*w+sx],256);
            if(!inhibition)continue;
            const int start=wrapped.x(sx,-15);
            for(int dy=-15;dy<=15;++dy)
            {
                auto* target=&totals[wrapped.y(sy,dy)*w];
                const auto* donor=samples.at(wrapped.y(sy,2*dy),sx-30);
                const std::int64_t scale=inhibition*offsetWeight(dy);
                wrappedSpans<1>(w,start,[&](int x,int offset,int count) {
                    for(int i=0;i<count;++i)
                        target[x+i]-=scale*(offsetWeights[offset+i]*donor[2*(offset+i)]);
                });
            }
        }
    }
    else
    {
        totals.assign(size,0);
        const PaddedRows<std::uint16_t> samples(inhibitionQ8,w,h);
        // Sparse donor alternative; signed contributions remain signed until
        // every pair is accumulated, so nearby deficits cancel bonuses exactly.
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int donor=contributionQ8[sy*w+sx];
            if(!donor)continue;
            const int start=wrapped.x(sx,-15);
            for(int dy=-15;dy<=15;++dy)
            {
                auto* target=&totals[wrapped.y(sy,-dy)*w];
                const auto* inhibitors=samples.at(wrapped.y(sy,-2*dy),sx-30);
                const std::int64_t scale=donor*offsetWeight(dy);
                wrappedSpans<1>(w,start,[&](int x,int offset,int count) {
                    for(int i=0;i<count;++i)
                        target[x+i]+=scale*(offsetWeights[offset+i]*(256-std::min<int>(inhibitors[2*(offset+i)],256)));
                });
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
        const DiagonalSamples<std::int16_t> samples(contributionQ8,w,h,false);
        // Shore at (x+2dy,y+2dx), donor at (x+dx,y+dy). Invert the
        // former relation to stamp only from nonzero support cells.
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int support=supportQ8[sy*w+sx];
            if(!support)continue;
            const int start=wrapped.x(sx,-30);
            const int diagonal=wrap(sx+2*sy,w);
            for(int dx=-15;dx<=15;++dx)
            {
                auto* target=&totals[wrapped.y(sy,-2*dx)*w];
                const std::int64_t scale=support*offsetWeight(dx);
                const auto* donor=samples.at(wrapped.x(diagonal,-3*dx),sy-2*dx-15);
                wrappedSpans<2>(w,start,[&](int x,int offset,int count) {
                    for(int i=0;i<count;++i)
                        target[x+2*i]+=scale*(offsetWeights[offset+i]*donor[30-offset-i]);
                });
            }
        }
    }
    else
    {
        const DiagonalSamples<std::uint16_t> samples(supportQ8,w,h,true);
        for(int sy=0;sy<h;++sy)for(int sx=0;sx<w;++sx)
        {
            const int donor=contributionQ8[sy*w+sx];
            if(!donor)continue;
            const int start=wrapped.x(sx,-15);
            const int diagonal=wrap(2*sx+sy,h);
            for(int dy=-15;dy<=15;++dy)
            {
                auto* target=&totals[wrapped.y(sy,-dy)*w];
                const std::int64_t scale=donor*offsetWeight(dy);
                const auto* support=samples.at(wrapped.y(diagonal,3*dy),sx+2*dy-15);
                wrappedSpans<1>(w,start,[&](int x,int offset,int count) {
                    for(int i=0;i<count;++i)
                        target[x+i]+=scale*(offsetWeights[offset+i]*support[offset+i]);
                });
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
	return ready && land.getW()==map.getW() && land.getH()==map.getH() &&
		aquatic.size()==std::size_t(map.getW())*map.getH();
}

void GrowthCache::terrainChanged(std::size_t index, const TerrainProperties& before,
	const TerrainProperties& after)
{
	if (!ready) return;
	// These are the actual kernel inputs. Inhibition saturates at one, and a
	// disabled fertility source contributes zero regardless of its stored value.
	if ((before.fertilitySource ? before.fertilityQ8 : 0) !=
		(after.fertilitySource ? after.fertilityQ8 : 0) ||
		std::min<unsigned>(before.inhibitionQ8, 256) != std::min<unsigned>(after.inhibitionQ8, 256) ||
		before.shoreSupportQ8 != after.shoreSupportQ8 || before.growthQ8 != after.growthQ8)
	{
		invalidate();
		return;
	}
	// Habitat permissions affect only this cell's resource-rate lookup. The
	// terrain-weighted fields remain exact, including the exposed landField().
    // The resource registry's compiled habitat table is read live by rate().
    assert(index < localGrowth.size());
}

void GrowthCache::rebuild(const Map& map)
{
    owner=&map;
	const int w=map.getW(), h=map.getH();
	const std::size_t size=std::size_t(w)*h;
	std::vector<std::int16_t> contribution(size);
	std::vector<std::uint16_t> inhibition(size), shore(size);
	localGrowth.resize(size);
	for (std::size_t i=0; i<size; ++i)
	{
		const auto& p=map.terrainPropertiesAt(i);
		contribution[i]=p.fertilitySource ? p.fertilityQ8 : 0;
		inhibition[i]=p.inhibitionQ8;
		shore[i]=p.shoreSupportQ8;
		localGrowth[i]=p.growthQ8;
	}
	land.rebuildWeighted(w,h,contribution,inhibition);
	aquatic=shoreGrowthField(w,h,contribution,shore);
	land.multiplyLocal(localGrowth);
	ready=true;
}

std::uint32_t GrowthCache::rate(std::size_t index,int resourceType) const
{
    assert(owner && index<localGrowth.size());
    if (resourceType<0 || !owner->resourceRegistry().valid(unsigned(resourceType)) ||
        !owner->terrainPropertiesAt(index).resourcesGrow ||
        !owner->terrainSupportsResourceAt(index,static_cast<ResourceId>(resourceType))) return 0;
    const auto& p=owner->resourcePropertiesByIndex(resourceType);
    std::uint64_t value=0;
    switch (p.ecology)
    {
    case ResourceEcology::Land: value=land.values()[index]; break;
    case ResourceEcology::Shore: value=std::uint64_t(aquatic[index])*localGrowth[index]/256; break;
    case ResourceEcology::Uniform: value=std::uint64_t(kScale)*localGrowth[index]/256; break;
    case ResourceEcology::None: return 0;
    }
    value=value*p.growthRate/kScale;
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
				if (map.isMaterialTakeable(x,y,MaterialId::Food) || map.isMaterialTakeable(x,y,MaterialId::Wood))
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
					if (!cell && map.terrainSupportsMaterialAt(nx,ny,MaterialId::Food))
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
			if (!map.terrainSupportsMaterialAt(int(i%w),int(i/w),MaterialId::Food)) keep[i]=0;
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
