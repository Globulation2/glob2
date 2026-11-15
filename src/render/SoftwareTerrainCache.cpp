// SPDX-License-Identifier: GPL-3.0-or-later
#include <PresentationWork.h>
#include <chrono>
#include "SoftwareTerrainCache.h"
#include "GlobalContainer.h"
#include "scene/SceneMap.h"
#include "terrain/TerrainCompositor.h"
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <cstring>
#include <stdexcept>
#include <map>
#include <set>
#include <RenderDevice.h>
#include <atomic>
#include "BackgroundWork.h"
// GPU pages use the compositor's prepared bytes and CPU coverage. This cache
// owns logical leases only; the device separately owns in-flight allocations.
class GpuTerrainPageCache
{
    using Resources = GAGCore::RenderResources;
    using PageKey = std::tuple<int,int,int,int>;
    struct Page { GAGCore::TextureHandle handle; GAGCore::TextureSnapshot pixels; std::uint64_t used; bool ready; };
    struct Source { GAGCore::TextureHandle handle; std::uint64_t revision, used; };
    struct MaskKey {
        TerrainVisual::Recipe recipe; int scale;
        bool operator<(const MaskKey &other) const {
            const auto fields=[](const auto &key) { const auto &r=key.recipe;
                return std::tie(r.corners,r.x,r.y,r.width,r.height,r.seed,r.neighborhood,r.hasNeighborhood,key.scale); };
            return fields(*this)<fields(other);
        }
    };
    struct Mask { std::shared_ptr<const GAGCore::CoverageData> data; std::array<TerrainVisual::MaterialId,4> palette;
        unsigned colors; std::size_t bytes; std::uint64_t used; };
    std::shared_ptr<Resources> resources;
    std::map<PageKey,Page> pages;
    std::set<PageKey> protectedPages;
    std::map<std::pair<TerrainVisual::MaterialId,unsigned>,Source> sources;
    std::map<MaskKey,Mask> masks;
    struct MaskLeaseBudget { std::atomic_size_t bytes{0}; };
    // Device restoration sources and queued commands can outlive this cache.
    // Charge their owned CPU coverage until the final shared lease disappears.
    inline static const auto globalMaskLeases=std::make_shared<MaskLeaseBudget>();
    const std::shared_ptr<MaskLeaseBudget> localMaskLeases=std::make_shared<MaskLeaseBudget>();
    std::size_t pageBytes=0,maskBytes=0;
    std::uint64_t frame=0,maskIdentity=0,contentRevision=0;
    inline static std::atomic_uint64_t nextCacheIdentity{0};
    const std::uint64_t cacheIdentity=++nextCacheIdentity;
    static constexpr std::size_t PageBudget=128u*1024*1024,MaskBudget=8u*1024*1024,MaxSources=1024;
    static constexpr std::size_t MaskLeaseBudgetBytes=40u*1024*1024;
    static PageKey key(const SoftwareTerrainCache::Chunk &chunk) {return {chunk.x,chunk.y,chunk.scale,chunk.downsample};}
    GAGCore::TextureSnapshot material(TerrainVisual::Compositor &compositor,const TerrainVisual::Recipe &recipe,TerrainVisual::MaterialId id) {
        const auto view=compositor.preparedMaterial(recipe,id);
        if(!view.size || view.pixels.empty()) return {};
        const auto sourceKey=std::make_pair(id,view.variant);
        auto found=sources.find(sourceKey);
        if(found!=sources.end() && found->second.revision==view.revision) {
            found->second.used=frame;return resources->snapshot(found->second.handle);
        }
        // Snapshot copies the prepared animation pixels before the next prepare.
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurfaceFrom(view.size,view.size,SDL_PIXELFORMAT_RGBA32,
                const_cast<void*>(static_cast<const void*>(view.pixels.data())),view.size*4),SDL_DestroySurface);
        if(!surface) throw std::bad_alloc();
        GAGCore::TextureDescriptor descriptor;descriptor.width=descriptor.height=view.size;
        descriptor.alpha=GAGCore::TextureDescriptor::Alpha::Straight;
        GAGCore::TextureHandle handle;
        if(found!=sources.end()) {
            handle=found->second.handle;
            if(!resources->update(handle,surface.get(),view.revision,descriptor)) throw std::bad_alloc();
        } else {
            // Source cache is bounded independently of submitted snapshots.
            // One four-layout material alone has 256 variants; sources this
            // frame already drew are never evicted, or a view thrashes them.
            if(sources.size()>=MaxSources) {
                auto oldest=std::min_element(sources.begin(),sources.end(),[](const auto &a,const auto &b){return a.second.used<b.second.used;});
                if(oldest->second.used!=frame) {resources->erase(oldest->second.handle);sources.erase(oldest);}
            }
            handle=resources->create(surface.get(),view.revision,descriptor);
            if(!handle) throw std::bad_alloc();
        }
        sources[sourceKey]={handle,view.revision,frame};return resources->snapshot(handle);
    }
    Mask coverage(TerrainVisual::Compositor &compositor,const TerrainVisual::Recipe &recipe,int scale) {
        const MaskKey maskKey{recipe,scale};
        if(auto found=masks.find(maskKey);found!=masks.end()) {found->second.used=frame;return found->second;}
        const auto samples=std::size_t(32*scale)*(32*scale);
        const auto allocationBytes=sizeof(GAGCore::CoverageData)+samples*sizeof(GAGCore::CoverageSample);
        // Retire idle lookup entries first. In-flight/device leases remain charged
        // even after eviction, so admission cannot hide their retained storage.
        while(globalMaskLeases->bytes.load()+allocationBytes>MaskLeaseBudgetBytes && !masks.empty()) {
            auto oldest=std::min_element(masks.begin(),masks.end(),[](const auto &a,const auto &b){return a.second.used<b.second.used;});
            maskBytes-=oldest->second.bytes;masks.erase(oldest);
        }
        auto used=globalMaskLeases->bytes.load();
        do {
            if(allocationBytes>MaskLeaseBudgetBytes || used>MaskLeaseBudgetBytes-allocationBytes)throw std::bad_alloc();
        } while(!globalMaskLeases->bytes.compare_exchange_weak(used,used+allocationBytes));
        localMaskLeases->bytes.fetch_add(allocationBytes);
        const auto release=[global=globalMaskLeases,local=localMaskLeases,allocationBytes](GAGCore::CoverageData *value) {
            delete value;global->bytes.fetch_sub(allocationBytes);local->bytes.fetch_sub(allocationBytes);
        };
        // Construct separately: a failed raw allocation must release its reserved
        // bytes; shared_ptr construction invokes its deleter if its control block fails.
        GAGCore::CoverageData *raw=nullptr;
        try {raw=new GAGCore::CoverageData;} catch(...) {release(nullptr);throw;}
        std::shared_ptr<GAGCore::CoverageData> owned(raw,release);
        auto original=compositor.mask(recipe,scale);
        // A local cache identity separates its epochs from retired devices
        // and other scene caches without consuming synchronized randomness.
        owned->identity=cacheIdentity;owned->revision=++maskIdentity;
        owned->width=owned->height=32*scale;owned->samples.resize(original.samples.size());
        // The fixed-size vector constructor is exact on supported standard
        // libraries. Reject unexpected extra capacity rather than undercharge it.
        if(owned->samples.capacity()!=samples)throw std::bad_alloc();
        for(std::size_t i=0;i<original.samples.size();++i) {
            const auto &sample=original.samples[i];auto &out=owned->samples[i];
            std::copy(std::begin(sample.others),std::end(sample.others),out.others.begin());
            out.margin=sample.margin;out.slots=sample.slots;
        }
        const auto bytes=allocationBytes;
        Mask result{std::move(owned),original.palette,original.colors,bytes,frame};
        while(maskBytes+bytes>MaskBudget && !masks.empty()) {
            auto oldest=std::min_element(masks.begin(),masks.end(),[](const auto &a,const auto &b){return a.second.used<b.second.used;});
            maskBytes-=oldest->second.bytes;masks.erase(oldest);
        }
        if(bytes<=MaskBudget) {masks.emplace(maskKey,result);maskBytes+=bytes;}
        return result;
    }
  public:
    explicit GpuTerrainPageCache(std::shared_ptr<Resources> owner):resources(std::move(owner)){}
    ~GpuTerrainPageCache() {for(const auto &[k,page]:pages) resources->erase(page.handle);
        for(const auto &[k,source]:sources) resources->erase(source.handle);}
    std::uint64_t registryIdentity() const {return resources->instanceIdentity();}
    std::size_t retainedBytes() const {return pageBytes + localMaskLeases->bytes.load();}
    std::size_t textureBytes() const {return pageBytes;}
    std::pair<std::size_t,std::size_t> compositionReservation(const SoftwareTerrainCache::Chunk &chunk) const {
        const auto dimension=std::size_t(SoftwareTerrainCache::ChunkPixels)*chunk.scale/chunk.downsample;
        const auto pixels=pages.contains(key(chunk))?0:dimension*dimension*4;
        // Submitted masks can outlive lookup eviction. Reserve their complete
        // bounded ownership allowance before adding packets to this frame.
        return {pixels+MaskLeaseBudgetBytes-localMaskLeases->bytes.load(),pixels};
    }
    void erase(const SoftwareTerrainCache::Chunk &chunk) {
        auto found=pages.find(key(chunk));if(found==pages.end())return;
        pageBytes-=found->second.pixels->bytes;resources->erase(found->second.handle);pages.erase(found);
    }
    void begin(std::uint64_t revision,std::uint64_t tick,int x0,int y0,int x1,int y1,int maskW,int maskH,int scale,int reduction) {
        if(contentRevision!=revision) {
            for(auto &[key,page]:pages) page.ready=false;
            contentRevision=revision;
        }
        frame=tick;protectedPages.clear();
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x)
            protectedPages.emplace((x*SoftwareTerrainCache::ChunkTiles)&maskW,(y*SoftwareTerrainCache::ChunkTiles)&maskH,scale,reduction);
    }
    GAGCore::TextureSnapshot find(const SoftwareTerrainCache::Chunk &chunk) {
        const auto found=pages.find(key(chunk));if(found==pages.end() || !found->second.ready)return {};
        found->second.used=frame;return found->second.pixels;
    }
    bool compose(GAGCore::GraphicContext &gfx,TerrainVisual::Compositor &compositor,
                 SoftwareTerrainCache::Chunk &chunk,const std::array<SoftwareTerrainCache::Tile,256> &tiles) {
        using namespace GAGCore;
        const int composition=32*chunk.scale,cellSize=composition/chunk.downsample;
        const int dimension=SoftwareTerrainCache::ChunkTiles*cellSize;
        // Recompose unpublished pixels in the same allocation; replacing its
        // handle would restart a deferred upload on every retry.
        auto existing=pages.find(key(chunk));
        auto target=existing==pages.end()?TextureSnapshot{}:existing->second.pixels;
        if(!target) {
            const auto bytes=std::size_t(dimension)*dimension*4;
            while(pageBytes+bytes>PageBudget) {
                auto oldest=pages.end();for(auto it=pages.begin();it!=pages.end();++it)
                    if(!protectedPages.contains(it->first) && (oldest==pages.end() || it->second.used<oldest->second.used))oldest=it;
                if(oldest==pages.end())throw std::bad_alloc();
                pageBytes-=oldest->second.pixels->bytes;resources->erase(oldest->second.handle);pages.erase(oldest);
            }
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> pixels(SDL_CreateSurface(dimension,dimension,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            if(!pixels)throw std::bad_alloc();
            SDL_FillSurfaceRect(pixels.get(),nullptr,0);
            TextureDescriptor descriptor;descriptor.width=descriptor.height=dimension;
            descriptor.alpha=TextureDescriptor::Alpha::Premultiplied;descriptor.budgetClass=TextureDescriptor::BudgetClass::TerrainPage;
            auto handle=resources->create(pixels.get(),1,descriptor);if(!handle)throw std::bad_alloc();
            target=resources->snapshot(handle);pages[key(chunk)]={handle,target,frame,false};pageBytes+=target->bytes;
        }
        // Prepare owned packets before emitting any part of the page. Failure
        // leaves the caller free to use the CPU compositor for the entire page.
        std::vector<DrawPacket> packets;packets.reserve(257);
        DrawPacket clear;clear.kind=DrawPacket::Kind::Clear;clear.clearColor=true;clear.clearValue={0,0,0,0};packets.push_back(std::move(clear));
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            const auto &tile=tiles[y*16+x];if(!tile.discovered)continue;
            auto data=std::make_shared<WeightedComposeData>();
            data->compositionSize=composition;data->reductionDivisor=chunk.downsample;
            data->targetRect={x*cellSize,y*cellSize,cellSize,cellSize};
            std::array<TerrainVisual::MaterialId,4> palette{};
            const auto &recipe=tile.recipe;
            const bool uniform=recipe.uniform();
            if(uniform) {palette[0]=recipe.corners[0];data->paletteCount=1;}
            else {const auto mask=coverage(compositor,recipe,chunk.scale);
                data->coverage=mask.data;palette=mask.palette;data->paletteCount=mask.colors;}
            for(unsigned slot=0;slot<data->paletteCount;++slot) {
                data->materials[slot]=material(compositor,recipe,palette[slot]);
                const auto &seam=compositor.catalog().materials[palette[slot]].seam;
                data->seams[slot]={seam.height,seam.cast,seam.castWidth,seam.fringe,seam.fringeWidth,seam.fringeColor};
                const auto &surf=compositor.catalog().materials[palette[slot]].surf;
                const auto &material=compositor.catalog().materials[palette[slot]];
                const std::uint32_t calm=material.shoreCalm?
                    std::uint32_t(256-TerrainVisual::ShoreCalmContrastQ8)<<24|material.calmMean[0]<<16|material.calmMean[1]<<8|material.calmMean[2]:0;
                data->surfs[slot]={surf.period,surf.reach,surf.foam,surf.foamWidth,surf.shallow,surf.shallowWidth,
                                   surf.wet,surf.wetWidth,surf.shallowColor,surf.foamColor,surf.ticks,calm,
                                   material.shoreWash};
            }
            data->surfOriginX=recipe.x*8192;data->surfOriginY=recipe.y*8192;data->surfTime=compositor.surfTime();
            for(unsigned k=0;k<4;++k)data->shoreCalm|=std::uint32_t(recipe.calm[k])<<(8*k);
            DrawPacket packet;packet.kind=DrawPacket::Kind::WeightedCompose;packet.blend=DrawPacket::Blend::Opaque;
            packet.weightedCompose=std::move(data);packets.push_back(std::move(packet));
        }
        pages.at(key(chunk)).ready=false;
        if(!gfx.beginDeviceTarget(target,dimension,dimension))return false;
        try {
            for(auto &packet:packets) if(!gfx.submitDevicePacket(std::move(packet))) {
                gfx.endDeviceTarget();return false;
            }
            gfx.endDeviceTarget();
        } catch(...) {gfx.endDeviceTarget();throw;}
        pages.at(key(chunk)).ready=true;
        return true;
    }
};

namespace
{
constexpr int NativeTilePixels =
	SoftwareTerrainCache::ChunkPixels / SoftwareTerrainCache::ChunkTiles;
int chunkOf(int tile)
{
	return int(std::floor(double(tile) / SoftwareTerrainCache::ChunkTiles));
}
std::size_t pageStorage(int scale, bool gpu, int downsample = 1)
{
	// Retain the fixed recipe/run allowance at every density. GPU accounting
	// also reserves texture and mip storage alongside the CPU surface.
	const std::size_t pixels = SoftwareTerrainCache::ChunkPixels * scale / downsample;
	return SoftwareTerrainCache::ChunkStorageBytes -
		   SoftwareTerrainCache::ChunkPixels * SoftwareTerrainCache::ChunkPixels * 4 +
		   pixels * pixels * 4 * (gpu ? 3 : 1);
}
// Normal surface destruction flushes queued renderer references and releases
// textures on both GPU backends. Keep the composed pixels owned by the page.
void retireTexture(SoftwareTerrainCache::Chunk &chunk)
{
	// A page whose pixels went after upload had only the texture: compose it again.
	if (chunk.image->portableReleased())
		chunk.valid = false;
	chunk.parked.reset(chunk.image->takePixels());
	chunk.image.reset();
}
// A page drawn on the portable renderer keeps its CPU pixels only while
// something will write them: animation outside the shared overlay tiles
// (shorelines, mixed animated cells) or a pending reduction from it.
bool needsPagePixels(const SoftwareTerrainCache::Chunk &chunk, const TerrainVisual::Compositor &compositor)
{
	if (chunk.hasShoreline) return true;
	for (int i = 0; i < SoftwareTerrainCache::ChunkTiles * SoftwareTerrainCache::ChunkTiles; ++i)
	{
		const auto &tile = chunk.tiles[i];
		if (!tile.discovered || chunk.overlay[i]) continue;
		std::array<TerrainVisual::MaterialId, 4> palette;
		const unsigned count = tile.recipe.palette(palette);
		if (std::any_of(palette.begin(), palette.begin() + count, [&](auto id) { return compositor.animated(id); }))
			return true;
	}
	return false;
}
bool portableRenderer()
{
	return globalContainer->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL;
}
// Whether this view's pages compose HD material frames. Accelerated renderers
// do, except the portable SDL renderer at one screen pixel per logical pixel
// or fewer (Compositor::highResolutionPages), where HD pages only cost.
// Captures draw once at their own density and keep HD.
bool composeHighResolution(TerrainVisual::Compositor &compositor, bool tiledCapture)
{
	auto &gfx = *globalContainer->gfx;
	if (!gfx.rendererCapabilities().accelerated) return false;
	if (tiledCapture || !portableRenderer()) return true;
	return compositor.highResolutionPages(double(gfx.mapTransformScale()) * gfx.getRasterScale());
}
struct SamplingPlan
{
	int resolution;
	int downsample;
	std::size_t budget;
	bool pageFits, viewFits;
};
SamplingPlan samplingPlan(const SceneMap &map, int left, int top, int right, int bottom, int vx,
						  int vy, int preferredResolution, int preferredDownsample = 0,
						  bool stableView = false)
{
	const bool gpu = globalContainer->gfx->rendererCapabilities().accelerated;
	const auto budget = gpu ? SoftwareTerrainCache::GPUBudget : SoftwareTerrainCache::Budget;
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	// A map smaller than one page repeats within that canonical page. All map
	// dimensions are powers of two, so its period divides the page dimensions.
	constexpr int tiles = SoftwareTerrainCache::ChunkTiles;
	// Size the interactive HD view for its worst alignment on the page grid.
	// A pan can expose an extra row and column without changing the zoom.
	const int columns = stableView ? (right - left + tiles - 1) / tiles + 1 : x1 - x0 + 1;
	const int rows = stableView ? (bottom - top + tiles - 1) / tiles + 1 : y1 - y0 + 1;
	const auto visiblePages = std::uint64_t(std::min(columns, std::max(1, map.getW() / tiles))) *
		std::min(rows, std::max(1, map.getH() / tiles));
	const auto cacheBudget = gpu ? SoftwareTerrainCache::GPUCacheBudget : budget;
	const int limit = globalContainer->gfx->maximumTextureSize();
	const auto fitsDevice = [&](int resolution)
	{ return limit <= 0 || SoftwareTerrainCache::ChunkPixels * resolution <= limit; };
	int resolution = preferredResolution;
	// Cache and streaming draws choose density from the complete view, not from
	// each submitted page. Memory pressure must not change sampling page by page.
	while (resolution > 1 &&
		   (!fitsDevice(resolution) || visiblePages * pageStorage(resolution, gpu) > budget))
		resolution /= 2;
	// A zoomed-out view can exceed the budget even at native density. Streaming
	// then recomposes and uploads the entire view every frame. Retain filtered
	// pages instead, down to the nearest power-of-two display density. Always
	// rounding up would leave a 2x HiDPI crossfade streaming just above half
	// density. Nearest-level selection limits magnification to sqrt(2).
	// A forced density keeps streaming pages identical to the complete-view plan.
	int downsample = preferredDownsample > 0 ? preferredDownsample : 1;
	if (gpu && !preferredDownsample && resolution == 1)
	{
		auto &target = *globalContainer->gfx;
		// Offscreen captures can override the window DPI. Use the active
		// target's raster scale so atlas detail is independent of window size.
		const double density = target.mapTransformScale() * target.getRasterScale();
		while (downsample < NativeTilePixels && density <= std::sqrt(2.0) / (downsample * 2) &&
			   visiblePages * pageStorage(resolution, gpu, downsample) > budget)
			downsample *= 2;
	}
	const auto storage = pageStorage(resolution, gpu, downsample);
	const bool pageFits =
		(limit <= 0 || SoftwareTerrainCache::ChunkPixels * resolution / downsample <= limit) &&
		storage <= budget;
	// Immediate draws can stream textures from retained CPU pixels. Progressive
	// draws need the full view to fit eventually or some holes never fill.
	const auto cpuStorage = pageStorage(resolution, false, downsample);
	const auto minimumStorage = gpu && !stableView ? visiblePages * cpuStorage + storage - cpuStorage
		: visiblePages * storage;
	const bool texturesFit = !stableView || !gpu || visiblePages * (storage - cpuStorage) <= budget;
	return {resolution, downsample, cacheBudget, pageFits,
		pageFits && texturesFit && minimumStorage <= cacheBudget};
}

// Box-filter composed coverage in premultiplied space, then retain straight
// alpha for the ordinary surface upload. Transparent (undiscovered) cells must not
// darken their neighbours.
// Both surfaces are ARGB8888; the power-of-two divisor divides one native tile.
// Filtering each tile independently preserves exact tile-aligned page crops.
template <int divisor> void reduceTile(SDL_Surface *source, SDL_Surface *target, int ox, int oy)
{
	constexpr int size = NativeTilePixels / divisor, count = divisor * divisor;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
		{
			unsigned alpha = 0, rgb[3] = {};
			for (int dy = 0; dy < divisor; ++dy)
			{
				const auto *row =
					reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(source->pixels) +
													 (y * divisor + dy) * source->pitch);
				for (int dx = 0; dx < divisor; ++dx)
				{
					const Uint32 p = row[x * divisor + dx];
					const unsigned a = p >> 24;
					alpha += a;
					for (int k = 0; k < 3; ++k)
						rgb[k] += ((p >> (k * 8)) & 255) * a;
				}
			}
			Uint32 pixel = ((alpha + count / 2) / count) << 24;
			for (int k = 0; k < 3; ++k)
				pixel |= (alpha == 255u * count ? (rgb[k] + 255u * count / 2) / (255u * count)
												: (alpha ? (rgb[k] + alpha / 2) / alpha : 0))
						 << (k * 8);
			auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(target->pixels) +
												   (oy + y) * target->pitch);
			row[ox + x] = pixel;
		}
}
void reduceTile(SDL_Surface *source, SDL_Surface *target, int ox, int oy, int divisor)
{
	// The sampling plan chooses one of these power-of-two reductions. Constant
	// footprints let the compiler unroll filtering and divide opaque colours by
	// a constant, with the same integer rounding as the alpha-weighted path.
	switch (divisor)
	{
	case 2:
		return reduceTile<2>(source, target, ox, oy);
	case 4:
		return reduceTile<4>(source, target, ox, oy);
	case 8:
		return reduceTile<8>(source, target, ox, oy);
	case 16:
		return reduceTile<16>(source, target, ox, oy);
	case 32:
		return reduceTile<32>(source, target, ox, oy);
	default:
		throw std::logic_error("Invalid terrain reduction");
	}
}
SamplingPlan viewSamplingPlan(const SceneMap &map, int left, int top, int right, int bottom, int vx,
							  int vy, int preferredResolution, bool tiledCapture,
							  int preferredDownsample = 0, bool stableView = false)
{
	// A narrow atlas edge must not select HD pages after its wider neighbor
	// selected native pages. That changes seams and repeatedly clears the cache.
	// Select density from the complete capture, then admit each bounded tile.
	if (tiledCapture)
	{
		const auto capture =
			samplingPlan(map, 0, 0, map.getW() - 1, map.getH() - 1, 0, 0, preferredResolution);
		preferredResolution = capture.resolution;
		preferredDownsample = capture.downsample;
		if (globalContainer->gfx->rendererCapabilities().accelerated)
			preferredDownsample = std::max(preferredDownsample,
				NativeTilePixels / SoftwareTerrainCache::capturePixelsPerCell(map.getW(), map.getH()));
	}
	return samplingPlan(map, left, top, right, bottom, vx, vy, preferredResolution,
						preferredDownsample, stableView);
}
void drawEmergencyTiles(const SceneMap &map, int left, int top, int right, int bottom, int vx,
						int vy, Uint32 visibleTeams, bool wholeMap, int preferredResolution)
{
	auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
	auto &target = *globalContainer->gfx;
	const int limit = target.maximumTextureSize();
	int scale = preferredResolution;
	while (scale > 1 && limit > 0 && 32 * scale > limit)
		scale /= 2;
	if (limit > 0 && 32 * scale > limit)
		throw std::runtime_error("Device cannot fit a native terrain tile");
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
		SDL_CreateSurface(32 * scale, 32 * scale, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
	if (!pixels)
		throw std::bad_alloc();
	TerrainVisual::Surface tile(pixels.get(), scale > 1);
	pixels.release(); // Surface owns pixels only after successful construction.
	for (int y = top; y <= bottom; ++y)
		for (int x = left; x <= right; ++x)
			if (wholeMap || map.isMapPartiallyDiscovered(x + vx - 1, y + vy - 1, x + vx + 1,
														 y + vy + 1, visibleTeams))
			{
				compositor.compose(compositor.describe(map, x + vx, y + vy), tile.getSDLSurface(),
								   0, 0, scale);
				tile.markPixelsChanged();
				target.drawSurface(x * 32, y * 32, 32, 32, &tile);
			}
}
// A CPU-only view owns its SDL descriptor, but borrows immutable chunk pixels.
// The producer has already verified every pixel in this rectangle is opaque.
class OpaqueView final : public GAGCore::DrawableSurface
{
  public:
	OpaqueView(SDL_Surface *owner, SDL_Rect rect)
	{
		sdlsurface = SDL_CreateSurfaceFrom(
			rect.w, rect.h, owner->format,
			static_cast<char *>(owner->pixels) + rect.y * owner->pitch + rect.x * 4, owner->pitch);
		if (!sdlsurface)
			throw std::bad_alloc();
		opaquePixels = true;
		opacityRevision = pixelRevision;
	}
};
void buildOpaqueRuns(SoftwareTerrainCache::Chunk &chunk)
{
	std::vector<SDL_Rect> runs;
	for (int y = 0; y < SoftwareTerrainCache::ChunkTiles; ++y)
		for (int x = 0; x < SoftwareTerrainCache::ChunkTiles;)
		{
			auto opaque = [&](int column)
			{ return chunk.opaque[y * SoftwareTerrainCache::ChunkTiles + column]; };
			if (!opaque(x))
			{
				++x;
				continue;
			}
			const int start = x++;
			while (x < SoftwareTerrainCache::ChunkTiles && opaque(x))
				++x;
			SDL_Rect rect{start * 32, y * 32, (x - start) * 32, 32};
			auto previous = std::find_if(
				runs.begin(), runs.end(), [&](const SDL_Rect &run)
				{ return run.x == rect.x && run.w == rect.w && run.y + run.h == rect.y; });
			if (previous == runs.end())
				runs.push_back(rect);
			else
				previous->h += rect.h;
		}
	for (const auto &rect : runs)
		chunk.opaqueRuns.push_back(
			{rect, std::make_unique<OpaqueView>(chunk.image->getSDLSurface(), rect)});
}
} // namespace

namespace
{
struct TerrainCellResult
{
    unsigned index;
    SoftwareTerrainCache::Tile input;
    Uint8 *pixels = nullptr;
    std::optional<TerrainVisual::Compositor::CellMask> mask;
    const TerrainVisual::Compositor::CellMask *reused = nullptr;
    // Keeps `reused` alive for a background job when it is a page's held mask.
    std::shared_ptr<const SoftwareTerrainCache::HeldMask> held;
    std::optional<TerrainVisual::Compositor::Refresh> refresh;
    bool opaque = false, empty = false, keepMask = false;
};
// The first alpha selects one constant predicate: opaque and empty cells need
// only one scan, while a partially transparent sample rejects both immediately.
// Keep the scan vectorizable for the common uniform opaque terrain pages.
template <Uint32 Alpha>
bool allAlpha(const SDL_Surface &surface, SDL_Rect rect)
{
    for (int y = rect.y; y < rect.y + rect.h; ++y)
    {
        const auto *row = reinterpret_cast<const Uint32 *>(
            static_cast<const Uint8 *>(surface.pixels) + y * surface.pitch);
        if (!std::all_of(row + rect.x, row + rect.x + rect.w,
                         [](Uint32 pixel) { return pixel >> 24 == Alpha; }))
            return false;
    }
    return true;
}
std::pair<bool, bool> classifyAlpha(const SDL_Surface &surface, SDL_Rect rect)
{
    const auto *first = reinterpret_cast<const Uint32 *>(
        static_cast<const Uint8 *>(surface.pixels) + rect.y * surface.pitch) + rect.x;
    if (*first >> 24 == 255) return {allAlpha<255>(surface, rect), false};
    if (*first >> 24 == 0) return {false, allAlpha<0>(surface, rect)};
    return {false, false};
}
// A reduced page (downsample > 1) composes each cell at native size into a
// job-local tile and box-filters it into the page, as the serial path does.
void buildTerrainJob(std::span<TerrainCellResult> results,
                     const TerrainVisual::Compositor::CpuMaterials &materials, int samplingScale,
                     int downsample, int pitch, std::size_t cellsPerJob, std::size_t job)
{
    TerrainVisual::Compositor::CpuScratch scratch;
    std::vector<Uint32> native;
    SDL_Surface nativeTile{};
    if (downsample > 1)
    {
        native.resize(std::size_t(NativeTilePixels) * NativeTilePixels);
        nativeTile.format = SDL_PIXELFORMAT_ARGB8888;
        nativeTile.w = nativeTile.h = NativeTilePixels;
        nativeTile.pitch = NativeTilePixels * 4;
        nativeTile.pixels = native.data();
    }
    for (std::size_t k = job * cellsPerJob; k < results.size() && k < (job + 1) * cellsPerJob; ++k)
    {
        auto &result = results[k];
        SDL_Surface output{};
        output.format = SDL_PIXELFORMAT_ARGB8888;
        output.w = output.h = 32 * samplingScale / downsample;
        output.pitch = pitch;
        output.pixels = result.pixels;
        if (result.input.discovered)
        {
            if (!result.reused && result.keepMask)
                result.mask = materials.mask(result.input.recipe, samplingScale);
            materials.compose(result.input.recipe, downsample > 1 ? &nativeTile : &output, samplingScale,
                              scratch,
                              result.reused ? result.reused
                              : result.mask ? &*result.mask
                                            : nullptr,
                              downsample > 1 || !result.refresh ? nullptr : &*result.refresh);
            if (downsample > 1)
                reduceTile(&nativeTile, &output, 0, 0, downsample);
            std::tie(result.opaque, result.empty) =
                classifyAlpha(output, {0, 0, output.w, output.h});
        }
        else
        {
            // Hidden cells formerly received the slab's zero initialization.
            // Write their complete unpublished rectangle without touching SDL state.
            for (int y = 0; y < output.h; ++y)
                std::fill_n(reinterpret_cast<Uint32 *>(result.pixels + y * pitch), output.w, 0);
            result.opaque = false;
            result.empty = true;
        }
    }
}
} // namespace

// A cold page, or an animation refresh of a valid one, composed off the
// graphics thread. Workers write only into this object's pixels and results,
// from an immutable material snapshot; the graphics thread installs the
// finished page into the cache on a later frame.
struct TerrainPendingPage
{
    int x = 0, y = 0, scale = 1, downsample = 1;
    Uint32 seed = 0;
    Uint64 terrainRevision = 0;
    std::array<Uint32, SoftwareTerrainCache::SourceSpan * SoftwareTerrainCache::SourceSpan> sources{};
    std::array<SoftwareTerrainCache::Tile, SoftwareTerrainCache::ChunkTiles * SoftwareTerrainCache::ChunkTiles> tiles{};
    std::vector<std::pair<TerrainVisual::MaterialId, std::uint64_t>> materialRevisions;
    std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> materials;
    std::vector<TerrainCellResult> results;
    // A cold page composes into pixels; a refresh of a valid page composes only
    // its changed cells into cellPixels, one cell after another.
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels{nullptr, SDL_DestroySurface};
    std::vector<Uint32> cellPixels;
    bool refresh = false;
    int pitch = 0;
    Uint64 discoveryRevision = 0;
    Uint32 discoveryTeams = 0;
    bool discoveryWhole = false;
    std::size_t jobs = 0, cellsPerJob = 1;
    std::atomic<std::size_t> remaining{0};
    std::atomic<bool> cancelled{false}, failed{false};
    std::uint64_t wanted = 0;
    bool done() const { return remaining.load(std::memory_order_acquire) == 0; }
};

namespace
{
// Queues a pending page's jobs on the shared presentation workers.
void submitPage(const std::shared_ptr<TerrainPendingPage> &page)
{
    for (std::size_t job = 0; job < page->jobs; ++job)
        BackgroundWork::submit([page, job]
        {
            if (!page->cancelled.load(std::memory_order_relaxed) && !page->failed.load(std::memory_order_relaxed))
            {
                try
                {
                    buildTerrainJob(page->results, *page->materials, page->scale, page->downsample,
                                    page->pitch, page->cellsPerJob, job);
                }
                catch (...)
                {
                    page->failed.store(true, std::memory_order_relaxed);
                }
            }
            page->remaining.fetch_sub(1, std::memory_order_acq_rel);
        });
}
}

void SoftwareTerrainCache::cancelPending()
{
    for (auto &page : pending)
        page->cancelled.store(true, std::memory_order_relaxed);
    pending.clear();
    workerMaterials.reset();
}

int SoftwareTerrainCache::capturePixelsPerCell(int width, int height)
{
	const auto count =
		std::uint64_t(std::max(1, width / ChunkTiles)) * std::max(1, height / ChunkTiles);
	int divisor = 1;
	while (divisor < NativeTilePixels)
	{
		const auto cpu = pageStorage(1, false, divisor), total = pageStorage(1, true, divisor);
		if (count * total <= GPUCacheBudget && count * (total - cpu) <= GPUBudget)
			break;
		divisor *= 2;
	}
	return NativeTilePixels / divisor;
}

bool SoftwareTerrainCache::prepare(const SceneMap &map, GAGCore::Sprite &sprite, int left, int top,
								   int right, int bottom, int vx, int vy, Uint32 visibleTeams,
								   bool wholeMap, int time, bool tiledCapture)
{
	const bool fixedDensity = benchmarkDownsample > 0 && !tiledCapture;
	return prepareAtResolution(map, sprite, left, top, right, bottom, vx, vy, visibleTeams,
							   wholeMap, time, fixedDensity ? 1 : 0, tiledCapture,
							   fixedDensity ? benchmarkDownsample : 0);
}
namespace
{
// Only a contact between a surf material and a dry material can animate.
bool hasShoreline(const TerrainVisual::Recipe &recipe, const TerrainVisual::Compositor &compositor)
{
    // The palette includes halo materials, so a cell beside the water's edge
    // whose corners are all land still animates its wash.
    const auto liquid = [&](auto id) { return compositor.catalog().materials[id].surf.period > 0; };
    std::array<TerrainVisual::MaterialId, 4> palette;
    const unsigned count = recipe.palette(palette);
    const bool first = liquid(palette[0]);
    return std::any_of(palette.begin() + 1, palette.begin() + count, [&](auto id) { return liquid(id) != first; });
}

bool surfStepChanged(TerrainVisual::MaterialId id, unsigned previousTime,
                     const TerrainVisual::Compositor &compositor)
{
    const auto &surf = compositor.catalog().materials[id].surf;
    return surf.period && previousTime / surf.ticks != compositor.surfTime() / surf.ticks;
}

// An unchanged mixed cell recomposes only the samples its changed textures
// and advanced surf reach; the page keeps every other sample's pixels.
template <class Changed>
TerrainVisual::Compositor::Refresh cellRefresh(const TerrainVisual::Recipe &recipe, Changed materialChanged,
                                               unsigned previousTime, const TerrainVisual::Compositor &compositor)
{
    TerrainVisual::Compositor::Refresh refresh;
    std::array<TerrainVisual::MaterialId, 4> palette;
    for (unsigned slot = 0, count = recipe.palette(palette); slot < count; ++slot)
    {
        refresh.textures |= unsigned(materialChanged(palette[slot])) << slot;
        refresh.surf |= unsigned(surfStepChanged(palette[slot], previousTime, compositor)) << slot;
    }
    return refresh;
}
// Rectangles covering a page's recomposed cells: runs along each row, joined
// with identical runs directly below them.
std::vector<SDL_Rect> dirtyRects(const std::array<bool, SoftwareTerrainCache::ChunkTiles * SoftwareTerrainCache::ChunkTiles> &dirty,
                                 int cellPixels)
{
    constexpr int tiles = SoftwareTerrainCache::ChunkTiles;
    std::vector<SDL_Rect> rects;
    for (int y = 0; y < tiles; ++y)
        for (int x = 0; x < tiles;)
        {
            if (!dirty[y * tiles + x])
            {
                ++x;
                continue;
            }
            const int start = x;
            while (x < tiles && dirty[y * tiles + x])
                ++x;
            const SDL_Rect rect{start * cellPixels, y * cellPixels, (x - start) * cellPixels, cellPixels};
            auto above = std::find_if(rects.begin(), rects.end(), [&](const SDL_Rect &run)
                { return run.x == rect.x && run.w == rect.w && run.y + run.h == rect.y; });
            if (above == rects.end())
                rects.push_back(rect);
            else
                above->h += rect.h;
        }
    return rects;
}
bool tileSurfChanged(const TerrainVisual::Recipe &recipe, unsigned previousTime,
                     const TerrainVisual::Compositor &compositor)
{
    std::array<TerrainVisual::MaterialId, 4> palette;
    const unsigned count = recipe.palette(palette);
    return previousTime != compositor.surfTime() && hasShoreline(recipe, compositor) &&
        std::any_of(palette.begin(), palette.begin() + count,
            [&](auto id) { return surfStepChanged(id, previousTime, compositor); });
}
}
bool SoftwareTerrainCache::tryBuildCpuPage(Chunk &page,
                                        const std::array<Tile, ChunkTiles * ChunkTiles> &tiles,
                                        TerrainVisual::Compositor &compositor, SDL_Surface *target,
                                        bool wasValid, std::size_t budget, bool nativeAvailable,
                                        std::array<bool, ChunkTiles * ChunkTiles> *dirty)
{
    // Reduced pages compose native cells and filter them; a retained native
    // page is already that input, so the serial path reduces it directly.
    if ((downsample != 1 && (nativeAvailable || resolution != 1)) || resolution > 4 ||
        GAGCore::renderExecution() == GAGCore::RenderExecution::Immediate ||
        !GAGCore::PresentationWork::current) return false;
    const auto materialChanged = [&](TerrainVisual::MaterialId id)
    {
        const auto old = std::find_if(page.materialRevisions.begin(), page.materialRevisions.end(),
                                      [id](const auto &revision) { return revision.first == id; });
        return old == page.materialRevisions.end() ||
               old->second != compositor.textureRevision(id);
    };
    // Keep oversized HD cells and the optimized reduction path
    // serial. A CPU job covers at most 16K sampled pixels.
    constexpr std::size_t inFlightLimit = 4u * 1024u * 1024u;
    const std::size_t cellPixels = std::size_t(32 * resolution) * (32 * resolution);
    // Select changed cells first: a local edit needs neither snapshots nor
    // admission space for materials used only by unchanged cells in this page.
    std::vector<unsigned> changed;
    changed.reserve(tiles.size());
    bool hasDiscoveredChanges = false;
    for (unsigned i = 0; i < tiles.size(); ++i)
    {
        const auto &tile = tiles[i];
        if (wasValid && page.tiles[i] == tile &&
            (!tile.discovered || (std::none_of(tile.recipe.corners.begin(),
                                               tile.recipe.corners.end(), materialChanged) &&
                                  !tileSurfChanged(tile.recipe, page.surfTime, compositor))))
            continue;
        changed.push_back(i);
        hasDiscoveredChanges = hasDiscoveredChanges || tile.discovered;
    }
    if (!hasDiscoveredChanges) return false;
    const std::size_t perCell =
        cellPixels * sizeof(TerrainVisual::Compositor::CellMask::Sample) +
        sizeof(TerrainVisual::Compositor::CellMask) + sizeof(Tile) + 256;
    PERF_SCOPE_TIME(TerrainPlanning);
    // The page is unpublished for the whole rebuild. Lock on the owner once;
    // workers receive only disjoint raw cell rectangles and the stable pitch.
    // A failure may leave partial pixels, but the caller keeps the page invalid.
    if (!SDL_LockSurface(target)) throw std::runtime_error(SDL_GetError());
    struct Unlock
    {
        SDL_Surface *surface;
        ~Unlock() { SDL_UnlockSurface(surface); }
    } unlock{target};
    auto *pixels = static_cast<Uint8 *>(target->pixels);
    const int pitch = target->pitch, n = 32 * resolution / downsample;
    // Planning is owner-only. Masks cannot be evicted or
    // replaced until the synchronous group finishes.
    for (std::size_t first = 0; first < changed.size();)
    {
        // Snapshot only this bounded group's variants. A whole page can span
        // several layouts and exceed the in-flight cap before any jobs start.
        const std::size_t allowance = bytes() <= budget
            ? std::min({inFlightLimit, budget - bytes(), GAGCore::RenderWorkReservation::available()}) : 0;
        std::size_t groupCells = std::min(std::size_t(64), std::max(std::size_t(2), changed.size() - first));
        std::size_t count = 0, reserve = 0;
        std::vector<TerrainVisual::Recipe> recipes;
        recipes.reserve(groupCells);
        for (;;)
        {
            count = std::min(groupCells, changed.size() - first);
            recipes.clear();
            for (std::size_t k = first; k < first + count; ++k)
                if (tiles[changed[k]].discovered) recipes.push_back(tiles[changed[k]].recipe);
            const auto materialBytes = compositor.cpuMaterialBytesForRecipes(recipes);
            reserve = materialBytes * 2 + perCell * (count + 2);
            if (reserve <= allowance) break;
            if (groupCells == 2) return false;
            groupCells = std::max(std::size_t(2), groupCells / 2);
        }
        GAGCore::RenderWorkReservation workReservation;
        if (!workReservation.reserve(reserve)) return false;
        std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> materials;
        {
            PERF_SCOPE_TIME(TerrainMaterials);
            materials = compositor.cpuMaterialsForRecipes(recipes);
        }
        cpuMaterialLease = materials;
        std::vector<TerrainCellResult> results;
        results.reserve(count);
        for (std::size_t k = first; k < first + count; ++k)
        {
            const unsigned i = changed[k];
            TerrainCellResult result{};
            result.index = i;
            result.input = tiles[i];
            result.pixels = pixels + (i / ChunkTiles) * n * pitch + (i % ChunkTiles) * n * 4;
            const auto &recipe = tiles[i].recipe;
            const auto &held = page.masks[i];
            if (tiles[i].discovered && held && held->mask.recipe == recipe &&
                held->mask.scale == resolution)
                result.reused = &held->mask;
            if (wasValid && page.tiles[i] == tiles[i] && tiles[i].discovered && !recipe.uniform())
                result.refresh = cellRefresh(recipe, materialChanged, page.surfTime, compositor);
            std::array<TerrainVisual::MaterialId, 4> palette;
            const unsigned paletteCount = recipe.palette(palette);
            result.keepMask = tiles[i].discovered && !recipe.uniform() &&
                              std::any_of(palette.begin(), palette.begin() + paletteCount,
                                          [&](auto id) { return compositor.animated(id); });
            results.push_back(std::move(result));
        }
        // At most 16K sampled pixels per job, and at least two jobs per participant
        // so a wide pool is not left idle by a group of a few large jobs.
        const std::size_t participants = std::max<std::size_t>(1, GAGCore::renderConcurrency());
        const std::size_t cellsPerJob = std::clamp(
            (results.size() + 2 * participants - 1) / (2 * participants), std::size_t(1),
            std::max(std::size_t(1), 16384 / cellPixels));
        const auto started = std::chrono::steady_clock::now();
        {
            PERF_SCOPE_TIME(TerrainBatch);
            GAGCore::executePresentationJobs(
                (results.size() + cellsPerJob - 1) / cellsPerJob,
                [&results, materials, samplingScale = resolution, reduction = downsample, pitch,
                 cellsPerJob](std::size_t job)
                { buildTerrainJob(results, *materials, samplingScale, reduction, pitch, cellsPerJob, job); });
        }
        ++workMetrics.batches;
        workMetrics.jobs += (results.size() + cellsPerJob - 1) / cellsPerJob;
        workMetrics.cells += results.size();
        workMetrics.batchNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - started)
                                   .count();
        workMetrics.peakReservedBytes = std::max(workMetrics.peakReservedBytes, reserve);
        // Pixel writes have joined before owner-only mask/opacity accounting.
        // No worker publishes drawable state or retained mask ownership.
        PERF_SCOPE_TIME(TerrainInstall);
        for (auto &result : results)
        {
            auto &held = page.masks[result.index];
            if (held && !result.reused)
                held.reset();
            if (result.mask && releaseMasks(result.mask->bytes()))
                held = std::make_unique<HeldMask>(std::move(*result.mask), maskTotal);
            page.opaque[result.index] = result.opaque;
            page.empty[result.index] = result.empty;
            if (dirty) (*dirty)[result.index] = true;
            page.overlay[result.index] = false;
        }
        first += count;
    }
    return true;
}

SoftwareTerrainCache::AnimatedTiles *SoftwareTerrainCache::animatedTile(TerrainVisual::Compositor &compositor,
                                                                       const TerrainVisual::Recipe &recipe,
                                                                       SDL_Rect &source, int scale, int reduction)
{
    const auto id = recipe.corners[0];
    const auto &material = compositor.catalog().materials[id];
    if (material.animationFrames <= 1 || material.variants.empty()) return nullptr;
    auto &set = animatedTiles[{unsigned(id), scale, reduction}];
    set.used = frame;
    const int variants = int(material.variants.size());
    // Tiles at the page's density, each in a one-texel border copied from its
    // edge, so linear sampling of a scaled page never reads a neighbour.
    const int n = NativeTilePixels * scale / reduction, cell = n + 2;
    if (!set.atlas)
    {
        set.columns = int(std::ceil(std::sqrt(double(variants))));
        const int rows = (variants + set.columns - 1) / set.columns;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
            SDL_CreateSurface(set.columns * cell, rows * cell, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        if (!pixels) throw std::bad_alloc();
        set.atlas = std::make_unique<TerrainVisual::Surface>(pixels.get(), scale > 1 || reduction > 1);
        pixels.release();
        set.revisions.assign(variants, 0);
        set.opaque.assign(variants, 0);
    }
    const unsigned variant = compositor.catalog().variantIndex(id, recipe.x, recipe.y, recipe.seed);
    const int cx = int(variant) % set.columns * cell, cy = int(variant) / set.columns * cell;
    source = {cx + 1, cy + 1, n, n};
    const auto revision = compositor.textureRevision(id) + 1;
    if (set.revisions[variant] != revision)
    {
        // A uniform, uncalmed cell's pixels depend only on its material and variant.
        auto *atlas = set.atlas->getSDLSurface();
        if (reduction > 1)
        {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> native(
                SDL_CreateSurface(NativeTilePixels, NativeTilePixels, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
            if (!native) throw std::bad_alloc();
            compositor.compose(recipe, native.get(), 0, 0, 1);
            reduceTile(native.get(), atlas, source.x, source.y, reduction);
        }
        else
            compositor.compose(recipe, atlas, source.x, source.y, scale);
        const auto row = [&](int y) { return reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(atlas->pixels) + y * atlas->pitch); };
        for (int y = source.y; y < source.y + n; ++y)
        {
            row(y)[cx] = row(y)[source.x];
            row(y)[source.x + n] = row(y)[source.x + n - 1];
        }
        std::memcpy(row(cy) + cx, row(source.y) + cx, std::size_t(cell) * 4);
        std::memcpy(row(cy + cell - 1) + cx, row(source.y + n - 1) + cx, std::size_t(cell) * 4);
        set.opaque[variant] = allAlpha<255>(*atlas, source);
        set.revisions[variant] = revision;
        set.changed = true;
    }
    return set.opaque[variant] ? &set : nullptr;
}
bool SoftwareTerrainCache::prepareAtResolution(const SceneMap &map, GAGCore::Sprite &, int left,
											   int top, int right, int bottom, int vx, int vy,
											   Uint32 visibleTeams, bool wholeMap, int time,
											   int preferredResolution, bool tiledCapture,
											   int preferredDownsample)
{
	if (registry.get() != &map.terrainRegistry() || assets != map.frozenAssetBundle())
	{
        chunks.clear();
        gpuPages.reset();
        cpuMaterialLease.reset();
        animatedTiles.clear();
        cancelPending();
		registry = map.frozenTerrainRegistry();
        assets = map.frozenAssetBundle();
	}

    if (GAGCore::renderExecution() == GAGCore::RenderExecution::Immediate)
    {
        cpuMaterialLease.reset();
    }
	PERF_SCOPE_TIME(TerrainCache);
	if (resetGeneration != GAGCore::GraphicContext::renderResetGeneration())
	{
		// A renderer reset took the textures of pages whose pixels had gone.
		resetGeneration = GAGCore::GraphicContext::renderResetGeneration();
		std::erase_if(chunks, [](const auto &chunk) { return chunk->image && chunk->image->portableReleased(); });
	}
	copies.clear();
	incomplete = false;
	++frame;
	if (left > right || top > bottom) return true;
    if (!enabled)
    {
        gpuPages.reset();
        chunks.clear();
        cpuMaterialLease.reset();
        cancelPending();
        return false;
    }
	paintBounds = {left * 32, top * 32, (right - left + 1) * 32, (bottom - top + 1) * 32};
	auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
	overlayCompositor = &compositor;
	auto &gfx = *globalContainer->gfx;
	const bool nextGPU = gfx.rendererCapabilities().accelerated;
	// When even native-size pages need reduction, avoid preparing HD
	// materials for the interactive Legacy OpenGL view.
	const bool reducedLegacyView = gfx.isLegacyGLScreen() && !tiledCapture && !preferredDownsample &&
		samplingPlan(map, left, top, right, bottom, vx, vy, 1).downsample > 1;
	const bool portable = portableRenderer();
	const bool highResolution = composeHighResolution(compositor, tiledCapture);
    {
        PERF_SCOPE_TIME(TerrainMaterials);
	// CPU and device composition can rebuild complete pages beyond the
	// viewport, so prepare every cell in those pages before collecting jobs.
	const auto prepareMaterials = [&] {
		compositor.prepareVisible(highResolution, time, map,
			chunkOf(left + vx) * ChunkTiles, chunkOf(top + vy) * ChunkTiles,
			(chunkOf(right + vx) + 1) * ChunkTiles - 1,
			(chunkOf(bottom + vy) + 1) * ChunkTiles - 1, reducedLegacyView,
			// Portable draws decor HD frames only zoomed in past its sprite threshold.
			!portable || GAGCore::Sprite::portableHighResolution());
	};
	prepareMaterials();
	// A capture is drawn once, so it waits for streamed HD rather than
	// keeping the native fallback that interactive frames replace later.
	if (tiledCapture && highResolution)
	{
		GAGCore::Sprite::finishHighResolution();
		prepareMaterials();
	}
    }
	const auto plan =
		viewSamplingPlan(map, left, top, right, bottom, vx, vy,
						 preferredResolution > 0 ? preferredResolution : compositor.scale(),
						 tiledCapture, preferredDownsample);
	if (!plan.viewFits)
	{
        chunks.clear();
        gpuPages.reset();
        cancelPending();
        cpuMaterialLease.reset(); // Release cache storage before the caller streams pages.
		return false;
	}
	const int nextResolution = plan.resolution;
	const auto budget = plan.budget;
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	if (gpu != nextGPU)
	{
		chunks.clear();
        cpuMaterialLease.reset();
        cancelPending();
	}
	resolution = nextResolution;
	gpu = nextGPU;
	downsample = plan.downsample;
	// Once a portable view has settled back on native frames, its HD pages are
	// stale (their materials' revisions moved on) and only hold memory.
	if (portable && composedHighResolution && !highResolution)
		std::erase_if(chunks, [&](const auto &chunk) {
			if (chunk->scale == 1) return false;
			if (gpuPages) gpuPages->erase(*chunk);
			return true;
		});
	composedHighResolution = highResolution;
	const auto storage = pageStorage(resolution, false, downsample);
	const auto uploadReserve = gpu ? pageStorage(resolution, true, downsample) - storage : 0;
    const auto *device = gfx.renderDevice();
    const auto visiblePages = std::uint64_t(std::min(x1-x0+1,std::max(1,map.getW()/ChunkTiles))) *
        std::min(y1-y0+1,std::max(1,map.getH()/ChunkTiles));
    // Preflight the complete view, including retained packet leases, before
    // selecting GPU composition. Oversized views use the CPU page reference.
    const auto coverageReserve = visiblePages * ChunkTiles * ChunkTiles *
        std::uint64_t(32*resolution) * (32*resolution) * 9;
    const auto colorReserve = visiblePages * std::uint64_t(ChunkTiles*32*resolution/downsample) *
        (ChunkTiles*32*resolution/downsample) * 4;
    // Class budgets are independent maxima, not simultaneously available GPU
    // memory. Conservatively reserve the complete visible composition and a
    // bounded upload segment on top of currently resident resources. Cached
    // pages may be counted twice; declining promotion releases those caches and
    // uses the CPU reference until aggregate headroom becomes available again.
    constexpr std::uint64_t deviceBudget = 256u*1024*1024;
    constexpr std::uint64_t uploadReserveBytes = 8u*1024*1024;
    const auto resident = device ? device->telemetry().resourceBytes : 0;
    const auto compositionReserve = coverageReserve + colorReserve + uploadReserveBytes;
    const bool composeOnDevice = device && device->capabilities().terrainComposition &&
        coverageReserve <= GPUMaskBudget && compositionReserve <= deviceBudget &&
        resident <= deviceBudget-compositionReserve;
    const auto generation = gfx.getGLContextGeneration();
	try
	{
    if (bool(gpuPages) != composeOnDevice || (gpuPages &&
        (gpuPageGeneration != generation || gpuPages->registryIdentity() != gfx.renderResources()->instanceIdentity()))) {
        if (gpuPages && (gpuPageGeneration != generation ||
                         gpuPages->registryIdentity() != gfx.renderResources()->instanceIdentity()))
        {
            chunks.clear();
            cpuMaterialLease.reset();
        }
        else {
            // A zoom can change composition admission without invalidating CPU
            // pixels at another density. Retain those bounded cache entries;
            // device-only pages cannot survive destruction of their targets.
            std::erase_if(chunks, [](const auto &chunk) { return !chunk->cpuComposed; });
        }
        gpuPages.reset();
        if (composeOnDevice) gpuPages = std::make_shared<GpuTerrainPageCache>(gfx.renderResourceOwner());
        gpuPageGeneration = generation;
    }
    if (gpuPages)
        cpuMaterialLease.reset();
    if (gpuPages) gpuPages->begin(gfx.renderTargetContentRevision(),frame,x0,y0,x1,y1,map.getMaskW(),map.getMaskH(),resolution,downsample);

		// Protect the complete requested view before admitting its first page.
		// Otherwise a pan can evict a page we are about to visit later this frame.
		const int periodX = std::max(1, map.getW() / ChunkTiles);
		const int periodY = std::max(1, map.getH() / ChunkTiles);
		for (auto &c : chunks)
		{
			const bool selected = c->scale == resolution && c->downsample == downsample &&
				((c->x / ChunkTiles - x0) & (periodX - 1)) <= x1 - x0 &&
				((c->y / ChunkTiles - y0) & (periodY - 1)) <= y1 - y0;
			if (selected) c->used = frame;
		}
		const auto makeRoom = [&](std::size_t needed, std::size_t textureNeeded = 0)
		{
			while (bytes() + needed > budget ||
                (gpu && residentTextureBytes()+textureNeeded > GPUBudget))
			{
				auto oldest = chunks.end();
				// Retire idle renderer storage before discarding any composed pixels.
				if (gpu)
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->used != frame && (*it)->image &&
							(oldest == chunks.end() || (*it)->textureUsed < (*oldest)->textureUsed)) oldest = it;
				if (oldest == chunks.end())
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->used != frame &&
							(oldest == chunks.end() || (*it)->used < (*oldest)->used)) oldest = it;
				if (oldest == chunks.end())
				{
					// Visible CPU pages are protected; their renderer allocations
					// can still be retired and uploaded later during drawing.
					if (!gpu) return false;
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->image && (oldest == chunks.end() ||
							(*it)->textureUsed < (*oldest)->textureUsed)) oldest = it;
					if (oldest == chunks.end()) return false;
				}
				// First retire GPU storage; evict CPU pixels only if still needed.
				if (gpu && (*oldest)->image)
				{
					retireTexture(**oldest);
				}
				else {if(gpuPages)gpuPages->erase(**oldest);chunks.erase(oldest);}
			}
			return true;
		};
		// Allocate scratch pixels only when a reduced tile actually changes. Warm
		// frames reuse pages without allocating or recomposing native pixels.
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> scratch(nullptr,
																			SDL_DestroySurface);
		// Uniform cells of one material/variant share the same filtered pixels.
		// A water phase can change thousands of cells but only sixteen variants;
		// compose/filter each of those once within this capture pass.
		using Pixels = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
		std::vector<std::vector<Pixels>> uniformTiles(compositor.catalog().materials.size());
		const auto copyUniform = [&](const auto &recipe, SDL_Surface *target, int ox, int oy, int n)
		{
			const auto id = recipe.corners[0];
			const auto variant =
				compositor.catalog().variantIndex(id, recipe.x, recipe.y, recipe.seed);
			auto &variants = uniformTiles[id];
			if (variants.empty())
			{
				variants.reserve(compositor.catalog().materials[id].variants.size());
				for (std::size_t i = 0; i < compositor.catalog().materials[id].variants.size(); ++i)
					variants.emplace_back(nullptr, SDL_DestroySurface);
			}
			auto &tile = variants[variant];
			if (!tile)
			{
				tile.reset(SDL_CreateSurface(n, n, SDL_PIXELFORMAT_ARGB8888));
				if (!tile)
					throw std::bad_alloc();
				if (!scratch)
					scratch.reset(SDL_CreateSurface(NativeTilePixels, NativeTilePixels,
													SDL_PIXELFORMAT_ARGB8888));
				if (!scratch)
					throw std::bad_alloc();
				compositor.compose(recipe, scratch.get(), 0, 0, 1);
				reduceTile(scratch.get(), tile.get(), 0, 0, downsample);
			}
			for (int y = 0; y < n; ++y)
				std::memcpy(static_cast<Uint8 *>(target->pixels) + (oy + y) * target->pitch +
								ox * 4,
							static_cast<const Uint8 *>(tile->pixels) + y * tile->pitch, n * 4);
		};
		const auto terrainRevision = map.terrainRevision();
		const int columns = x1 - x0 + 1;
		const auto pageCount = std::size_t(columns) * (y1 - y0 + 1);
		const auto findPage = [&](int cx, int cy) -> Chunk *
		{
			const int wx = (cx * ChunkTiles) & map.getMaskW();
			const int wy = (cy * ChunkTiles) & map.getMaskH();
			for (auto &chunk : chunks)
				if (chunk->x == wx && chunk->y == wy && chunk->scale == resolution &&
					chunk->downsample == downsample)
					return chunk.get();
			return nullptr;
		};
		// Interactive accelerated views compose cold pages on background workers.
		// Reduction from a retained native page stays here: it only filters.
		const bool deferring = deferBuilds && gpu && !gpuPages && !tiledCapture &&
			resolution <= 4 && (downsample == 1 || resolution == 1);
		if (!deferring)
			cancelPending();
		const auto storageFor = [&](int scale, int reduction)
		{
			return pageStorage(scale, false, reduction) +
				(gpu ? pageStorage(scale, true, reduction) - pageStorage(scale, false, reduction) : 0);
		};
		// Finished pages join the cache within a per-frame byte budget: a new
		// page uploads its whole texture on its first draw (16 MiB at four times
		// density) and a refresh copies its changed cells. One always goes.
		if (!pending.empty())
		{
			constexpr std::size_t InstallBytesPerFrame = 8u * 1024 * 1024;
			std::size_t installBytes = 0;
			int installs = 0;
			std::erase_if(pending, [&](const std::shared_ptr<TerrainPendingPage> &page)
			{
				if (!page->done() || installs >= 4)
					return false;
				const std::size_t side = std::size_t(ChunkPixels) * page->scale / page->downsample;
				const std::size_t bytes = page->refresh
					? page->results.size() * (side / ChunkTiles) * (side / ChunkTiles) * 4
					: side * side * 4;
				if (installs && installBytes + bytes > InstallBytesPerFrame)
					return false;
				installBytes += bytes;
				if (page->failed || page->seed != map.terrainSeed())
					return true;
				if (page->refresh)
				{
					Chunk *target = nullptr;
					for (auto &chunk : chunks)
						if (chunk->x == page->x && chunk->y == page->y && chunk->scale == page->scale &&
							chunk->downsample == page->downsample)
							target = chunk.get();
					if (!target || !target->valid || target->tiles != page->tiles)
						return true;
					auto *pixels = target->image ? target->image->getSDLSurface() : target->parked.get();
					if (!pixels)
						return true;
					const int n = 32 * page->scale / page->downsample;
					std::array<bool, ChunkTiles * ChunkTiles> dirty{};
					for (auto &result : page->results)
					{
						const int i = int(result.index);
						for (int row = 0; row < n; ++row)
							std::memcpy(static_cast<Uint8 *>(pixels->pixels) + (i / ChunkTiles * n + row) * pixels->pitch +
											i % ChunkTiles * n * 4,
										result.pixels + std::size_t(row) * n * 4, std::size_t(n) * 4);
						target->opaque[i] = result.opaque;
						target->empty[i] = result.empty;
						dirty[i] = true;
						auto &held = target->masks[i];
						if (!held && result.mask && releaseMasks(result.mask->bytes()))
							held = std::make_unique<HeldMask>(std::move(*result.mask), maskTotal);
					}
					target->materialRevisions = std::move(page->materialRevisions);
					target->surfTime = page->materials->surfTime;
					if (target->image)
						target->image->markPixelsChanged(dirtyRects(dirty, n));
					++installs;
					return true;
				}
				if (!page->pixels)
					return true;
				Chunk *existing = nullptr;
				for (auto &chunk : chunks)
					if (chunk->x == page->x && chunk->y == page->y && chunk->scale == page->scale &&
						chunk->downsample == page->downsample)
						existing = chunk.get();
				if (existing && existing->valid)
					return true;
				if (existing)
					std::erase_if(chunks, [&](const auto &chunk) { return chunk.get() == existing; });
				if (!makeRoom(storageFor(page->scale, page->downsample)))
					return true;
				auto fresh = std::make_unique<Chunk>();
				fresh->x = page->x;
				fresh->y = page->y;
				fresh->scale = page->scale;
				fresh->downsample = page->downsample;
				fresh->seed = page->seed;
				fresh->recipeTerrainRevision = page->terrainRevision;
				fresh->sources = page->sources;
				fresh->tiles = page->tiles;
				for (auto &result : page->results)
				{
					fresh->opaque[result.index] = result.opaque;
					fresh->empty[result.index] = result.empty;
					// Uniform animated cells draw from the shared tiles, so animation
					// never needs the page itself recomposed or uploaded.
					SDL_Rect tileSource;
					const auto &recipe = result.input.recipe;
					fresh->overlay[result.index] = result.input.discovered && recipe.uniform() && !recipe.calmed() &&
						animatedTile(compositor, recipe, tileSource, page->scale, page->downsample);
					if (result.mask && releaseMasks(result.mask->bytes()))
						fresh->masks[result.index] = std::make_unique<HeldMask>(std::move(*result.mask), maskTotal);
				}
				fresh->materialRevisions = std::move(page->materialRevisions);
				fresh->surfTime = page->materials->surfTime;
				for (const auto &tile : fresh->tiles)
					if (tile.discovered)
						fresh->hasShoreline |= hasShoreline(tile.recipe, compositor);
				fresh->parked = std::move(page->pixels);
				fresh->discoveryRevision = page->discoveryRevision;
				fresh->discoveryTeams = page->discoveryTeams;
				fresh->discoveryWhole = page->discoveryWhole;
				fresh->discoveryStamped = true;
				fresh->valid = true;
				fresh->used = frame;
				chunks.push_back(std::move(fresh));
				++rebuilds;
				++installs;
				return true;
			});
		}
		const auto standIn = [&](int wx, int wy) -> Chunk *
		{
			Chunk *best = nullptr;
			double bestDistance = 0;
			const double wanted = std::log2(double(resolution) / downsample);
			for (auto &chunk : chunks)
				if (chunk->x == wx && chunk->y == wy && chunk->valid && chunk->cpuComposed &&
					(chunk->image || chunk->parked) && chunk->seed == map.terrainSeed())
				{
					// The nearest density, preferring the sharper of two equally near.
					const double density = std::log2(double(chunk->scale) / chunk->downsample);
					const double distance = std::abs(density - wanted) - (density > wanted ? 0.25 : 0);
					if (!best || distance < bestDistance)
						best = chunk.get(), bestDistance = distance;
				}
			return best;
		};
		// One snapshot of every material serves all of this frame's launches.
		// The cache keeps the last one, so its texture copies stay shared and a
		// new snapshot copies only textures whose revision moved on.
		if (!deferring)
			workerMaterials.reset();
		std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> snapshot;
		const auto frameMaterials = [&]
		{
			if (!snapshot)
				workerMaterials = snapshot = compositor.cpuMaterials();
			materialsUsed = frame;
			return snapshot;
		};
		const auto launchRefresh = [&](Chunk &entry, const std::array<Tile, ChunkTiles * ChunkTiles> &tiles) -> std::size_t
		{
			const auto materialChanged = [&](TerrainVisual::MaterialId id)
			{
				const auto previous = std::find_if(entry.materialRevisions.begin(), entry.materialRevisions.end(),
					[id](const auto &revision) { return revision.first == id; });
				return previous == entry.materialRevisions.end() ||
					compositor.textureRevision(id) != previous->second;
			};
			auto page = std::make_shared<TerrainPendingPage>();
			page->refresh = true;
			page->x = entry.x;
			page->y = entry.y;
			page->scale = entry.scale;
			page->downsample = entry.downsample;
			page->seed = entry.seed;
			page->terrainRevision = entry.recipeTerrainRevision;
			page->tiles = tiles;
			const int n = 32 * entry.scale / entry.downsample;
			std::vector<unsigned> changed;
			for (unsigned i = 0; i < tiles.size(); ++i)
			{
				const auto &tile = tiles[i];
				// Overlay cells draw from the shared animated tiles instead.
				if (!tile.discovered || entry.overlay[i])
					continue;
				if (std::any_of(tile.recipe.corners.begin(), tile.recipe.corners.end(), materialChanged) ||
					tileSurfChanged(tile.recipe, entry.surfTime, compositor))
					changed.push_back(i);
			}
			// Revisions as of this snapshot; a later step queues another refresh.
			for (const auto &tile : tiles)
			{
				if (!tile.discovered)
					continue;
				std::array<TerrainVisual::MaterialId, 4> palette;
				for (unsigned k = 0, count = tile.recipe.palette(palette); k < count; ++k)
					if (std::none_of(page->materialRevisions.begin(), page->materialRevisions.end(),
									 [&](const auto &revision) { return revision.first == palette[k]; }))
						page->materialRevisions.emplace_back(palette[k], compositor.textureRevision(palette[k]));
			}
			if (changed.empty())
			{
				// Only overlay cells moved on; the page pixels are still current.
				entry.materialRevisions = std::move(page->materialRevisions);
				entry.surfTime = compositor.surfTime();
				return std::size_t(1);
			}
			page->materials = frameMaterials();
			page->cellPixels.resize(changed.size() * std::size_t(n) * n);
			page->pitch = n * 4;
			page->results.reserve(changed.size());
			for (std::size_t k = 0; k < changed.size(); ++k)
			{
				const unsigned i = changed[k];
				TerrainCellResult result{};
				result.index = i;
				result.input = tiles[i];
				result.pixels = reinterpret_cast<Uint8 *>(page->cellPixels.data() + k * std::size_t(n) * n);
				// Workers read the kept coverage, shared, rather than sampling it again.
				const auto &held = entry.masks[i];
				const int maskScale = entry.downsample > 1 ? 1 : entry.scale;
				if (held && held->mask.recipe == tiles[i].recipe && held->mask.scale == maskScale)
				{
					result.held = held;
					result.reused = &held->mask;
				}
				page->results.push_back(std::move(result));
			}
			page->cellsPerJob = 16;
			page->jobs = (page->results.size() + page->cellsPerJob - 1) / page->cellsPerJob;
			page->remaining.store(page->jobs, std::memory_order_relaxed);
			page->wanted = frame;
			pending.push_back(page);
			submitPage(page);
			return changed.size();
		};
		const auto launch = [&](int wx, int wy, const std::array<Tile, ChunkTiles * ChunkTiles> &tiles,
								const std::array<Uint32, SourceSpan * SourceSpan> &sources)
		{
			auto page = std::make_shared<TerrainPendingPage>();
			page->x = wx;
			page->y = wy;
			page->scale = resolution;
			page->downsample = downsample;
			page->seed = map.terrainSeed();
			page->terrainRevision = terrainRevision;
			page->sources = sources;
			page->tiles = tiles;
			for (const auto &tile : tiles)
			{
				if (!tile.discovered)
					continue;
				std::array<TerrainVisual::MaterialId, 4> palette;
				for (unsigned k = 0, n = tile.recipe.palette(palette); k < n; ++k)
					if (std::none_of(page->materialRevisions.begin(), page->materialRevisions.end(),
									 [&](const auto &revision) { return revision.first == palette[k]; }))
						page->materialRevisions.emplace_back(palette[k], compositor.textureRevision(palette[k]));
			}
			page->materials = frameMaterials();
			page->discoveryRevision = map.discoveryRevision();
			page->discoveryTeams = visibleTeams;
			page->discoveryWhole = wholeMap;
			const int size = ChunkPixels * resolution / downsample, n = 32 * resolution / downsample;
			page->pixels.reset(SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888));
			if (!page->pixels)
				throw std::bad_alloc();
			auto *base = static_cast<Uint8 *>(page->pixels->pixels);
			const int pitch = page->pixels->pitch;
			page->pitch = pitch;
			page->results.reserve(tiles.size());
			for (unsigned i = 0; i < tiles.size(); ++i)
			{
				TerrainCellResult result{};
				result.index = i;
				result.input = tiles[i];
				result.pixels = base + (i / ChunkTiles) * n * pitch + (i % ChunkTiles) * n * 4;
				std::array<TerrainVisual::MaterialId, 4> palette;
				const unsigned paletteCount = tiles[i].recipe.palette(palette);
				result.keepMask = tiles[i].discovered && !tiles[i].recipe.uniform() &&
					std::any_of(palette.begin(), palette.begin() + paletteCount,
								[&](auto id) { return compositor.animated(id); });
				page->results.push_back(std::move(result));
			}
			page->cellsPerJob = 16;
			page->jobs = (page->results.size() + page->cellsPerJob - 1) / page->cellsPerJob;
			page->remaining.store(page->jobs, std::memory_order_relaxed);
			page->wanted = frame;
			pending.push_back(page);
			submitPage(page);
		};
		// Graphics-thread work this frame for the workers: cells described or
		// gathered for launches, and reductions from native pages.
		constexpr std::size_t LaunchUnitsPerFrame = 1024;
		constexpr int MaxReductionsPerFrame = 4;
		std::size_t launchUnits = 0;
		int reductionsThisFrame = 0;
		const std::size_t pendingLimit = std::size_t(std::max(1u, BackgroundWork::workers())) * 2 + 2;
		// In-flight buffers are bounded in bytes too: at four times density a
		// page is 16 MiB. One launch always goes when nothing is in flight.
		constexpr std::size_t PendingBytes = 64u * 1024 * 1024;
		const auto pendingBytes = [&]
		{
			std::size_t total = 0;
			for (const auto &page : pending)
				total += page->pixels ? std::size_t(page->pixels->pitch) * page->pixels->h
									  : page->cellPixels.size() * sizeof(Uint32);
			return total;
		};
		const auto roomFor = [&](std::size_t bytes) { return pending.empty() || pendingBytes() + bytes <= PendingBytes; };
		const std::size_t pageBytes = std::size_t(ChunkPixels * resolution / downsample) * (ChunkPixels * resolution / downsample) * 4;
		const auto refreshesPending = [&]
		{ return std::size_t(std::count_if(pending.begin(), pending.end(), [](const auto &page) { return page->refresh; })); };
		// Whether a missing page found no slot this frame; refreshes then wait.
		bool waiting = false;
		// Visit the view's centre first so workers fill it before its edges.
		std::vector<std::size_t> order(pageCount);
		for (std::size_t i = 0; i < pageCount; ++i)
			order[i] = i;
		if (deferring)
		{
			const double centreX = (left + right + 1) * 0.5 + vx, centreY = (top + bottom + 1) * 0.5 + vy;
			const auto distance = [&](std::size_t i)
			{
				const double dx = (x0 + int(i % columns)) * ChunkTiles + ChunkTiles * 0.5 - centreX;
				const double dy = (y0 + int(i / columns)) * ChunkTiles + ChunkTiles * 0.5 - centreY;
				return dx * dx + dy * dy;
			};
			std::stable_sort(order.begin(), order.end(),
							 [&](std::size_t a, std::size_t b) { return distance(a) < distance(b); });
		}
		for (const std::size_t i : order)
			{
				const int cx = x0 + int(i % columns), cy = y0 + int(i / columns);
				const int wx = (cx * ChunkTiles) & map.getMaskW();
				const int wy = (cy * ChunkTiles) & map.getMaskH();
				Chunk *entry = findPage(cx, cy);
				// Animation and artwork revisions change pixels but keep their
				// recipes. The terrain revision avoids scanning the vertex
				// window on ordinary frames.
				const bool sameTerrain = entry && entry->valid &&
					entry->recipeTerrainRevision == terrainRevision && entry->seed == map.terrainSeed();
				// Compare the complete vertex window on edits, including the
				// halo needed by borders and shore calm.
				std::array<Uint32, SourceSpan * SourceSpan> sources{};
				if (!sameTerrain)
				{
					for (int y = -SourceHalo; y <= ChunkTiles + SourceHalo; ++y)
						for (int x = -SourceHalo; x <= ChunkTiles + SourceHalo; ++x)
							sources[(y + SourceHalo) * SourceSpan + x + SourceHalo] = map.vertexTerrainAt(wx + x, wy + y);
				}
				const auto revisionChanged = [&](const auto &revision)
				{ return compositor.textureRevision(revision.first) != revision.second; };
				const auto pageChanged = [&](const Chunk &page)
				{
					return std::any_of(page.materialRevisions.begin(), page.materialRevisions.end(),
						[&](const auto &revision) {
							return (revisionChanged(revision) || (page.hasShoreline &&
								surfStepChanged(revision.first, page.surfTime, compositor)));
						});
				};
				bool sameContent = entry && entry->valid && (!gpuPages || gpuPages->find(*entry)) &&
								 (sameTerrain || entry->sources == sources) &&
								 entry->seed == map.terrainSeed();
				const bool artChanged = sameContent && pageChanged(*entry);
				const bool discoveryCurrent = entry && entry->discoveryStamped &&
					entry->discoveryWhole == wholeMap &&
					(wholeMap || (entry->discoveryRevision == map.discoveryRevision() &&
								  entry->discoveryTeams == visibleTeams));
				if (sameContent && !artChanged && !discoveryCurrent)
					for (int y = 0; y < ChunkTiles && sameContent; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							const bool discovered =
								wholeMap ||
								map.isMapPartiallyDiscovered(wx + x - 1, wy + y - 1, wx + x + 1,
															 wy + y + 1, visibleTeams);
							if (discovered != entry->tiles[y * ChunkTiles + x].discovered)
							{
								sameContent = false;
								break;
							}
						}
				const auto stampDiscovery = [&](auto &target)
				{
					target.discoveryRevision = map.discoveryRevision();
					target.discoveryTeams = visibleTeams;
					target.discoveryWhole = wholeMap;
				};
				const bool unchanged = sameContent && !artChanged;
				if (!unchanged && entry && entry->image && entry->image->portableReleased())
				{
					// Its pixels went after upload: a change composes the page again.
					entry->valid = false;
					entry->image.reset();
					const int side = ChunkPixels * entry->scale / entry->downsample;
					entry->parked.reset(SDL_CreateSurface(side, side, SDL_PIXELFORMAT_ARGB8888));
					if (!entry->parked) throw std::bad_alloc();
				}
				if (unchanged)
				{
					stampDiscovery(*entry);
					entry->discoveryStamped = true;
					entry->used = frame;
					entry->recipeTerrainRevision = terrainRevision;
					++hits;
					copies.push_back(
						{entry, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
					continue;
				}
				Chunk *reductionSource = nullptr;
				const auto describeTiles = [&](std::array<Tile, ChunkTiles * ChunkTiles> &tiles)
				{
					if (sameTerrain && discoveryCurrent)
						tiles = entry->tiles;
					else
					for (int y = 0; y < ChunkTiles; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							const int index = y * ChunkTiles + x;
							auto &t = tiles[index];
							t.discovered = wholeMap || map.isMapPartiallyDiscovered(
														   wx + x - 1, wy + y - 1, wx + x + 1,
														   wy + y + 1, visibleTeams);
							if (t.discovered)
							{
								t.recipe = sameTerrain && entry->tiles[index].discovered
									? entry->tiles[index].recipe : compositor.describe(map, wx + x, wy + y);
							}
						}
				};
				const auto pendingFor = [&](int scale, int reduction) -> TerrainPendingPage *
				{
					for (auto &page : pending)
						if (page->x == wx && page->y == wy && page->scale == scale && page->downsample == reduction)
							return page.get();
					return nullptr;
				};
				// A cold page in an interactive view: a retained native page reduces
				// here, a few per frame; otherwise workers compose it, launched within
				// the frame's budget, and the view draws a stand-in meanwhile.
				if (deferring && (!entry || !entry->valid))
				{
					reductionSource = nullptr;
					if (downsample > 1 && reductionsThisFrame < MaxReductionsPerFrame)
						for (auto &c : chunks)
							if (c->x == wx && c->y == wy && c->scale == 1 && c->downsample == 1 && c->valid &&
								c->seed == map.terrainSeed() && c->recipeTerrainRevision == terrainRevision &&
								c->discoveryStamped && c->discoveryWhole == wholeMap &&
								(wholeMap || (c->discoveryRevision == map.discoveryRevision() &&
											  c->discoveryTeams == visibleTeams)) &&
								!pageChanged(*c))
								reductionSource = c.get();
					if (reductionSource)
						++reductionsThisFrame;
					else
					{
						if (auto *queued = pendingFor(resolution, downsample))
							queued->wanted = frame;
						else if (launchUnits + ChunkTiles * ChunkTiles <= LaunchUnitsPerFrame &&
								 pending.size() < pendingLimit && roomFor(pageBytes))
						{
							std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
							describeTiles(tiles);
							launch(wx, wy, tiles, sources);
							launchUnits += ChunkTiles * ChunkTiles;
						}
						else
							waiting = true;
						if (Chunk *substitute = standIn(wx, wy))
						{
							substitute->used = frame;
							copies.push_back({substitute, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
						}
						else
							incomplete = true;
						continue;
					}
				}
				std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
				// A reduction's native page already holds this view's current tiles.
				if (reductionSource)
					tiles = reductionSource->tiles;
				else
					describeTiles(tiles);
				// Reduced pages average native composition. A retained native
				// page is already that exact input, including coverage and alpha;
				// do not sample every contour again just to change zoom density.
				Chunk *native = nullptr;
				std::uint64_t nativeUsed = 0;
				if (downsample > 1 && !gpuPages)
					for (auto &c : chunks)
						if (c->x == wx && c->y == wy && c->scale == 1 && c->downsample == 1 &&
							c->valid && c->tiles == tiles &&
							!pageChanged(*c))
						{
							native = c.get();
							nativeUsed = native->used;
							// Admission may evict idle pages; retain this input until
							// its reduction has completed.
							native->used = frame;
							break;
						}
				// An animation step of a valid page: workers recompose its changed
				// cells while the view keeps drawing the current pixels.
				if (deferring && entry && entry->valid && entry->cpuComposed && artChanged && entry->tiles == tiles)
				{
					// Missing pages come first: animation would otherwise keep every
					// slot busy and leave pages away from the centre unbuilt.
					if (auto *queued = pendingFor(entry->scale, entry->downsample))
						queued->wanted = frame;
					else if (!coldBacklog && launchUnits < LaunchUnitsPerFrame &&
							 refreshesPending() < pendingLimit / 2 && pending.size() < pendingLimit &&
							 roomFor(pageBytes / 4))
						launchUnits += launchRefresh(*entry, tiles);
					stampDiscovery(*entry);
					entry->discoveryStamped = true;
					entry->used = frame;
					copies.push_back({entry, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
					continue;
				}
				if (!entry)
				{
					if (!makeRoom(storage + uploadReserve))
					{
						// Native reuse is optional. Do not reject a view that fits
						// only after its idle reduction input is evicted.
						if (native)
						{
							native->used = nativeUsed;
							native = nullptr;
						}
						if (!makeRoom(storage + uploadReserve)) throw std::bad_alloc();
					}
					auto fresh = std::make_unique<Chunk>();
					fresh->x = wx;
					fresh->y = wy;
					fresh->scale = resolution;
					fresh->downsample = downsample;
					std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
						SDL_CreateSurface(ChunkPixels * resolution / downsample,
										  ChunkPixels * resolution / downsample,
										  SDL_PIXELFORMAT_ARGB8888),
						SDL_DestroySurface);
					if (!pixels)
						throw std::bad_alloc();
					if (gpu) fresh->parked = std::move(pixels);
					else
					{
						fresh->image = std::make_unique<TerrainVisual::Surface>(pixels.get(), false);
						pixels.release(); // Keep ownership if the drawable allocation fails.
					}
					entry = fresh.get();
					chunks.push_back(std::move(fresh));
				}
				entry->used = frame;
				const auto materialChanged = [&](TerrainVisual::MaterialId id)
				{
					const auto previous = std::find_if(
						entry->materialRevisions.begin(), entry->materialRevisions.end(),
						[id](const auto &revision) { return revision.first == id; });
					return previous == entry->materialRevisions.end() || revisionChanged(*previous);
				};
				const bool sourcesChanged = pageChanged(*entry);
				if (!entry->valid || sourcesChanged || entry->tiles != tiles || (gpuPages && !gpuPages->find(*entry)))
				{
					auto *target = entry->image ? entry->image->getSDLSurface() : entry->parked.get();
					// A page retired after the portable renderer released its pixels
					// keeps neither an image nor parked pixels: compose into new ones.
					if (!target)
					{
						entry->valid = false;
						entry->image.reset();
						const int side = ChunkPixels * entry->scale / entry->downsample;
						entry->parked.reset(SDL_CreateSurface(side, side, SDL_PIXELFORMAT_ARGB8888));
						if (!entry->parked) throw std::bad_alloc();
						target = entry->parked.get();
					}
					const bool wasValid = entry->valid;
					// Cells written by this rebuild; an uploaded page updates only those.
					std::array<bool, ChunkTiles * ChunkTiles> dirty{};
					entry->valid = false;
					entry->opaqueRuns.clear();
                    if (gpuPages) {
                        const auto [retainedReserve,textureReserve]=gpuPages->compositionReservation(*entry);
                        if(!makeRoom(retainedReserve,textureReserve))throw std::bad_alloc();
                        if(!gpuPages->compose(gfx,compositor,*entry,tiles)) {
                            // Admission can progress next turn in these same targets.
                            copies.clear();return false;
                        }
                    }
                    else if (!tryBuildCpuPage(*entry, tiles, compositor, target, wasValid, budget, native != nullptr,
                                              &dirty))
                    {
                        ++workMetrics.serialPages;
                        PERF_SCOPE_TIME(TerrainBatch);
                        for (int y = 0; y < ChunkTiles; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							const int i = y * ChunkTiles + x, n = 32 * resolution / downsample;
							// Device pages draw uniform animated cells from shared tiles
							// at their density; the page keeps an earlier frame below.
							SDL_Rect tileSource;
							const bool overlay = gpu && tiles[i].discovered &&
								tiles[i].recipe.uniform() && !tiles[i].recipe.calmed() &&
								animatedTile(compositor, tiles[i].recipe, tileSource, resolution, downsample);
							if (wasValid && overlay && entry->overlay[i] && entry->tiles[i] == tiles[i])
								continue;
							if (wasValid && entry->tiles[i] == tiles[i] &&
								(!tiles[i].discovered ||
								 std::none_of(tiles[i].recipe.corners.begin(),
											  tiles[i].recipe.corners.end(), materialChanged) &&
                                 !tileSurfChanged(tiles[i].recipe, entry->surfTime, compositor)))
								continue;
							dirty[i] = true;
							entry->overlay[i] = overlay;
							auto &held = entry->masks[i];
							const auto &recipe = tiles[i].recipe;
							const int maskScale = downsample > 1 ? 1 : resolution;
							if (held && (!tiles[i].discovered || held->mask.recipe != recipe ||
										 held->mask.scale != maskScale))
								held.reset();
							std::optional<TerrainVisual::Compositor::CellMask> passing;
							const TerrainVisual::Compositor::CellMask *cellMask =
								held ? &held->mask : nullptr;
							// The palette, as in tryBuildCpuPage: halo surf animates too.
							std::array<TerrainVisual::MaterialId, 4> palette;
							const unsigned paletteCount = recipe.palette(palette);
							if (tiles[i].discovered && !native && !cellMask && !recipe.uniform() &&
								std::any_of(palette.begin(), palette.begin() + paletteCount,
											[&](auto id) { return compositor.animated(id); }))
							{
								passing = compositor.mask(recipe, maskScale);
								if (releaseMasks(passing->bytes()))
								{
									held = std::make_unique<HeldMask>(std::move(*passing), maskTotal);
									passing.reset();
									cellMask = &held->mask;
								}
								else
									cellMask = &*passing;
							}
							if (tiles[i].discovered)
							{
								// Calmed shore cells vary within a variant; compose those.
								if (gpu && downsample > 1 && !native && !recipe.calmed() && recipe.uniform())
									copyUniform(recipe, target, x * n, y * n, n);
								else if (downsample > 1)
								{
									if (native)
									{
										auto *pixels = native->image ? native->image->getSDLSurface() : native->parked.get();
										auto tile = *pixels;
										tile.pixels = static_cast<Uint8 *>(pixels->pixels) + y * 32 * pixels->pitch + x * 32 * 4;
										reduceTile(&tile, target, x * n, y * n, downsample);
									}
									else
									{
										if (!scratch) scratch.reset(SDL_CreateSurface(NativeTilePixels,
																		NativeTilePixels,
																		SDL_PIXELFORMAT_ARGB8888));
										if (!scratch)
											throw std::bad_alloc();
										compositor.compose(recipe, scratch.get(), 0, 0, 1, cellMask);
										reduceTile(scratch.get(), target, x * n, y * n, downsample);
									}
								}
								else
								{
									// The page already holds this cell's previous composition.
									std::optional<TerrainVisual::Compositor::Refresh> refresh;
									if (wasValid && entry->tiles[i] == tiles[i] && cellMask)
									{
										refresh = cellRefresh(recipe, materialChanged, entry->surfTime, compositor);
										// An animation step visits only the samples it can change.
										if (held && cellMask == &held->mask &&
											!((refresh->textures | refresh->surf) & ~compositor.animatedSlots(held->mask)))
											refresh->samples = &held->animatedSamples(compositor);
									}
									compositor.compose(recipe, target, x * n, y * n, resolution,
													   cellMask, refresh ? &*refresh : nullptr);
								}
							}
							else
							{
								SDL_Rect area{x * n, y * n, n, n};
								SDL_FillSurfaceRect(target, &area, 0);
							}
							// Opacity runs are consumed only by the software draw path.
							if (!gpu)
							{
                                std::tie(entry->opaque[i], entry->empty[i]) =
                                    classifyAlpha(*target, {x * n, y * n, n, n});
							}
						}
                    }
					if (entry->image && !gpuPages)
					{
						if (!wasValid || std::all_of(dirty.begin(), dirty.end(), [](bool cell) { return cell; }))
							entry->image->markPixelsChanged();
						else if (std::any_of(dirty.begin(), dirty.end(), [](bool cell) { return cell; }))
							entry->image->markPixelsChanged(dirtyRects(dirty, ChunkPixels * resolution / downsample / ChunkTiles));
					}
					entry->tiles = tiles;
					stampDiscovery(*entry);
					entry->discoveryStamped = true;
					entry->materialRevisions.clear();
					entry->surfTime = compositor.surfTime();
					entry->hasShoreline = false;
					for (const auto &tile : tiles)
					{
						if (!tile.discovered)
							continue;
						entry->hasShoreline |= hasShoreline(tile.recipe, compositor);
						std::array<TerrainVisual::MaterialId, 4> palette;
						for (unsigned k = 0, n = tile.recipe.palette(palette); k < n; ++k)
						{
							const auto id = palette[k];
							if (std::none_of(entry->materialRevisions.begin(), entry->materialRevisions.end(),
										 [id](const auto &revision) { return revision.first == id; }))
								entry->materialRevisions.emplace_back(id, compositor.textureRevision(id));
						}
					}
					if (!gpu)
						buildOpaqueRuns(*entry);
					entry->valid = true;
					entry->cpuComposed = !gpuPages;
					++rebuilds;
					if (native) ++reductions;
				}
				else
					++hits;
				if (!sameTerrain) entry->sources = sources;
				entry->seed = map.terrainSeed();
				entry->recipeTerrainRevision = terrainRevision;
				if (native) native->used = nativeUsed;
				copies.push_back({entry, (cx * ChunkTiles - vx) * 32, (cy * ChunkTiles - vy) * 32});
			}
		coldBacklog = waiting;
		// A view that has stopped launching work lets its snapshot's texture
		// copies go, and densities it left let their animated tiles go.
		if (pending.empty() && frame > materialsUsed + 120)
			workerMaterials.reset();
		std::erase_if(animatedTiles, [&](const auto &entry) { return entry.second.used + 600 < frame; });
		// Pages the view has not drawn or stood in with for about ten seconds go:
		// missing pages compose in the background, so other zoom densities need
		// not stay resident.
		if (deferring)
			std::erase_if(chunks, [&](const auto &chunk) { return chunk->used + 600 < frame; });
		// Pages the view has left for a few frames are no longer worth composing.
		std::erase_if(pending, [&](const std::shared_ptr<TerrainPendingPage> &page)
		{
			if (page->wanted + 8 >= frame)
				return false;
			page->cancelled.store(true, std::memory_order_relaxed);
			return true;
		});
		for (auto &[key, set] : animatedTiles)
			if (set.changed)
			{
				set.atlas->markPixelsChanged();
				set.changed = false;
			}
		if (gpu && !gpuPages &&
			std::any_of(copies.begin(), copies.end(), [](const auto &copy) { return !copy.chunk->image; }) &&
			!makeRoom(uploadReserve))
			throw std::bad_alloc();
	}
	catch (const std::bad_alloc &)
	{
		copies.clear();
        chunks.clear();
        gpuPages.reset();
        cpuMaterialLease.reset();
        cancelPending();
		return false;
	}
	return true;
}
void SoftwareTerrainCache::drawUncached(const SceneMap &map, GAGCore::Sprite &sprite, int left,
										int top, int right, int bottom, int vx, int vy,
										Uint32 visibleTeams, bool wholeMap, int time,
										FallbackMode mode, bool tiledCapture)
{
	auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
	const bool highResolution = composeHighResolution(compositor, tiledCapture);
	const auto prepareMaterials = [&] {
		compositor.prepareVisible(highResolution, time, map,
			chunkOf(left + vx) * ChunkTiles, chunkOf(top + vy) * ChunkTiles,
			(chunkOf(right + vx) + 1) * ChunkTiles - 1,
			(chunkOf(bottom + vy) + 1) * ChunkTiles - 1, false, !portableRenderer());
	};
	prepareMaterials();
	if (tiledCapture && highResolution)
	{
		GAGCore::Sprite::finishHighResolution();
		prepareMaterials();
	}
	const auto plan =
		viewSamplingPlan(map, left, top, right, bottom, vx, vy, compositor.scale(), tiledCapture);
	if (mode == FallbackMode::EmergencyTiles || !plan.pageFits)
	{
		drawEmergencyTiles(map, left, top, right, bottom, vx, vy, visibleTeams, wholeMap,
						   plan.resolution);
		return;
	}
	const int x0 = chunkOf(left + vx), x1 = chunkOf(right + vx);
	const int y0 = chunkOf(top + vy), y1 = chunkOf(bottom + vy);
	for (int cy = y0; cy <= y1; ++cy)
		for (int cx = x0; cx <= x1; ++cx)
		{
			const int pageLeft = std::max(left, cx * ChunkTiles - vx);
			const int pageTop = std::max(top, cy * ChunkTiles - vy);
			const int pageRight = std::min(right, (cx + 1) * ChunkTiles - vx - 1);
			const int pageBottom = std::min(bottom, (cy + 1) * ChunkTiles - vy - 1);
			// Exactly one temporary page is alive. Reuse preparation/drawing so
			// software run sampling and GPU mip neighborhoods match the cache.
			SoftwareTerrainCache page;
			if (page.prepareAtResolution(map, sprite, pageLeft, pageTop, pageRight, pageBottom, vx,
										 vy, visibleTeams, wholeMap, time, plan.resolution, false,
										 plan.downsample))
				page.draw(*globalContainer->gfx);
			else
				drawEmergencyTiles(map, pageLeft, pageTop, pageRight, pageBottom, vx, vy,
								   visibleTeams, wholeMap, plan.resolution);
		}
}
void SoftwareTerrainCache::draw(GAGCore::GraphicContext &target)
{
	for (const auto &copy : copies)
	{
        if (gpuPages) {
            auto page = gpuPages->find(*copy.chunk);
            if (!page) throw std::logic_error("A protected GPU terrain page was evicted");
            SDL_Rect area{copy.x,copy.y,ChunkPixels,ChunkPixels},visible;
            if (SDL_GetRectIntersection(&area,&paintBounds,&visible)) {
                GAGCore::DrawPacket packet;packet.kind=GAGCore::DrawPacket::Kind::Blit;
                packet.texture=std::move(page);packet.blend=GAGCore::DrawPacket::Blend::Premultiplied;
                packet.linear=copy.chunk->scale>1 || copy.chunk->downsample>1;
                packet.source={(visible.x-area.x)*resolution/downsample,(visible.y-area.y)*resolution/downsample,
                    visible.w*resolution/downsample,visible.h*resolution/downsample};
                packet.destination={float(visible.x),float(visible.y),float(visible.w),float(visible.h)};
                target.submitDevicePacket(std::move(packet));
            }
            continue;
        }
		if (gpu)
		{
			if (!copy.chunk->image && !copy.chunk->parked)
				continue; // Lost with its texture this frame; composed again next.
			if (!copy.chunk->image)
			{
				const auto needed = pageStorage(copy.chunk->scale, true, copy.chunk->downsample) -
					pageStorage(copy.chunk->scale, false, copy.chunk->downsample);
				bool room = true;
				while (bytes() + needed > GPUCacheBudget || residentTextureBytes() + needed > GPUBudget)
				{
					Chunk *oldest = nullptr;
					for (auto &chunk : chunks)
						if (chunk->image && (!oldest ||
							(chunk->used != frame && oldest->used == frame) ||
							((chunk->used == frame) == (oldest->used == frame) &&
							 chunk->textureUsed < oldest->textureUsed)))
							oldest = chunk.get();
					if (oldest && oldest->used != frame)
					{
						retireTexture(*oldest);
						continue;
					}
					// Pages this frame does not draw that hold only pixels, such as
					// HD pages parked before a zoom out, go before a drawn texture.
					auto idle = chunks.end();
					if (bytes() + needed > GPUCacheBudget)
						for (auto it = chunks.begin(); it != chunks.end(); ++it)
							if ((*it)->used != frame && !(*it)->image &&
								(idle == chunks.end() || (*it)->used < (*idle)->used))
								idle = it;
					if (idle != chunks.end())
						chunks.erase(idle);
					else if (oldest)
						retireTexture(*oldest);
					else
					{
						room = false;
						break;
					}
				}
				// Every remaining page is drawn this frame: this one waits.
				if (!room)
					continue;
				copy.chunk->image = std::make_unique<TerrainVisual::Surface>(copy.chunk->parked.get(),
					copy.chunk->scale > 1 || copy.chunk->downsample > 1);
				copy.chunk->parked.release();
			}
			copy.chunk->textureUsed = ++textureUse;
			SDL_Rect area{copy.x, copy.y, ChunkPixels, ChunkPixels}, visible;
			if (SDL_GetRectIntersection(&area, &paintBounds, &visible))
			{
				// Stand-in pages can have another density than the view.
				const int scale = copy.chunk->scale, reduction = copy.chunk->downsample;
				const auto draw = [&](SDL_Rect rect)
				{
					target.drawSurface(rect.x, rect.y, rect.w, rect.h, copy.chunk->image.get(),
						(rect.x - area.x) * scale / reduction, (rect.y - area.y) * scale / reduction,
						rect.w * scale / reduction, rect.h * scale / reduction);
				};
                draw(visible);
				// Native pages stay readable for reductions; others keep their
				// pixels only while animation writes them.
				if (deferBuilds && !(copy.chunk->scale == 1 && copy.chunk->downsample == 1) &&
					copy.chunk->image->hasPixels() && overlayCompositor &&
					!needsPagePixels(*copy.chunk, *overlayCompositor))
					copy.chunk->image->releasePortablePixels();
				// Uniform animated cells over the page, from their shared tiles.
				if (overlayCompositor)
					for (int i = 0; i < ChunkTiles * ChunkTiles; ++i)
					{
						if (!copy.chunk->overlay[i]) continue;
						SDL_Rect cell{copy.x + i % ChunkTiles * 32, copy.y + i / ChunkTiles * 32, 32, 32}, shown, source;
						if (!SDL_GetRectIntersection(&cell, &paintBounds, &shown)) continue;
						auto *set = animatedTile(*overlayCompositor, copy.chunk->tiles[i].recipe, source, scale, reduction);
						if (!set) continue;
						if (set->changed)
						{
							set->atlas->markPixelsChanged();
							set->changed = false;
						}
						target.drawSurface(shown.x, shown.y, shown.w, shown.h, set->atlas.get(),
							source.x + (shown.x - cell.x) * scale / reduction, source.y + (shown.y - cell.y) * scale / reduction,
							shown.w * scale / reduction, shown.h * scale / reduction);
					}
			}
			continue;
		}
		for (const auto &run : copy.chunk->opaqueRuns)
		{
			SDL_Rect destination{copy.x + run.rect.x, copy.y + run.rect.y, run.rect.w, run.rect.h},
				visible;
			if (SDL_GetRectIntersection(&destination, &paintBounds, &visible))
				target.drawSurface(visible.x, visible.y, run.image.get(), visible.x - destination.x,
								   visible.y - destination.y, visible.w, visible.h);
		}
		for (int y = 0; y < ChunkTiles; ++y)
			for (int x = 0; x < ChunkTiles; ++x)
			{
				if (copy.chunk->opaque[y * ChunkTiles + x] || copy.chunk->empty[y * ChunkTiles + x])
					continue;
				SDL_Rect area{copy.x + x * 32, copy.y + y * 32, 32, 32}, visible;
				if (SDL_GetRectIntersection(&area, &paintBounds, &visible))
					target.drawSurface(visible.x, visible.y, copy.chunk->image.get(),
									   x * 32 + visible.x - area.x, y * 32 + visible.y - area.y,
									   visible.w, visible.h);
			}
	}
}
bool SoftwareTerrainCache::releaseMasks(std::size_t needed)
{
	std::size_t budget = gpu ? GPUMaskBudget : MaskBudget;
#if !defined(GLOB2_MOBILE) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
	// Reduced desktop views cover many more mixed animated cells. Keep their
	// native coverage too; otherwise every phase samples those contours again.
	if (gpu && downsample > 1) budget = GPUBudget;
#endif
	if (needed > budget)
		return false;
	if (*maskTotal + needed <= budget)
		return true;
	std::vector<Chunk *> idle;
	for (auto &chunk : chunks)
		if (chunk->used != frame)
			idle.push_back(chunk.get());
	std::sort(idle.begin(), idle.end(), [](auto *a, auto *b) { return a->used < b->used; });
	for (auto *chunk : idle)
	{
		if (*maskTotal + needed <= budget)
			break;
		for (auto &held : chunk->masks)
			held.reset();
	}
	return *maskTotal + needed <= budget;
}
std::size_t SoftwareTerrainCache::bytes() const
{
    std::size_t total = (gpuPages ? gpuPages->retainedBytes() : 0) +
                        (cpuMaterialLease ? cpuMaterialLease->bytes : 0);
	for (const auto &chunk : chunks)
	{
		total += pageStorage(chunk->scale, gpu && bool(chunk->image), chunk->downsample);
		// A released page holds its texture alone.
		if (chunk->image && chunk->image->portableReleased())
		{
			const std::size_t side = std::size_t(ChunkPixels) * chunk->scale / chunk->downsample;
			total -= side * side * 4;
		}
	}
	return total;
}

std::size_t SoftwareTerrainCache::residentTextureBytes() const
{
	std::size_t total = gpuPages ? gpuPages->textureBytes() : 0;
	if (gpu)
		for (const auto &chunk : chunks)
			if (chunk->image)
				total += pageStorage(chunk->scale, true, chunk->downsample) -
					pageStorage(chunk->scale, false, chunk->downsample);
	return total;
}
