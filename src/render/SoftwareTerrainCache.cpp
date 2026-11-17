// SPDX-License-Identifier: GPL-3.0-or-later
#include <PresentationWork.h>
#include <chrono>
#include <cstdio>
#include "SoftwareTerrainCache.h"
#include "GlobalContainer.h"
#include "scene/SceneMap.h"
#include "terrain/TerrainCompositor.h"
#include <PerformanceTelemetry.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <cstring>
#include <limits>
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
    static constexpr std::size_t MaskBudget=8u*1024*1024,MaxSources=1024;
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
        const auto dimension=std::size_t(chunk.pixels());
        const auto pixels=pages.contains(key(chunk))?0:dimension*dimension*4;
        // Submitted masks can outlive lookup eviction. Reserve their complete
        // bounded ownership allowance before adding packets to this frame.
        return {pixels+MaskLeaseBudgetBytes-localMaskLeases->bytes.load(),pixels};
    }
    void erase(const SoftwareTerrainCache::Chunk &chunk) {
        auto found=pages.find(key(chunk));if(found==pages.end())return;
        pageBytes-=found->second.pixels->bytes;resources->erase(found->second.handle);pages.erase(found);
    }
    void begin(std::uint64_t revision,std::uint64_t tick,int x0,int y0,int x1,int y1,int maskW,int maskH,int scale,int reduction,int pageTiles) {
        if(contentRevision!=revision) {
            for(auto &[key,page]:pages) page.ready=false;
            contentRevision=revision;
        }
        frame=tick;protectedPages.clear();
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x)
            protectedPages.emplace((x*pageTiles)&maskW,(y*pageTiles)&maskH,scale,reduction);
    }
    GAGCore::TextureSnapshot find(const SoftwareTerrainCache::Chunk &chunk) {
        const auto found=pages.find(key(chunk));if(found==pages.end() || !found->second.ready)return {};
        found->second.used=frame;return found->second.pixels;
    }
    bool compose(GAGCore::GraphicContext &gfx,TerrainVisual::Compositor &compositor,
                 SoftwareTerrainCache::Chunk &chunk,const std::array<SoftwareTerrainCache::Tile,256> &tiles) {
        using namespace GAGCore;
        const int composition=32*chunk.scale,cellSize=composition/chunk.downsample;
        const int dimension=chunk.span*cellSize;
        // Recompose unpublished pixels in the same allocation; replacing its
        // handle would restart a deferred upload on every retry.
        auto existing=pages.find(key(chunk));
        auto target=existing==pages.end()?TextureSnapshot{}:existing->second.pixels;
        if(!target) {
            const auto bytes=std::size_t(dimension)*dimension*4;
            // The device's TerrainPage class (smaller on phones).
            const auto pageBudget=resources->classLimit(TextureDescriptor::BudgetClass::TerrainPage);
            while(pageBytes+bytes>pageBudget) {
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
        for(int y=0;y<chunk.span;++y) for(int x=0;x<chunk.span;++x) {
            const auto &tile=tiles[y*SoftwareTerrainCache::ChunkTiles+x];if(!tile.discovered)continue;
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
// Pages composed ahead of the camera may displace only pages the view has not
// drawn for this many frames (a second at 60 frames a second).
constexpr std::uint64_t AheadIdleFrames = 60;
// How far ahead of the camera's recent motion pages compose, in frames.
constexpr double LookaheadFrames = 30;
int chunkOf(int tile, int pageTiles = SoftwareTerrainCache::ChunkTiles)
{
	return int(std::floor(double(tile) / pageTiles));
}
bool portableRenderer()
{
	return globalContainer->gfx->backendSwitchStatus().active == GAGCore::RendererBackend::PortableSDL;
}
std::size_t pageStorage(int scale, bool gpu, int downsample = 1)
{
	// Retain the fixed recipe/run allowance at every density. GPU accounting
	// also reserves the texture alongside the CPU surface, and its mips on
	// renderers that build them; the portable renderer's page textures have none.
	const std::size_t pixels =
		std::size_t(SoftwareTerrainCache::pageTilesAt(scale)) * 32 * scale / downsample;
	return SoftwareTerrainCache::ChunkStorageBytes -
		   SoftwareTerrainCache::ChunkPixels * SoftwareTerrainCache::ChunkPixels * 4 +
		   pixels * pixels * 4 * (gpu ? (portableRenderer() ? 2 : 3) : 1);
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
// The cells of a copy's page it draws: its clip within the page, in cells.
SDL_Rect drawnCells(const SoftwareTerrainCache::Copy &copy)
{
	const int span = copy.chunk->span;
	const int x0 = std::clamp(copy.clip.x / 32, 0, span), y0 = std::clamp(copy.clip.y / 32, 0, span);
	const int x1 = std::clamp((copy.clip.x + copy.clip.w) / 32, x0, span);
	const int y1 = std::clamp((copy.clip.y + copy.clip.h) / 32, y0, span);
	return {x0, y0, x1 - x0, y1 - y0};
}
// Whether this view's pages compose HD material frames. Accelerated renderers
// do, except the portable SDL renderer at one screen pixel per logical pixel
// or fewer (Compositor::highResolutionPages, the HD sprite policy's
// threshold), where HD pages only cost. The software renderer composes native
// pages: its CPU composition holds no HD page budget. Captures draw once at
// their own density and keep HD.
bool composeHighResolution(TerrainVisual::Compositor &compositor, bool tiledCapture)
{
	auto &gfx = *globalContainer->gfx;
	if (!gfx.rendererCapabilities().accelerated) return false;
	if (tiledCapture || !portableRenderer()) return true;
	return compositor.highResolutionPages(double(gfx.mapTransformScale()) * gfx.getRasterScale());
}
// The composition scale a view's pages want. Captures and other renderers
// take the compositor's (four with HD material frames, else one). The
// portable renderer takes the density the direct terrain pass draws at: the
// smallest power of two at least the display density, up to four, which is
// its art tiles' and coverage masks' density over 32 pixels a cell. Composed
// denser, a page is minified linearly without mips (128 pixels a cell drawn
// at 48, zoom 1.5, aliases where the direct pass draws 64-pixel tiles);
// composed at native density with HD artwork off, it magnifies coverage into
// steps where the direct pass draws smooth borders. HD frames compose below
// their size box-filtered, as the direct pass filters its art tiles; native
// frames are sampled nearest into a denser page, as it samples them too.
int pageScale(const TerrainVisual::Compositor &compositor, bool tiledCapture)
{
	auto &gfx = *globalContainer->gfx;
	if (tiledCapture || !portableRenderer() || !gfx.rendererCapabilities().accelerated) return compositor.scale();
	const double density = double(gfx.mapTransformScale()) * gfx.getRasterScale();
	int scale = 1;
	while (scale < 4 && scale < density) scale *= 2;
	return scale;
}
struct SamplingPlan
{
	int resolution;
	int downsample;
	std::size_t budget;
	bool pageFits, viewFits;
	// Why the density is below the preferred one ("" when it is not), and
	// the view's pages at the chosen density, for TERRAIN_PAGE_ADMISSION.
	const char *coarser = "";
	std::uint64_t pages = 0;
};
SamplingPlan samplingPlan(const SceneMap &map, int left, int top, int right, int bottom, int vx,
						  int vy, int preferredResolution, int preferredDownsample = 0,
						  bool stableView = false)
{
	const bool gpu = globalContainer->gfx->rendererCapabilities().accelerated;
	const auto budget = gpu ? SoftwareTerrainCache::GPUBudget : SoftwareTerrainCache::Budget;
	// The view's pages at a composition scale, whose page size depends on it.
	// A map smaller than one page repeats within that canonical page. All map
	// dimensions are powers of two, so its period divides the page dimensions.
	const auto pagesAt = [&](int scale) -> std::uint64_t
	{
		const int tiles = SoftwareTerrainCache::pageTilesAt(scale);
		const int x0 = chunkOf(left + vx, tiles), x1 = chunkOf(right + vx, tiles);
		const int y0 = chunkOf(top + vy, tiles), y1 = chunkOf(bottom + vy, tiles);
		// Size the interactive HD view for its worst alignment on the page grid.
		// A pan can expose an extra row and column without changing the zoom.
		const int columns = stableView ? (right - left + tiles - 1) / tiles + 1 : x1 - x0 + 1;
		const int rows = stableView ? (bottom - top + tiles - 1) / tiles + 1 : y1 - y0 + 1;
		return std::uint64_t(std::min(columns, std::max(1, map.getW() / tiles))) *
			std::min(rows, std::max(1, map.getH() / tiles));
	};
	const auto cacheBudget = gpu ? SoftwareTerrainCache::GPUCacheBudget : budget;
	const int limit = globalContainer->gfx->maximumTextureSize();
	const auto fitsDevice = [&](int resolution)
	{ return limit <= 0 || SoftwareTerrainCache::pageTilesAt(resolution) * 32 * resolution <= limit; };
	int resolution = preferredResolution;
	const char *coarser = "";
	// Cache and streaming draws choose density from the complete view, not from
	// each submitted page. Memory pressure must not change sampling page by page.
	while (resolution > 1 &&
		   (!fitsDevice(resolution) || pagesAt(resolution) * pageStorage(resolution, gpu) > budget))
	{
		if (!*coarser) coarser = fitsDevice(resolution) ? "page budget" : "texture size";
		resolution /= 2;
	}
	const auto visiblePages = pagesAt(resolution);
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
		// The portable renderer, which samples a native page linearly only once
		// it is reduced, keeps the density the direct terrain pass's art tiles
		// take below one screen pixel per logical pixel: the smallest power of
		// two at least 32 * density pixels a cell (8 at least), box-filtered from
		// the native cell. Drawn unreduced, a zoomed-out native page is sampled
		// nearest: grainy sand and a different pattern from the other renderers.
		if (portableRenderer())
			while (downsample < 4 && 32.0 / (downsample * 2) >= 32 * density)
				downsample *= 2;
		while (downsample < NativeTilePixels && density <= std::sqrt(2.0) / (downsample * 2) &&
			   visiblePages * pageStorage(resolution, gpu, downsample) > budget)
			downsample *= 2;
	}
	const auto storage = pageStorage(resolution, gpu, downsample);
	const bool pageFits =
		(limit <= 0 || SoftwareTerrainCache::pageTilesAt(resolution) * 32 * resolution / downsample <= limit) &&
		storage <= budget;
	// Immediate draws can stream textures from retained CPU pixels. Progressive
	// draws need the full view to fit eventually or some holes never fill.
	const auto cpuStorage = pageStorage(resolution, false, downsample);
	const auto minimumStorage = gpu && !stableView ? visiblePages * cpuStorage + storage - cpuStorage
		: visiblePages * storage;
	const bool texturesFit = !stableView || !gpu || visiblePages * (storage - cpuStorage) <= budget;
	return {resolution, downsample, cacheBudget, pageFits,
		pageFits && texturesFit && minimumStorage <= cacheBudget, coarser, visiblePages};
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
	for (int y = 0; y < chunk.span; ++y)
		for (int x = 0; x < chunk.span;)
		{
			auto opaque = [&](int column)
			{ return chunk.opaque[y * SoftwareTerrainCache::ChunkTiles + column]; };
			if (!opaque(x))
			{
				++x;
				continue;
			}
			const int start = x++;
			while (x < chunk.span && opaque(x))
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
// Whether a reduced page composes its mixed cells at the page's density, as
// the direct pass draws them (Compositor::compose with a reduction), rather
// than box-filtering their native composition. The render devices keep the
// box filter, which their own page composition (terrain-reduce) mirrors.
bool mixedCellsAtDensity()
{
    return !globalContainer->gfx->renderDevice();
}
// A reduced page (downsample > 1) composes each cell at native size into a
// job-local tile and box-filters it into the page, as the serial path does,
// or a mixed cell at the page's density (mixedCellsAtDensity).
void buildTerrainJob(std::span<TerrainCellResult> results,
                     const TerrainVisual::Compositor::CpuMaterials &materials, int samplingScale,
                     int downsample, int pitch, std::size_t cellsPerJob, std::size_t job,
                     bool mixedAtDensity, const Uint32 *nativePage = nullptr)
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
        const bool reduceMixed = mixedAtDensity && downsample > 1 && !result.input.recipe.uniform();
        if (result.input.discovered && nativePage && !reduceMixed)
        {
            // A retained native page already holds this cell's composition.
            SDL_Surface tile{};
            tile.format = SDL_PIXELFORMAT_ARGB8888;
            tile.w = tile.h = NativeTilePixels;
            tile.pitch = SoftwareTerrainCache::ChunkPixels * 4;
            tile.pixels = const_cast<Uint32 *>(nativePage) +
                std::size_t(result.index / SoftwareTerrainCache::ChunkTiles) * NativeTilePixels * SoftwareTerrainCache::ChunkPixels +
                std::size_t(result.index % SoftwareTerrainCache::ChunkTiles) * NativeTilePixels;
            reduceTile(&tile, &output, 0, 0, downsample);
            std::tie(result.opaque, result.empty) = classifyAlpha(output, {0, 0, output.w, output.h});
        }
        else if (result.input.discovered)
        {
            if (!result.reused && result.keepMask)
                result.mask = materials.mask(result.input.recipe, samplingScale);
            // A mixed cell composes at the page's density (Compositor::compose).
            materials.compose(result.input.recipe, downsample > 1 && !reduceMixed ? &nativeTile : &output, samplingScale,
                              scratch,
                              result.reused ? result.reused
                              : result.mask ? &*result.mask
                                            : nullptr,
                              downsample > 1 || !result.refresh ? nullptr : &*result.refresh,
                              reduceMixed ? downsample : 1);
            if (downsample > 1 && !reduceMixed)
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
    bool mixedAtDensity = false; // mixedCellsAtDensity() when it was launched.
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
    // A reduced page from a retained native page: a copy of its pixels, which
    // workers reduce instead of composing the cells again.
    std::vector<Uint32> native;
    bool refresh = false;
    // A refresh of an animation round: staged when done, published with the round.
    bool round = false;
    // Cells a refresh left out of view, stale once it is staged.
    std::array<bool, SoftwareTerrainCache::ChunkTiles * SoftwareTerrainCache::ChunkTiles> skipped{};
    int pitch = 0;
    Uint64 discoveryRevision = 0;
    Uint32 discoveryTeams = 0;
    bool discoveryWhole = false;
    std::size_t jobs = 0, cellsPerJob = 1;
    // Each task claims the next job, so a page queued ahead of the view and
    // queued again as visible work composes each job once.
    std::atomic<std::size_t> nextJob{0};
    std::atomic<std::size_t> remaining{0};
    // Composed ahead of the camera, behind visible work, until the view wants it.
    bool ahead = false;
    // The view drew a stand-in or the overview for it: it fades in on arrival.
    bool fellBack = false;
    // A page of the resident base (SoftwareTerrainCache::basePages).
    bool base = false;
    std::atomic<bool> cancelled{false}, failed{false};
    std::uint64_t wanted = 0;
    bool done() const { return remaining.load(std::memory_order_acquire) == 0; }
};

namespace
{
// Queues a pending page's jobs on the shared presentation workers.
void submitPage(const std::shared_ptr<TerrainPendingPage> &page,
                BackgroundWork::Priority priority = BackgroundWork::Priority::Visible)
{
    for (std::size_t task = 0; task < page->jobs; ++task)
        BackgroundWork::submit([page]
        {
            const std::size_t job = page->nextJob.fetch_add(1, std::memory_order_relaxed);
            if (job >= page->jobs)
                return; // Queued again as visible work; another task took it.
            if (!page->cancelled.load(std::memory_order_relaxed) && !page->failed.load(std::memory_order_relaxed))
            {
                try
                {
                    buildTerrainJob(page->results, *page->materials, page->scale, page->downsample,
                                    page->pitch, page->cellsPerJob, job, page->mixedAtDensity,
                                    page->native.empty() ? nullptr : page->native.data());
                }
                catch (...)
                {
                    page->failed.store(true, std::memory_order_relaxed);
                }
            }
            page->remaining.fetch_sub(1, std::memory_order_acq_rel);
        }, priority);
}
}

void SoftwareTerrainCache::cancelPending()
{
    for (auto &page : pending)
        page->cancelled.store(true, std::memory_order_relaxed);
    pending.clear();
    workerMaterials.reset();
    // Without background work, pages compose at the live phase again.
    round.reset();
    shownPhase = {};
    publishStaged();
    dropKeyframes();
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
								   bool wholeMap, AnimationTime time, bool tiledCapture)
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

// Whether the surf of `id` stepped between two animation times (by default,
// up to the compositor's own).
bool surfStepChanged(TerrainVisual::MaterialId id, unsigned previousTime,
                     const TerrainVisual::Compositor &compositor, unsigned now)
{
    const auto &surf = compositor.catalog().materials[id].surf;
    return surf.period && previousTime / surf.ticks != now / surf.ticks;
}
bool surfStepChanged(TerrainVisual::MaterialId id, unsigned previousTime,
                     const TerrainVisual::Compositor &compositor)
{
    return surfStepChanged(id, previousTime, compositor, compositor.surfTime());
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
                     const TerrainVisual::Compositor &compositor, unsigned now)
{
    std::array<TerrainVisual::MaterialId, 4> palette;
    const unsigned count = recipe.palette(palette);
    return previousTime != now && hasShoreline(recipe, compositor) &&
        std::any_of(palette.begin(), palette.begin() + count,
            [&](auto id) { return surfStepChanged(id, previousTime, compositor, now); });
}
bool tileSurfChanged(const TerrainVisual::Recipe &recipe, unsigned previousTime,
                     const TerrainVisual::Compositor &compositor)
{
    return tileSurfChanged(recipe, previousTime, compositor, compositor.surfTime());
}
}
std::size_t SoftwareTerrainCache::publishStaged()
{
    std::size_t bytes = 0;
    for (auto &chunk : chunks)
    {
        if (!chunk->hasStaged)
            continue;
        const int n = ChunkPixels * chunk->scale / chunk->downsample / ChunkTiles;
        if (chunk->image)
            chunk->image->markPixelsChanged(dirtyRects(chunk->staged, n));
        bytes += std::size_t(std::count(chunk->staged.begin(), chunk->staged.end(), true)) * n * n * 4;
        chunk->staged = {};
        chunk->hasStaged = false;
    }
    return bytes;
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
               old->second != pageRevision(compositor, id);
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
        // A smaller page uses the top-left corner of the arrays.
        if (int(i % ChunkTiles) >= page.span || int(i / ChunkTiles) >= page.span)
            continue;
        const auto &tile = tiles[i];
        if (wasValid && page.tiles[i] == tile && !page.stale[i] &&
            (!tile.discovered || (std::none_of(tile.recipe.corners.begin(),
                                               tile.recipe.corners.end(), materialChanged) &&
                                  (keyframing || !tileSurfChanged(tile.recipe, page.surfTime, compositor)))))
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
            if (wasValid && page.tiles[i] == tiles[i] && !page.stale[i] && tiles[i].discovered && !recipe.uniform())
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
                 cellsPerJob, mixedAtDensity = mixedCellsAtDensity()](std::size_t job)
                { buildTerrainJob(results, *materials, samplingScale, reduction, pitch, cellsPerJob, job,
                                  mixedAtDensity); });
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
    // The phase the view shows, or the live one when pages compose at it.
    const auto *shown = shownPhase.materials && id < shownPhase.revisions.size() ? shownPhase.materials.get() : nullptr;
    const auto revision = (shown ? shownPhase.revisions[id] : compositor.textureRevision(id)) + 1;
    if (set.revisions[variant] != revision)
    {
        // A uniform, uncalmed cell's pixels depend only on its material and variant.
        auto *atlas = set.atlas->getSDLSurface();
        if (reduction > 1)
        {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> native(
                SDL_CreateSurface(NativeTilePixels, NativeTilePixels, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
            if (!native) throw std::bad_alloc();
            if (shown) shown->compose(recipe, native.get(), 1);
            else compositor.compose(recipe, native.get(), 0, 0, 1);
            reduceTile(native.get(), atlas, source.x, source.y, reduction);
        }
        else if (shown)
        {
            // The tile's rectangle of the atlas, as a surface of its own.
            SDL_Surface tile{};
            tile.format = SDL_PIXELFORMAT_ARGB8888;
            tile.w = tile.h = n;
            tile.pitch = atlas->pitch;
            tile.pixels = static_cast<Uint8 *>(atlas->pixels) + source.y * atlas->pitch + source.x * 4;
            shown->compose(recipe, &tile, scale);
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
// Animated terrain on the portable renderer. Pages hold still pixels; each
// animated cell draws over its page every frame as a crossfade of two
// keyframes: its composition at the animation times key * keyframeInterval
// and (key + 1) * keyframeInterval, which workers compose from a material
// snapshot prepared at that time, ahead of when the view needs them. The
// crossfade weight is the GPU renderers' own (the fraction of the interval
// elapsed), and composition is linear in each animated texel, so the
// crossfade of two keyframes is the composition of the crossfaded textures:
// shorelines, open water and stand-ins all show one clock.
namespace
{
constexpr std::int64_t NoKey = std::numeric_limits<std::int64_t>::min();
std::int64_t floorDivide(std::int64_t value, std::int64_t divisor)
{
	const auto quotient = value / divisor;
	return quotient * divisor > value ? quotient - 1 : quotient;
}
// Keyframes ring through three slots: the two the view crossfades and the next.
constexpr int KeyframeRing = 3;
int ringSlot(std::int64_t key) { return int(((key % KeyframeRing) + KeyframeRing) % KeyframeRing); }
// Animation ticks between keyframes of a material whose artwork is still
// (surf alone moves): the surf advances well under a pixel in this many.
constexpr int StillArtKeyframeTicks = 6;
// Whether a discovered cell's pixels change over time: an animated corner or
// stone layer, or a halo material whose surf band reaches into the cell.
bool animatesCell(const TerrainVisual::Recipe &recipe, const TerrainVisual::Compositor &compositor)
{
	std::array<TerrainVisual::MaterialId, 4> palette;
	const unsigned count = recipe.palette(palette);
	for (unsigned k = 0; k < count; ++k)
		if (compositor.animated(palette[k])) return true;
	for (unsigned k = 0; k < recipe.haloCount; ++k)
		if (compositor.animated(recipe.halo[k])) return true;
	return false;
}
// Whether a snapshot holds the texture of every material a recipe samples.
bool snapshotCovers(const TerrainVisual::Compositor::CpuMaterials &materials, const TerrainVisual::Recipe &recipe)
{
	std::array<TerrainVisual::MaterialId, 4> palette;
	const unsigned count = recipe.palette(palette);
	for (unsigned k = 0; k < count; ++k)
	{
		const auto id = palette[k];
		if (id >= materials.textures.size())
			return false;
		// A halo material is listed for its surf bands and never covers a
		// sample (CpuMaterials::compose), so the view never prepares its
		// texture there: requiring it held such cells (open water beside a
		// shore cell) without keyframes forever.
		const auto halo = recipe.halo.begin(), haloEnd = recipe.halo.begin() + recipe.haloCount;
		const bool covers = std::find(recipe.corners.begin(), recipe.corners.end(), id) != recipe.corners.end() ||
			std::any_of(recipe.stoneLayers.begin(), recipe.stoneLayers.begin() + recipe.stoneLayerCount,
						[&](const auto &layer) { return layer.material == id || layer.ground == id; });
		if (!covers && std::find(halo, haloEnd, id) != haloEnd)
			continue;
		const auto variant = materials.definitions->variantIndex(id, recipe.x, recipe.y, recipe.seed);
		if (variant >= materials.textures[id].size() || !materials.textures[id][variant].pixels)
			return false;
	}
	return true;
}
// Keyframe stamps: the artwork's identity, and the size of its smaller endpoint.
struct KeyStampValue
{
	std::uint64_t hash = 0;
	int size = 0;
};
KeyStampValue unpackStamp(std::uint64_t packed) { return {packed >> 12, int(packed & 0xfff)}; }
std::uint64_t packStamp(std::uint64_t hash, int size) { return (hash << 12) | std::uint64_t(std::clamp(size, 1, 0xfff)); }
} // namespace

// Keyframes of a page's own animated cells, or of the uniform cells of one
// material at one density (one entry per variant). Each entry has a slot for
// each of three keyframes in one atlas, a row of Columns entries wide; each
// slot is bordered by a copy of its edge so that linear sampling of a scaled
// view never reads its neighbour. The atlas keeps its CPU pixels: finished
// keyframes write into them and upload as dirty rectangles.
struct SoftwareTerrainCache::KeyframeSet
{
	static constexpr int Columns = 8;
	struct Slot
	{
		std::int64_t key = NoKey;     // The keyframe the slot holds.
		std::int64_t pending = NoKey; // The keyframe workers compose into it.
		std::uint64_t generation = 0, epoch = 0;
	};
	struct Entry
	{
		std::array<Slot, KeyframeRing> slots;
		std::uint32_t serial = 0; // Changes when the entry stands for another cell.
		Tile tile;
		// Coverage kept for keyframes of a mixed cell its page holds none for.
		std::shared_ptr<HeldMask> mask;
	};
	int scale, reduction, n;
	std::unique_ptr<TerrainVisual::Surface> atlas;
	std::vector<Entry> entries;
	std::vector<int> entryOf; // By cell (page sets) or variant (uniform sets); -1 none.
	std::vector<SDL_Rect> dirty;
	std::uint64_t used = 0;
	// Page sets: the page's tileStamp `kind` was classified for, and each
	// cell's kind: 0 still, 1 a uniform set's, 2 its own entry.
	std::uint64_t tileStamp = ~std::uint64_t(0);
	std::array<std::uint8_t, ChunkTiles * ChunkTiles> kind{};
	std::uint32_t serials = 0;
	KeyframeSet(int scale, int reduction, std::size_t keys)
		: scale(scale), reduction(reduction), n(NativeTilePixels * scale / reduction), entryOf(keys, -1) {}
	// Each keyframe of the ring has a band of the atlas, entries in rows of
	// Columns within it, so the keyframes of one key upload as one rectangle.
	int bandRows = 0;
	SDL_Rect slot(int entry, int ring) const
	{
		return {entry % Columns * (n + 2) + 1, (ring * bandRows + entry / Columns) * (n + 2) + 1, n, n};
	}
	// Room for `count` entries: a taller atlas holding each band's slots at
	// their places in its new band, uploaded whole on its first draw.
	void reserve(std::size_t count)
	{
		const int wanted = int((count + Columns - 1) / Columns);
		if (wanted <= bandRows)
			return;
		const int rows = std::max(wanted, bandRows * 2), width = Columns * (n + 2);
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
			SDL_CreateSurface(width, KeyframeRing * rows * (n + 2), SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		if (!pixels)
			throw std::bad_alloc();
		SDL_FillSurfaceRect(pixels.get(), nullptr, 0);
		if (atlas)
		{
			const auto *old = atlas->getSDLSurface();
			const int bandHeight = bandRows * (n + 2);
			for (int ring = 0; ring < KeyframeRing; ++ring)
				for (int y = 0; y < bandHeight; ++y)
					std::memcpy(static_cast<Uint8 *>(pixels->pixels) + (ring * rows * (n + 2) + y) * pixels->pitch,
								static_cast<const Uint8 *>(old->pixels) + (ring * bandHeight + y) * old->pitch,
								std::size_t(width) * 4);
		}
		bandRows = rows;
		atlas = std::make_unique<TerrainVisual::Surface>(pixels.get(), scale > 1 || reduction > 1);
		pixels.release();
		dirty.clear();
	}
	// The entry of a cell or variant; a cell's entry forgets its keyframes
	// when the cell changed. Uniform sets keep the first cell of a variant.
	int entryFor(std::size_t key, const Tile &tile, bool shared)
	{
		int &index = entryOf[key];
		if (index < 0)
		{
			reserve(entries.size() + 1);
			index = int(entries.size());
			auto &entry = entries.emplace_back();
			entry.serial = ++serials;
			entry.tile = tile;
		}
		else if (!shared && !(entries[index].tile == tile))
		{
			auto &entry = entries[index];
			entry.slots = {};
			entry.serial = ++serials;
			entry.tile = tile;
			entry.mask.reset();
		}
		return index;
	}
	Uint32 *row(int y) const
	{
		auto *surface = atlas->getSDLSurface();
		return reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) + y * surface->pitch);
	}
	void write(int entry, int ring, const Uint32 *pixels)
	{
		const SDL_Rect r = slot(entry, ring);
		for (int y = 0; y < n; ++y)
		{
			Uint32 *out = row(r.y + y);
			std::memcpy(out + r.x, pixels + std::size_t(y) * n, std::size_t(n) * 4);
			out[r.x - 1] = out[r.x];
			out[r.x + n] = out[r.x + n - 1];
		}
		std::memcpy(row(r.y - 1) + r.x - 1, row(r.y) + r.x - 1, std::size_t(n + 2) * 4);
		std::memcpy(row(r.y + n) + r.x - 1, row(r.y + n - 1) + r.x - 1, std::size_t(n + 2) * 4);
		dirty.push_back({r.x - 1, r.y - 1, n + 2, n + 2});
	}
	void read(int entry, int ring, Uint32 *pixels) const
	{
		const SDL_Rect r = slot(entry, ring);
		for (int y = 0; y < n; ++y)
			std::memcpy(pixels + std::size_t(y) * n, row(r.y + y) + r.x, std::size_t(n) * 4);
	}
	// Uploads what keyframes wrote: within each band, the rows holding them.
	void flush()
	{
		if (!atlas || dirty.empty())
			return;
		std::array<SDL_Rect, KeyframeRing> bands{};
		const int bandHeight = bandRows * (n + 2);
		for (const auto &rect : dirty)
		{
			auto &band = bands[std::size_t(rect.y / bandHeight)];
			if (!band.w)
				band = rect;
			else
				SDL_GetRectUnion(&band, &rect, &band);
		}
		std::vector<SDL_Rect> rows;
		for (const auto &band : bands)
			if (band.w)
				rows.push_back({0, band.y, Columns * (n + 2), band.h});
		atlas->markPixelsChanged(rows);
		dirty.clear();
	}
	// The atlas's CPU pixels and its texture.
	std::size_t bytes() const
	{
		const auto *pixels = atlas ? atlas->getSDLSurface() : nullptr;
		return pixels ? std::size_t(pixels->pitch) * pixels->h * 2 : 0;
	}
};

// Keyframes of one key at one density, composed by a worker. Entries whose
// slot moved on by the time it finishes are not installed.
struct SoftwareTerrainCache::KeyframeJob
{
	std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> materials;
	std::int64_t key = 0;
	std::uint64_t generation = 0, epoch = 0;
	int scale = 1, reduction = 1;
	struct Item
	{
		std::weak_ptr<KeyframeSet> set;
		int entry = 0;
		std::uint32_t serial = 0;
		TerrainVisual::Recipe recipe;
		// Coverage to compose from, shared with its page; else built here
		// and kept when keepMask.
		std::shared_ptr<HeldMask> held;
		bool keepMask = false, composed = false;
		// Pixels holds an earlier keyframe of this cell: only the samples
		// animation changes are composed again.
		std::optional<TerrainVisual::Compositor::Refresh> refresh;
		std::unique_ptr<Uint32[]> pixels;
		std::optional<TerrainVisual::Compositor::CellMask> mask;
	};
	std::vector<Item> items;
	std::size_t pixels = 0;
	std::atomic<bool> cancelled{false}, failed{false}, done{false};
	void run()
	{
		try
		{
			TerrainVisual::Compositor::CpuScratch scratch;
			std::vector<Uint32> native;
			for (auto &item : items)
			{
				if (cancelled.load(std::memory_order_relaxed))
					break;
				const int n = NativeTilePixels * scale / reduction;
				SDL_Surface out{};
				out.format = SDL_PIXELFORMAT_ARGB8888;
				out.w = out.h = n;
				out.pitch = n * 4;
				out.pixels = item.pixels.get();
				const bool uniform = item.recipe.uniform();
				const TerrainVisual::Compositor::CellMask *mask = item.held ? &item.held->mask : nullptr;
				if (!uniform && !mask && item.keepMask)
				{
					item.mask = materials->mask(item.recipe, scale);
					mask = &*item.mask;
				}
				if (reduction > 1 && uniform)
				{
					// As the page reduces its uniform cells: native composition, box-filtered.
					native.resize(std::size_t(NativeTilePixels) * NativeTilePixels);
					SDL_Surface tile{};
					tile.format = SDL_PIXELFORMAT_ARGB8888;
					tile.w = tile.h = NativeTilePixels;
					tile.pitch = NativeTilePixels * 4;
					tile.pixels = native.data();
					materials->compose(item.recipe, &tile, 1, scratch);
					reduceTile(&tile, &out, 0, 0, reduction);
				}
				else
					materials->compose(item.recipe, &out, scale, scratch, uniform ? nullptr : mask,
									   item.refresh ? &*item.refresh : nullptr, uniform ? 1 : reduction);
				item.composed = true;
			}
		}
		catch (...)
		{
			failed.store(true, std::memory_order_relaxed);
		}
		done.store(true, std::memory_order_release);
	}
};

std::uint64_t SoftwareTerrainCache::pageRevision(const TerrainVisual::Compositor &compositor,
												 TerrainVisual::MaterialId id) const
{
	return keyframing && compositor.animated(id) ? 0 : compositor.textureRevision(id);
}

SoftwareTerrainCache::KeySnapshot *SoftwareTerrainCache::keySnapshot(std::int64_t key)
{
	for (auto &snapshot : keySnapshots)
		if (snapshot.key == key)
			return &snapshot;
	return nullptr;
}

std::int64_t SoftwareTerrainCache::keyframeTarget(int time)
{
	const auto now = floorDivide(time, keyframeInterval);
	std::erase_if(keySnapshots, [&](const KeySnapshot &snapshot) { return snapshot.key < now || snapshot.key > now + 2; });
	// A missing snapshot first (after a jump, or on a still clock), then one
	// taken from older artwork, a few frames apart; otherwise the next key.
	for (auto key = now; key <= now + 2; ++key)
		if (!keySnapshot(key))
			return key;
	for (auto key = now; key <= now + 2; ++key)
		if (const auto *snapshot = keySnapshot(key); snapshot->stale && frame >= snapshot->taken + 10)
			return key;
	return now + 2;
}

std::uint64_t SoftwareTerrainCache::keyStamp(TerrainVisual::Compositor &compositor, TerrainVisual::MaterialId id,
											 unsigned variant, std::int64_t key, bool highResolution)
{
	const auto &material = compositor.catalog().materials[id];
	// As Compositor::prepareSelected selects the endpoints of a time.
	const unsigned time = unsigned(key * keyframeInterval);
	const unsigned frames = unsigned(std::max(1, material.animationFrames));
	const unsigned phase = time / unsigned(std::max(1, material.animationTicks)) % frames;
	std::uint64_t hash = 1469598103934665603ull;
	int size = 0xfff;
	const auto mix = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
	for (unsigned k = 0; k < (frames > 1 ? 2u : 1u); ++k)
	{
		const auto source = compositor.phaseSource(id, variant, (phase + k) % frames, !highResolution);
		mix(source.identity);
		mix(source.revision);
		mix(source.edgeIdentity);
		mix(source.edgeRevision);
		size = std::min(size, source.size);
	}
	return packStamp(hash, size);
}

std::uint64_t SoftwareTerrainCache::stillEpoch(const TerrainVisual::Compositor &compositor) const
{
	std::uint64_t epoch = 0;
	for (std::size_t id = 0; id < compositor.catalog().materials.size(); ++id)
		if (!compositor.animated(TerrainVisual::MaterialId(id)))
			epoch += compositor.textureRevision(TerrainVisual::MaterialId(id));
	return epoch;
}

void SoftwareTerrainCache::takeKeySnapshot(TerrainVisual::Compositor &compositor, std::int64_t key, bool highResolution)
{
	auto materials = compositor.cpuMaterials();
	auto *snapshot = keySnapshot(key);
	if (snapshot && snapshot->materials == materials && !snapshot->stale)
		return;
	const auto &catalog = compositor.catalog();
	std::vector<std::vector<std::uint64_t>> stamps(catalog.materials.size());
	for (std::size_t id = 0; id < catalog.materials.size() && id < materials->textures.size(); ++id)
	{
		if (!compositor.animated(TerrainVisual::MaterialId(id)))
			continue;
		const auto &textures = materials->textures[id];
		stamps[id].assign(textures.size(), 0);
		for (std::size_t variant = 0; variant < textures.size(); ++variant)
			if (textures[variant].pixels)
				stamps[id][variant] = keyStamp(compositor, TerrainVisual::MaterialId(id), unsigned(variant), key, highResolution);
	}
	const auto epoch = stillEpoch(compositor);
	if (!snapshot)
	{
		keySnapshots.push_back({key, std::move(materials), ++keyGeneration, epoch, std::move(stamps), false, frame});
		return;
	}
	// New pixels of artwork a snapshot already held make a new generation;
	// variants it gains (a view moving on) do not.
	bool changed = snapshot->epoch != epoch;
	for (std::size_t id = 0; id < stamps.size() && !changed && id < snapshot->stamps.size(); ++id)
		for (std::size_t variant = 0; variant < stamps[id].size() && variant < snapshot->stamps[id].size(); ++variant)
			if (stamps[id][variant] && snapshot->stamps[id][variant] && stamps[id][variant] != snapshot->stamps[id][variant])
			{
				changed = true;
				break;
			}
	snapshot->materials = std::move(materials);
	snapshot->stamps = std::move(stamps);
	snapshot->epoch = epoch;
	snapshot->stale = false;
	snapshot->taken = frame;
	if (changed)
		snapshot->generation = ++keyGeneration;
}

void SoftwareTerrainCache::classifyKeyframes(Chunk &page, const TerrainVisual::Compositor &compositor)
{
	if (!page.keyframes)
		page.keyframes = std::make_shared<KeyframeSet>(page.scale, page.downsample, std::size_t(ChunkTiles) * ChunkTiles);
	auto &set = *page.keyframes;
	if (set.tileStamp == page.tileStamp)
		return;
	set.tileStamp = page.tileStamp;
	std::size_t own = 0;
	for (int i = 0; i < ChunkTiles * ChunkTiles; ++i)
	{
		const auto &tile = page.tiles[i];
		std::uint8_t kind = 0;
		if (i % ChunkTiles < page.span && i / ChunkTiles < page.span && tile.discovered &&
			animatesCell(tile.recipe, compositor))
			kind = tile.recipe.uniform() && !tile.recipe.calmed() ? 1 : 2;
		set.kind[i] = kind;
		own += kind == 2;
	}
	// The atlas holds every cell of the page that needs one at once.
	if (own)
		set.reserve(std::max(own, set.entries.size()));
}

void SoftwareTerrainCache::dropKeyframes()
{
	for (auto &job : keyframeJobs)
		job->cancelled.store(true, std::memory_order_relaxed);
	if (keyframeJobs.empty() && keySnapshots.empty() && uniformKeyframes.empty() && keyframeDraws.empty())
		return;
	keyframeJobs.clear();
	keySnapshots.clear();
	keyframeDraws.clear();
	uniformKeyframes.clear();
	// Slots awaiting the cancelled jobs would wait for ever: start again.
	for (auto &chunk : chunks)
		chunk->keyframes.reset();
	for (auto &page : basePages)
		if (page)
			page->keyframes.reset();
}

std::size_t SoftwareTerrainCache::keyframeBytes() const
{
	std::size_t total = 0;
	for (const auto &chunk : chunks)
		if (chunk->keyframes)
			total += chunk->keyframes->bytes();
	for (const auto &page : basePages)
		if (page && page->keyframes)
			total += page->keyframes->bytes();
	for (const auto &[key, set] : uniformKeyframes)
		total += set->bytes();
	return total;
}

void SoftwareTerrainCache::updateKeyframes(TerrainVisual::Compositor &compositor, AnimationTime time, bool highResolution)
{
	const int interval = keyframeInterval;
	// The key before the presentation time and the share of the interval
	// to the next one elapsed: continuous, like the direct pass's crossfade.
	const auto [now, weight] = keyframeBlend(time, interval);
	// Finished keyframes go into their slots, uploaded as dirty rectangles.
	std::vector<std::shared_ptr<KeyframeSet>> written;
	std::erase_if(keyframeJobs, [&](const std::shared_ptr<KeyframeJob> &job)
	{
		if (!job->done.load(std::memory_order_acquire))
			return false;
		const int ring = ringSlot(job->key);
		const bool failed = job->failed.load(std::memory_order_relaxed);
		for (auto &item : job->items)
		{
			auto set = item.set.lock();
			if (!set || item.entry >= int(set->entries.size()))
				continue;
			auto &entry = set->entries[item.entry];
			auto &slot = entry.slots[ring];
			if (entry.serial != item.serial || slot.pending != job->key)
				continue;
			slot.pending = NoKey;
			if (failed || !item.composed)
				continue;
			set->write(item.entry, ring, item.pixels.get());
			slot.key = job->key;
			slot.generation = job->generation;
			slot.epoch = job->epoch;
			if (item.mask && !entry.mask && releaseMasks(item.mask->bytes()))
				entry.mask = std::make_shared<HeldMask>(std::move(*item.mask), maskTotal);
			if (std::find(written.begin(), written.end(), set) == written.end())
				written.push_back(set);
		}
		return true;
	});
	for (auto &set : written)
		set->flush();
	// Snapshots of other keys than the one prepared this frame are checked
	// now and then: still artwork that changed, or HD frames that arrived
	// where a snapshot holds native ones, compose their keyframes again.
	if (frame % 8 == 0)
	{
		const auto epoch = stillEpoch(compositor);
		for (auto &snapshot : keySnapshots)
		{
			if (snapshot.stale)
				continue;
			snapshot.stale = snapshot.epoch != epoch;
			for (std::size_t id = 0; id < snapshot.stamps.size() && !snapshot.stale; ++id)
				for (std::size_t variant = 0; variant < snapshot.stamps[id].size(); ++variant)
				{
					const auto held = snapshot.stamps[id][variant];
					if (held && unpackStamp(keyStamp(compositor, TerrainVisual::MaterialId(id), unsigned(variant),
												   snapshot.key, highResolution)).size > unpackStamp(held).size)
					{
						snapshot.stale = true;
						break;
					}
				}
		}
	}
	// The cells each copy shows, and a margin of two cells for the camera's
	// next steps, want the keyframes from the current one to the next but one.
	struct Want
	{
		std::shared_ptr<KeyframeSet> set;
		int entry;
		std::int64_t key;
		Chunk *page;
		int cell;
	};
	std::vector<Want> wants;
	keyframeDraws.clear();
	keyframeMisses = 0;
	const Uint64 ticks = SDL_GetTicks();
	const SDL_Rect margin{paintBounds.x - 64, paintBounds.y - 64, paintBounds.w + 128, paintBounds.h + 128};
	const auto &catalog = compositor.catalog();
	for (auto &copy : copies)
	{
		copy.keyframeBegin = copy.keyframeEnd = std::uint32_t(keyframeDraws.size());
		Chunk &page = *copy.chunk;
		if (!page.valid || !page.cpuComposed)
			continue;
		classifyKeyframes(page, compositor);
		const auto own = page.keyframes;
		own->used = frame;
		bool ready = true;
		const SDL_Rect cells = drawnCells(copy);
		const int scale = page.scale, reduction = page.downsample;
		for (int y = cells.y; y < cells.y + cells.h; ++y)
			for (int x = cells.x; x < cells.x + cells.w; ++x)
			{
				const int i = y * ChunkTiles + x;
				const auto kind = own->kind[i];
				if (!kind)
					continue;
				const SDL_Rect cell{copy.x + x * 32, copy.y + y * 32, 32, 32};
				SDL_Rect near, shown;
				if (!SDL_GetRectIntersection(&cell, &margin, &near))
					continue;
				const auto &tile = page.tiles[i];
				std::shared_ptr<KeyframeSet> set;
				int entry;
				if (kind == 1)
				{
					const auto id = tile.recipe.corners[0];
					auto &shared = uniformKeyframes[{unsigned(id), scale, reduction}];
					if (!shared)
						shared = std::make_shared<KeyframeSet>(scale, reduction, catalog.materials[id].variants.size());
					set = shared;
					entry = set->entryFor(catalog.variantIndex(id, tile.recipe.x, tile.recipe.y, tile.recipe.seed), tile, true);
				}
				else
				{
					set = own;
					entry = set->entryFor(std::size_t(i), tile, false);
				}
				set->used = frame;
				for (auto key = now; key <= now + 2; ++key)
				{
					const auto &slot = set->entries[entry].slots[ringSlot(key)];
					const auto *snapshot = keySnapshot(key);
					if (!snapshot || slot.pending == key || (slot.key == key && slot.generation == snapshot->generation))
						continue;
					wants.push_back({set, entry, key, &page, i});
				}
				if (!SDL_GetRectIntersection(&cell, &paintBounds, &shown))
					continue;
				const auto &slots = set->entries[entry].slots;
				const bool first = slots[ringSlot(now)].key == now, second = slots[ringSlot(now + 1)].key == now + 1;
				ready = ready && first && second;
				keyframeMisses += !(first && second);
				if (!first && !second)
					continue; // The page's own pixels show until a keyframe arrives.
				const auto source = [&](std::int64_t key)
				{
					const SDL_Rect r = set->slot(entry, ringSlot(key));
					return SDL_Rect{r.x + (shown.x - cell.x) * scale / reduction, r.y + (shown.y - cell.y) * scale / reduction,
									shown.w * scale / reduction, shown.h * scale / reduction};
				};
				KeyframeDraw draw{set.get(), shown, source(first ? now : now + 1), source(now + 1), copy.alpha, 0};
				if (first && second && weight > 0)
					draw.secondAlpha = keyframeAlpha(copy.alpha, weight);
				keyframeDraws.push_back(draw);
			}
		// A page arriving over a fallback fades in once its animated cells
		// can animate, rather than showing them still meanwhile (for at
		// most HoldMs): until then the fallback beneath shows alone.
		constexpr Uint64 HoldMs = 500;
		if (copy.alpha < 255 && page.arrived && !ready && (!page.arrivalHeld || ticks - page.arrivalHeld < HoldMs))
		{
			if (!page.arrivalHeld)
				page.arrivalHeld = ticks;
			page.arrived = ticks;
			copy.alpha = 0;
			keyframeDraws.resize(copy.keyframeBegin);
		}
		copy.keyframeEnd = std::uint32_t(keyframeDraws.size());
		// The page's cells do not overlap: draw them atlas by atlas.
		std::sort(keyframeDraws.begin() + copy.keyframeBegin, keyframeDraws.end(),
				  [](const KeyframeDraw &a, const KeyframeDraw &b) { return std::less<>()(a.set, b.set); });
	}
	// Launch the wanted keyframes, the current key first, in jobs of one key
	// and density, within a bound on what is in flight and on this frame's
	// graphics-thread work.
	constexpr std::size_t ItemsPerJob = 16, PixelsPerJob = 64 * 1024;
	constexpr std::size_t ItemsPerFrame = 2048, ItemsInFlight = 8192;
	std::size_t inFlight = 0;
	for (const auto &job : keyframeJobs)
		inFlight += job->items.size();
	std::stable_sort(wants.begin(), wants.end(), [](const Want &a, const Want &b) { return a.key < b.key; });
	std::map<std::tuple<std::int64_t, int, int>, std::shared_ptr<KeyframeJob>> filling;
	const auto submit = [&](std::shared_ptr<KeyframeJob> job)
	{
		keyframeJobs.push_back(job);
		BackgroundWork::submit([job] { job->run(); }, BackgroundWork::Priority::Visible);
	};
	std::size_t launched = 0;
	for (auto &want : wants)
	{
		if (launched >= ItemsPerFrame || inFlight + launched >= ItemsInFlight)
			break;
		auto *snapshot = keySnapshot(want.key);
		auto &set = *want.set;
		auto &entry = set.entries[want.entry];
		auto &slot = entry.slots[ringSlot(want.key)];
		if (slot.pending == want.key)
			continue; // Wanted by another copy of the same set this frame.
		const auto &recipe = entry.tile.recipe;
		if (!snapshotCovers(*snapshot->materials, recipe))
		{
			// The view moved on to variants the snapshot was taken without.
			snapshot->stale = true;
			continue;
		}
		auto &job = filling[{want.key, set.scale, set.reduction}];
		if (!job)
		{
			job = std::make_shared<KeyframeJob>();
			job->materials = snapshot->materials;
			job->key = want.key;
			job->generation = snapshot->generation;
			job->epoch = snapshot->epoch;
			job->scale = set.scale;
			job->reduction = set.reduction;
		}
		KeyframeJob::Item item;
		item.set = want.set;
		item.entry = want.entry;
		item.serial = entry.serial;
		item.recipe = recipe;
		item.pixels.reset(new Uint32[std::size_t(set.n) * set.n]);
		if (!recipe.uniform())
		{
			const auto matches = [&](const std::shared_ptr<HeldMask> &held)
			{ return held && held->mask.recipe == recipe && held->mask.scale == set.scale; };
			if (want.set == want.page->keyframes && matches(want.page->masks[want.cell]))
				item.held = want.page->masks[want.cell];
			else if (matches(entry.mask))
				item.held = entry.mask;
			item.keepMask = !item.held;
			// An earlier keyframe of this cell from the same still artwork:
			// compose only the samples animation changes over it.
			if (item.held && set.reduction == 1)
				for (const auto &other : entry.slots)
					if (other.key != NoKey && other.epoch == snapshot->epoch)
					{
						const int ring = int(&other - entry.slots.data());
						set.read(want.entry, ring, item.pixels.get());
						const unsigned animatedSlots = compositor.animatedSlots(item.held->mask);
						item.refresh = TerrainVisual::Compositor::Refresh{animatedSlots, animatedSlots,
																		  &item.held->animatedSamples(compositor)};
						break;
					}
		}
		slot.pending = want.key;
		job->pixels += std::size_t(set.n) * set.n;
		job->items.push_back(std::move(item));
		++launched;
		if (job->items.size() >= ItemsPerJob || job->pixels >= PixelsPerJob)
		{
			submit(std::move(job));
			job.reset();
		}
	}
	for (auto &[key, job] : filling)
		if (job)
			submit(job);
	// Keyframes of pages the view has left for two seconds, and of uniform
	// cells at a density it left for ten, go.
	for (auto &chunk : chunks)
		if (chunk->keyframes && chunk->keyframes->used + 120 < frame)
			chunk->keyframes.reset();
	for (auto &page : basePages)
		if (page && page->keyframes && page->keyframes->used + 120 < frame)
			page->keyframes.reset();
	std::erase_if(uniformKeyframes, [&](const auto &entry) { return entry.second->used + 600 < frame; });
}

bool SoftwareTerrainCache::prepareAtResolution(const SceneMap &map, GAGCore::Sprite &, int left,
											   int top, int right, int bottom, int vx, int vy,
											   Uint32 visibleTeams, bool wholeMap, AnimationTime presentation,
											   int preferredResolution, bool tiledCapture,
											   int preferredDownsample)
{
	// Pages compose at whole steps; only the keyframe crossfade interpolates.
	const int time = presentation.steps;
	if (registry.get() != &map.terrainRegistry() || assets != map.frozenAssetBundle())
	{
        chunks.clear();
        clearBase();
        gpuPages.reset();
        cpuMaterialLease.reset();
        animatedTiles.clear();
        cancelPending();
        dropKeyframes();
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
		for (auto &page : basePages)
			if (page && page->image && page->image->portableReleased())
				page.reset();
	}
	copies.clear();
	incomplete = false;
	const Uint64 now = SDL_GetTicks();
	coverage = {};
	uncoveredPages.clear();
	// Before this frame marks what it draws: pages the last frame drew stay.
	if (trimBefore) trimIdlePages(8);
	++frame;
	if (left > right || top > bottom) return true;
    if (!enabled)
    {
        gpuPages.reset();
        chunks.clear();
        clearBase();
        cpuMaterialLease.reset();
        cancelPending();
        return false;
    }
	paintBounds = {left * 32, top * 32, (right - left + 1) * 32, (bottom - top + 1) * 32};
	auto &compositor = globalContainer->terrainCompositor(map.frozenAssetBundle());
	overlayCompositor = &compositor;
	// The camera's motion: the view centre's smoothed step in tiles, and the
	// view's growth while zooming out. A jump resets both.
	struct { int x0, y0, x1, y1; } around{};
	{
		const double centreX = (left + right + 1) * 0.5 + vx, centreY = (top + bottom + 1) * 0.5 + vy;
		const int span = std::max(right - left, bottom - top) + 1;
		const auto wrapped = [](double delta, int period)
		{ return delta - std::round(delta / period) * period; };
		const double dx = wrapped(centreX - lastCentreX, map.getW()), dy = wrapped(centreY - lastCentreY, map.getH());
		if (!motionKnown || std::abs(dx) > span || std::abs(dy) > span)
			motionX = motionY = growth = 0;
		else
		{
			motionX = motionX * 0.75 + dx * 0.25;
			motionY = motionY * 0.75 + dy * 0.25;
			growth = growth * 0.75 + (span - lastSpan) * 0.25;
		}
		motionKnown = true;
		lastCentreX = centreX;
		lastCentreY = centreY;
		lastSpan = span;
	}
	// Pages around the view and ahead of the camera (see the launches
	// below): a ring one page wide, wider ahead of a pan and all round while
	// zooming out, never past the map's own period. In pages of `tiles`: the
	// materials are prepared for ChunkTiles pages before the view's density,
	// and so its page size, is chosen.
	const auto ringAround = [&](int tiles)
	{
		const int vx0 = chunkOf(left + vx, tiles), vx1 = chunkOf(right + vx, tiles);
		const int vy0 = chunkOf(top + vy, tiles), vy1 = chunkOf(bottom + vy, tiles);
		const auto pagesAhead = [&](double cells)
		{ return int(std::ceil(std::min(std::abs(cells), double(lastSpan)) / tiles)); };
		const int leadX = pagesAhead(motionX * LookaheadFrames), leadY = pagesAhead(motionY * LookaheadFrames);
		const int widen = growth > 0.05 ? std::min(4, pagesAhead(growth * LookaheadFrames / 2)) : 0;
		decltype(around) ring{vx0 - 1 - widen - (motionX < 0 ? leadX : 0), vy0 - 1 - widen - (motionY < 0 ? leadY : 0),
				 vx1 + 1 + widen + (motionX > 0 ? leadX : 0), vy1 + 1 + widen + (motionY > 0 ? leadY : 0)};
		const int periodX = std::max(1, map.getW() / tiles), periodY = std::max(1, map.getH() / tiles);
		if (ring.x1 - ring.x0 + 1 > periodX) ring.x0 = vx0, ring.x1 = std::min(vx1, vx0 + periodX - 1);
		if (ring.y1 - ring.y0 + 1 > periodY) ring.y0 = vy0, ring.y1 = std::min(vy1, vy0 + periodY - 1);
		return ring;
	};
	around = ringAround(ChunkTiles);
	// Only interactive accelerated views that compose on the workers compose
	// ahead of the camera (see `deferring` below).
	const bool composeAhead = deferBuilds && !tiledCapture && globalContainer->gfx->rendererCapabilities().accelerated &&
		!(globalContainer->gfx->renderDevice() && globalContainer->gfx->renderDevice()->capabilities().terrainComposition);

	auto &gfx = *globalContainer->gfx;
	const bool nextGPU = gfx.rendererCapabilities().accelerated;
	// When even native-size pages need reduction, avoid preparing HD
	// materials for the interactive Legacy OpenGL view.
	const bool reducedLegacyView = gfx.isLegacyGLScreen() && !tiledCapture && !preferredDownsample &&
		samplingPlan(map, left, top, right, bottom, vx, vy, 1).downsample > 1;
	const bool portable = portableRenderer();
	const bool highResolution = composeHighResolution(compositor, tiledCapture);
	// The portable renderer's interactive views draw animated terrain from
	// keyframes (see updateKeyframes): the compositor is prepared at the time
	// of the keyframe the view needs next, not the live one.
	{
		int interval = 0;
		for (const auto &material : compositor.catalog().materials)
		{
			const int ticks = material.animationFrames > 1 ? std::max(1, material.animationTicks)
				: material.surf.period > 0 ? StillArtKeyframeTicks : 0;
			if (ticks) interval = interval ? std::min(interval, ticks) : ticks;
		}
		const bool wanted = composeAhead && portable && interval > 0;
		if (!wanted || interval != keyframeInterval)
			dropKeyframes();
		keyframing = wanted;
		keyframeInterval = interval;
	}
	const std::int64_t keyTarget = keyframing ? keyframeTarget(time) : 0;
	const int preparedTime = keyframing ? int(keyTarget * keyframeInterval) : time;
	// The portable renderer's resident base (see basePages): the finest
	// reduction of native density whose whole map fits half the platform's
	// terrain page allowance (16 pixels a cell for maps up to 256 cells square
	// on desktop), chosen once a map. The missing base pages nearest the view
	// launch a few a frame; their materials are prepared with the view's.
	std::vector<SDL_Point> baseLaunches;
	if (composeAhead && portable && !baseReduction)
	{
		const std::size_t allowance =
			GAGCore::platformRenderBudget().classLimit(GAGCore::TextureDescriptor::BudgetClass::TerrainPage) / 2;
		const int columns = std::max(1, map.getW() / ChunkTiles), rows = std::max(1, map.getH() / ChunkTiles);
		baseReduction = -1; // None fits.
		for (int reduction = 2; reduction <= 8; reduction *= 2)
		{
			const std::size_t side = ChunkPixels / reduction;
			if (std::size_t(columns) * rows * side * side * 4 > allowance)
				continue;
			baseReduction = reduction;
			baseColumns = columns;
			baseRows = rows;
			basePages.clear();
			basePages.resize(std::size_t(columns) * rows);
			break;
		}
	}
	if (composeAhead && portable && baseReduction > 0)
	{
		constexpr std::size_t BaseLaunchesPerFrame = 2;
		const std::size_t baseLimit = std::max(2u, BackgroundWork::workers());
		std::vector<char> queued(basePages.size(), 0);
		std::size_t inFlight = 0;
		for (const auto &page : pending)
			if (page->base)
			{
				++inFlight;
				queued[std::size_t(page->y / ChunkTiles) * baseColumns + page->x / ChunkTiles] = 1;
			}
		if (inFlight < baseLimit)
		{
			const double centreX = (left + right + 1) * 0.5 + vx, centreY = (top + bottom + 1) * 0.5 + vy;
			const auto wrapped = [](double delta, int period)
			{ return delta - std::round(delta / period) * period; };
			std::vector<std::pair<double, std::size_t>> missing;
			for (std::size_t i = 0; i < basePages.size(); ++i)
			{
				if (queued[i] || (basePages[i] && basePages[i]->valid))
					continue;
				const double dx = wrapped(double(i % baseColumns) * ChunkTiles + ChunkTiles * 0.5 - centreX, map.getW());
				const double dy = wrapped(double(i / baseColumns) * ChunkTiles + ChunkTiles * 0.5 - centreY, map.getH());
				missing.push_back({dx * dx + dy * dy, i});
			}
			const std::size_t count = std::min({missing.size(), baseLimit - inFlight, BaseLaunchesPerFrame});
			std::partial_sort(missing.begin(), missing.begin() + count, missing.end());
			for (std::size_t k = 0; k < count; ++k)
				baseLaunches.push_back({int(missing[k].second % baseColumns) * ChunkTiles,
										int(missing[k].second / baseColumns) * ChunkTiles});
		}
	}
    {
        PERF_SCOPE_TIME(TerrainMaterials);
	// CPU and device composition can rebuild complete pages beyond the
	// viewport, so prepare every cell in those pages before collecting jobs.
	const auto prepareMaterials = [&] {
		// Pages composed ahead of the camera need their materials too.
		const int px0 = composeAhead ? std::min(around.x0, chunkOf(left + vx)) : chunkOf(left + vx);
		const int py0 = composeAhead ? std::min(around.y0, chunkOf(top + vy)) : chunkOf(top + vy);
		const int px1 = composeAhead ? std::max(around.x1, chunkOf(right + vx)) : chunkOf(right + vx);
		const int py1 = composeAhead ? std::max(around.y1, chunkOf(bottom + vy)) : chunkOf(bottom + vy);
		compositor.prepareVisible(highResolution, preparedTime, map,
			px0 * ChunkTiles, py0 * ChunkTiles, (px1 + 1) * ChunkTiles - 1, (py1 + 1) * ChunkTiles - 1,
			reducedLegacyView,
			// Portable draws decor HD frames only where the HD sprite policy shows HD.
			!portable || GAGCore::Sprite::drawsHighResolutionOnDemand(), baseLaunches);
	};
	// A capture is drawn once: it waits for the material sheets the map holds.
	if (tiledCapture) compositor.finishMaterials(map);
	prepareMaterials();
	// A capture is drawn once, so it waits for streamed HD rather than
	// keeping the native fallback that interactive frames replace later.
	if (tiledCapture && highResolution)
	{
		GAGCore::Sprite::finishHighResolution();
		prepareMaterials();
	}
	if (keyframing)
		takeKeySnapshot(compositor, keyTarget, highResolution);
    }
	const auto plan =
		viewSamplingPlan(map, left, top, right, bottom, vx, vy,
						 preferredResolution > 0 ? preferredResolution : pageScale(compositor, tiledCapture),
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
	// One line whenever the view's page density (or why it is coarser than the
	// compositor's) changes, as DIRECT_TERRAIN_ADMISSION does for direct terrain.
	{
		const int wanted = preferredResolution > 0 ? preferredResolution : pageScale(compositor, tiledCapture);
		const std::tuple<int, int, int, const char *> admission{wanted, plan.resolution, plan.downsample, plan.coarser};
		if (admission != lastAdmission && !tiledCapture)
		{
			lastAdmission = admission;
			std::fprintf(stderr, "TERRAIN_PAGE_ADMISSION renderer=%s wanted_scale=%d scale=%d reduction=%d page_tiles=%d "
						 "pages=%llu page_bytes=%zu budget=%zu coarser=%s\n",
						 portableRenderer() ? "portable-sdl" : nextGPU ? "accelerated" : "software", wanted,
						 plan.resolution, plan.downsample, pageTilesAt(plan.resolution),
						 static_cast<unsigned long long>(plan.pages), pageStorage(plan.resolution, nextGPU, plan.downsample),
						 std::size_t(nextGPU ? GPUBudget : Budget), *plan.coarser ? plan.coarser : "-");
		}
	}
	pageTiles = pageTilesAt(nextResolution);
	const int x0 = chunkOf(left + vx, pageTiles), x1 = chunkOf(right + vx, pageTiles);
	const int y0 = chunkOf(top + vy, pageTiles), y1 = chunkOf(bottom + vy, pageTiles);
	if (gpu != nextGPU)
	{
		chunks.clear();
		clearBase();
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
    const auto visiblePages = std::uint64_t(std::min(x1-x0+1,std::max(1,map.getW()/pageTiles))) *
        std::min(y1-y0+1,std::max(1,map.getH()/pageTiles));
    // Preflight the complete view, including retained packet leases, before
    // selecting GPU composition. Oversized views use the CPU page reference.
    const auto coverageReserve = visiblePages * pageTiles * pageTiles *
        std::uint64_t(32*resolution) * (32*resolution) * 9;
    const auto colorReserve = visiblePages * std::uint64_t(pageTiles*32*resolution/downsample) *
        (pageTiles*32*resolution/downsample) * 4;
    // Class budgets are independent maxima, not simultaneously available GPU
    // memory. Conservatively reserve the complete visible composition and a
    // bounded upload segment on top of currently resident resources. Cached
    // pages may be counted twice; declining promotion releases those caches and
    // uses the CPU reference until aggregate headroom becomes available again.
    const auto &platformBudget = GAGCore::platformRenderBudget();
    const std::uint64_t deviceBudget = platformBudget.total;
    constexpr std::uint64_t uploadReserveBytes = 8u*1024*1024;
    const auto resident = device ? device->telemetry().resourceBytes : 0;
    const auto compositionReserve = coverageReserve + colorReserve + uploadReserveBytes;
    const bool composeOnDevice = device && device->capabilities().terrainComposition &&
        coverageReserve <= GPUMaskBudget && compositionReserve <= deviceBudget &&
        colorReserve <= platformBudget.classLimit(GAGCore::TextureDescriptor::BudgetClass::TerrainPage) &&
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
    if (gpuPages) gpuPages->begin(gfx.renderTargetContentRevision(),frame,x0,y0,x1,y1,map.getMaskW(),map.getMaskH(),resolution,downsample,pageTiles);

		// Protect the complete requested view before admitting its first page.
		// Otherwise a pan can evict a page we are about to visit later this frame.
		const int periodX = std::max(1, map.getW() / pageTiles);
		const int periodY = std::max(1, map.getH() / pageTiles);
		for (auto &c : chunks)
		{
			const bool selected = c->scale == resolution && c->downsample == downsample &&
				((c->x / pageTiles - x0) & (periodX - 1)) <= x1 - x0 &&
				((c->y / pageTiles - y0) & (periodY - 1)) <= y1 - y0;
			if (selected) c->used = frame;
		}
		// Pages last used before `before` (by default this frame) may go; work
		// ahead of the camera passes an older frame, so it never displaces pages
		// the view drew or stood in with recently.
		const auto makeRoom = [&](std::size_t needed, std::size_t textureNeeded = 0, std::uint64_t before = 0)
		{
			const std::uint64_t idle = before ? before : frame;
			while (bytes() + needed > budget ||
                (gpu && residentTextureBytes()+textureNeeded > GPUBudget))
			{
				auto oldest = chunks.end();
				// Retire idle renderer storage before discarding any composed pixels.
				if (gpu)
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->used < idle && (*it)->image &&
							(oldest == chunks.end() || (*it)->textureUsed < (*oldest)->textureUsed)) oldest = it;
				if (oldest == chunks.end())
					for (auto it = chunks.begin(); it != chunks.end(); ++it)
						if ((*it)->used < idle &&
							(oldest == chunks.end() || (*it)->used < (*oldest)->used)) oldest = it;
				if (oldest == chunks.end())
				{
					// Visible CPU pages are protected; their renderer allocations
					// can still be retired and uploaded later during drawing.
					if (!gpu || before) return false;
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
			const int wx = (cx * pageTiles) & map.getMaskW();
			const int wy = (cy * pageTiles) & map.getMaskH();
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
		keyframing = keyframing && deferring;
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
			// Visible pages install before pages composed ahead of the camera.
			std::stable_partition(pending.begin(), pending.end(), [](const auto &page) { return !page->ahead; });
			std::erase_if(pending, [&](const std::shared_ptr<TerrainPendingPage> &page)
			{
				// Animation rounds stage their refreshes below, after the view's pages.
				if (page->round || !page->done() || installs >= 16)
					return false;
				const std::size_t side = std::size_t(pageTilesAt(page->scale)) * 32 * page->scale / page->downsample;
				const std::size_t bytes = side * side * 4;
				if (installs && installBytes + bytes > InstallBytesPerFrame)
					return false;
				installBytes += bytes;
				if (page->failed || page->seed != map.terrainSeed())
					return true;
				if (!page->pixels)
					return true;
				if (page->base)
				{
					// A base page uploads its texture now and lets its pixels go:
					// it never animates, and the view may not draw it for minutes.
					const int column = page->x / ChunkTiles, row = page->y / ChunkTiles;
					if (page->downsample != baseReduction || column >= baseColumns || row >= baseRows)
						return true;
					auto fresh = std::make_unique<Chunk>();
					fresh->x = page->x;
					fresh->y = page->y;
					fresh->downsample = page->downsample;
					fresh->seed = page->seed;
					fresh->recipeTerrainRevision = page->terrainRevision;
					fresh->sources = page->sources;
					fresh->tiles = page->tiles;
					for (const auto &result : page->results)
					{
						fresh->opaque[result.index] = result.opaque;
						fresh->empty[result.index] = result.empty;
					}
					fresh->discoveryRevision = page->discoveryRevision;
					fresh->discoveryTeams = page->discoveryTeams;
					fresh->discoveryWhole = page->discoveryWhole;
					fresh->discoveryStamped = true;
					fresh->valid = true;
					fresh->resident = true;
					fresh->image = std::make_unique<TerrainVisual::Surface>(page->pixels.release(), true);
					fresh->image->prepareTexture();
					fresh->image->releasePortablePixels();
					basePages[std::size_t(row) * baseColumns + column] = std::move(fresh);
					++installs;
					return true;
				}
				Chunk *existing = nullptr;
				for (auto &chunk : chunks)
					if (chunk->x == page->x && chunk->y == page->y && chunk->scale == page->scale &&
						chunk->downsample == page->downsample)
						existing = chunk.get();
				if (existing && existing->valid)
					return true;
				if (existing)
					std::erase_if(chunks, [&](const auto &chunk) { return chunk.get() == existing; });
				if (!makeRoom(storageFor(page->scale, page->downsample), 0,
							  page->ahead ? std::max<std::uint64_t>(1, frame > AheadIdleFrames ? frame - AheadIdleFrames : 1) : 0))
					return true;
				auto fresh = std::make_unique<Chunk>();
				fresh->x = page->x;
				fresh->y = page->y;
				fresh->scale = page->scale;
				fresh->span = pageTilesAt(page->scale);
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
					fresh->overlay[result.index] = !keyframing && result.input.discovered && recipe.uniform() &&
						!recipe.calmed() && animatedTile(compositor, recipe, tileSource, page->scale, page->downsample);
					if (result.mask && releaseMasks(result.mask->bytes()))
						fresh->masks[result.index] = std::make_unique<HeldMask>(std::move(*result.mask), maskTotal);
				}
				fresh->materialRevisions = std::move(page->materialRevisions);
				fresh->surfTime = page->materials->surfTime;
				for (const auto &tile : fresh->tiles)
					if (tile.discovered && !keyframing)
						fresh->hasShoreline |= hasShoreline(tile.recipe, compositor);
				fresh->parked = std::move(page->pixels);
				if (!page->native.empty()) ++reductions;
				fresh->discoveryRevision = page->discoveryRevision;
				fresh->discoveryTeams = page->discoveryTeams;
				fresh->discoveryWhole = page->discoveryWhole;
				fresh->discoveryStamped = true;
				fresh->valid = true;
				// A page ahead of the view is not protected as a drawn one is.
				fresh->used = page->ahead ? frame - 1 : frame;
				fresh->arrived = page->fellBack ? SDL_GetTicks() : 0;
				chunks.push_back(std::move(fresh));
				++rebuilds;
				++installs;
				return true;
			});
		}
		// The resident base page holding world tile (wx, wy), when it shows the
		// map as it is now: a page whose terrain or discovered cells changed
		// draws nothing and composes again.
		const auto usableBase = [&](int wx, int wy) -> Chunk *
		{
			if (baseReduction <= 0)
				return nullptr;
			const int column = (wx & map.getMaskW()) / ChunkTiles, row = (wy & map.getMaskH()) / ChunkTiles;
			if (column >= baseColumns || row >= baseRows)
				return nullptr;
			Chunk *base = basePages[std::size_t(row) * baseColumns + column].get();
			if (!base || !base->valid || !base->image)
				return nullptr;
			const int px = column * ChunkTiles, py = row * ChunkTiles;
			if (base->seed != map.terrainSeed())
				base->valid = false;
			else if (base->recipeTerrainRevision != terrainRevision)
			{
				std::array<Uint32, SourceSpan * SourceSpan> sources{};
				for (int y = -SourceHalo; y <= ChunkTiles + SourceHalo; ++y)
					for (int x = -SourceHalo; x <= ChunkTiles + SourceHalo; ++x)
						sources[(y + SourceHalo) * SourceSpan + x + SourceHalo] = map.vertexTerrainAt(px + x, py + y);
				if (sources == base->sources)
					base->recipeTerrainRevision = terrainRevision;
				else
					base->valid = false;
			}
			if (base->valid && !(base->discoveryWhole == wholeMap &&
				(wholeMap || (base->discoveryRevision == map.discoveryRevision() && base->discoveryTeams == visibleTeams))))
			{
				for (int y = 0; y < ChunkTiles && base->valid; ++y)
					for (int x = 0; x < ChunkTiles; ++x)
						if ((wholeMap || map.isMapPartiallyDiscovered(px + x - 1, py + y - 1, px + x + 1, py + y + 1,
																	  visibleTeams)) != base->tiles[y * ChunkTiles + x].discovered)
						{
							base->valid = false;
							break;
						}
				if (base->valid)
				{
					base->discoveryRevision = map.discoveryRevision();
					base->discoveryTeams = visibleTeams;
					base->discoveryWhole = wholeMap;
				}
			}
			return base->valid ? base : nullptr;
		};
		// The page at its nearest other density; never the view's own density.
		// What stands in for the view's page at world tile (wx, wy) while it
		// composes: the pages of one other density over it, either the larger
		// page that holds it or the smaller pages within it (page sizes depend
		// on density). A density that covers the whole page comes first, then
		// the nearest, preferring the sharper of two equally near. Returns
		// whether those pages cover the whole page.
		const auto findStandIn = [&](int wx, int wy, std::vector<Chunk *> &found) -> bool
		{
			found.clear();
			const double wanted = std::log2(double(resolution) / downsample);
			std::map<std::pair<int, int>, std::pair<std::vector<Chunk *>, int>> densities;
			for (auto &chunk : chunks)
			{
				if (!chunk->valid || !chunk->cpuComposed ||
					(chunk->scale == resolution && chunk->downsample == downsample) ||
					!(chunk->image || chunk->parked) || chunk->seed != map.terrainSeed())
					continue;
				int cells = 0;
				if (chunk->span >= pageTiles)
				{
					if (chunk->x == (wx & ~(chunk->span - 1)) && chunk->y == (wy & ~(chunk->span - 1)))
						cells = pageTiles * pageTiles;
				}
				else if ((chunk->x & ~(pageTiles - 1)) == wx && (chunk->y & ~(pageTiles - 1)) == wy)
					cells = chunk->span * chunk->span;
				if (!cells)
					continue;
				auto &density = densities[{chunk->scale, chunk->downsample}];
				density.first.push_back(chunk.get());
				density.second += cells;
			}
			// The resident base covers the whole page, beneath any pages of its
			// density there.
			const std::pair<int, int> baseKey{1, baseReduction};
			if (Chunk *base = usableBase(wx, wy); base && densities[baseKey].second < pageTiles * pageTiles)
			{
				auto &density = densities[baseKey];
				density.first.insert(density.first.begin(), base);
				density.second = pageTiles * pageTiles;
			}
			double bestDistance = 0, partialDistance = 0;
			bool bestComplete = false;
			const std::vector<Chunk *> *partial = nullptr; // The nearest density that does not cover it.
			for (auto &[key, density] : densities)
			{
				const bool complete = density.second >= pageTiles * pageTiles;
				const double level = std::log2(double(key.first) / key.second);
				const double distance = std::abs(level - wanted) - (level > wanted ? 0.25 : 0);
				if (found.empty() || (complete && !bestComplete) ||
					(complete == bestComplete && distance < bestDistance))
					found = density.first, bestDistance = distance, bestComplete = complete;
				if (!complete && (!partial || distance < partialDistance))
					partial = &density.first, partialDistance = distance;
			}
			// Over the base, the parts of the page that a nearer density has.
			if (bestComplete && partial && partialDistance < bestDistance && found.front()->resident)
				found.insert(found.end(), partial->begin(), partial->end());
			return bestComplete;
		};
		std::vector<Chunk *> standInPages;
		// Draws what stands in for the view's page (cx, cy) at world tile
		// (wx, wy). Returns false, and lists its overview page for the
		// backdrop beneath, unless that covers the whole page.
		const auto drawStandIn = [&](int cx, int cy, int wx, int wy) -> bool
		{
			const bool complete = findStandIn(wx, wy, standInPages);
			const int screenX = (cx * pageTiles - vx) * 32, screenY = (cy * pageTiles - vy) * 32;
			for (Chunk *page : standInPages)
			{
				page->used = frame;
				if (page->span >= pageTiles)
					copies.push_back({page, screenX - (wx - page->x) * 32, screenY - (wy - page->y) * 32, 255,
									  {(wx - page->x) * 32, (wy - page->y) * 32, pageTiles * 32, pageTiles * 32}});
				else
					copies.push_back({page, screenX + (page->x - wx) * 32, screenY + (page->y - wy) * 32});
			}
			if (!complete)
			{
				incomplete = true;
				// The overview's pages are ChunkTiles wide.
				const SDL_Point backdrop{wx & ~(ChunkTiles - 1), wy & ~(ChunkTiles - 1)};
				if (std::none_of(uncoveredPages.begin(), uncoveredPages.end(), [&](const SDL_Point &page)
								 { return page.x == backdrop.x && page.y == backdrop.y; }))
					uncoveredPages.push_back(backdrop);
			}
			return complete;
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
		// Material sheets load on demand: a cell whose sheets are still loading
		// stays uncomposed, so the chain beneath (another density, the
		// overview) draws instead of a flat preview colour.
		const auto sheetsReady = [&](const std::array<Tile, ChunkTiles * ChunkTiles> &tiles)
		{
			bool ready = true;
			for (const auto &tile : tiles)
				if (tile.discovered && !compositor.sheetsReady(tile.recipe))
					ready = false; // Keep asking for every missing sheet.
			return ready;
		};
		// Brings a valid page to an animation round's phase: workers compose its
		// changed cells that the view shows from the round's snapshot. Cells out
		// of view become stale instead, until the view shows them.
		const auto launchRefresh = [&](Chunk &entry, const std::array<Tile, ChunkTiles * ChunkTiles> &tiles,
									   const AnimationPhase &phase, const std::array<bool, ChunkTiles * ChunkTiles> &shown,
									   std::size_t &units)
		{
			units = 1;
			// Keep the page's pixels until its sheets are back.
			if (!sheetsReady(tiles))
				return;
			const unsigned now = phase.materials->surfTime;
			const auto materialChanged = [&](TerrainVisual::MaterialId id)
			{
				const auto previous = std::find_if(entry.materialRevisions.begin(), entry.materialRevisions.end(),
					[id](const auto &revision) { return revision.first == id; });
				return previous == entry.materialRevisions.end() || phase.revisions[id] != previous->second;
			};
			auto page = std::make_shared<TerrainPendingPage>();
			page->refresh = true;
			page->round = true;
			page->x = entry.x;
			page->y = entry.y;
			page->scale = entry.scale;
			page->downsample = entry.downsample;
			page->mixedAtDensity = mixedCellsAtDensity();
			page->seed = entry.seed;
			page->terrainRevision = entry.recipeTerrainRevision;
			page->tiles = tiles;
			const int n = 32 * entry.scale / entry.downsample;
			// Whether an overlay cell touches a cell drawn from the page: linear
			// sampling of that cell's edge reads this cell's page pixels.
			const auto bordersPage = [&](unsigned i)
			{
				const int x = int(i % ChunkTiles), y = int(i / ChunkTiles);
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int nx = x + dx, ny = y + dy;
						if (nx >= 0 && ny >= 0 && nx < ChunkTiles && ny < ChunkTiles &&
							tiles[ny * ChunkTiles + nx].discovered && !entry.overlay[ny * ChunkTiles + nx])
							return true;
					}
				return false;
			};
			std::vector<unsigned> changed;
			for (unsigned i = 0; i < tiles.size(); ++i)
			{
				const auto &tile = tiles[i];
				// Overlay cells draw from the shared animated tiles instead, but
				// the page keeps those beside its own cells at the same phase.
				if (!tile.discovered || (entry.overlay[i] && !bordersPage(i)))
					continue;
				if (entry.stale[i] || std::any_of(tile.recipe.corners.begin(), tile.recipe.corners.end(), materialChanged) ||
					(!keyframing && tileSurfChanged(tile.recipe, entry.surfTime, compositor, now)))
				{
					if (shown[i])
						changed.push_back(i);
					else
						page->skipped[i] = true;
				}
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
						page->materialRevisions.emplace_back(palette[k], phase.revisions[palette[k]]);
			}
			if (changed.empty())
			{
				// Only overlay cells, or cells out of view, moved on.
				entry.materialRevisions = std::move(page->materialRevisions);
				entry.surfTime = now;
				for (unsigned i = 0; i < tiles.size(); ++i)
					entry.stale[i] = entry.stale[i] || page->skipped[i];
				return;
			}
			page->materials = phase.materials;
			materialsUsed = frame;
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
			round->pages.push_back(page);
			submitPage(page);
			units = changed.size();
		};
		const auto launch = [&](int wx, int wy, const std::array<Tile, ChunkTiles * ChunkTiles> &tiles,
								const std::array<Uint32, SourceSpan * SourceSpan> &sources,
								const SDL_Surface *nativePixels = nullptr, bool ahead = false)
		{
			auto page = std::make_shared<TerrainPendingPage>();
			page->ahead = ahead;
			page->x = wx;
			page->y = wy;
			page->scale = resolution;
			page->downsample = downsample;
			page->mixedAtDensity = mixedCellsAtDensity();
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
						page->materialRevisions.emplace_back(palette[k], pageRevision(compositor, palette[k]));
			}
			page->materials = frameMaterials();
			page->discoveryRevision = map.discoveryRevision();
			page->discoveryTeams = visibleTeams;
			page->discoveryWhole = wholeMap;
			const int size = pageTiles * 32 * resolution / downsample, n = 32 * resolution / downsample;
			page->pixels.reset(SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888));
			if (!page->pixels)
				throw std::bad_alloc();
			auto *base = static_cast<Uint8 *>(page->pixels->pixels);
			const int pitch = page->pixels->pitch;
			page->pitch = pitch;
			if (nativePixels)
			{
				page->native.resize(std::size_t(ChunkPixels) * ChunkPixels);
				for (int row = 0; row < ChunkPixels; ++row)
					std::memcpy(page->native.data() + std::size_t(row) * ChunkPixels,
								static_cast<const Uint8 *>(nativePixels->pixels) + std::size_t(row) * nativePixels->pitch,
								std::size_t(ChunkPixels) * 4);
			}
			page->results.reserve(std::size_t(pageTiles) * pageTiles);
			for (unsigned i = 0; i < tiles.size(); ++i)
			{
				if (int(i % ChunkTiles) >= pageTiles || int(i / ChunkTiles) >= pageTiles)
					continue;
				TerrainCellResult result{};
				result.index = i;
				result.input = tiles[i];
				result.pixels = base + (i / ChunkTiles) * n * pitch + (i % ChunkTiles) * n * 4;
				std::array<TerrainVisual::MaterialId, 4> palette;
				const unsigned paletteCount = tiles[i].recipe.palette(palette);
				result.keepMask = !nativePixels && tiles[i].discovered && !tiles[i].recipe.uniform() &&
					std::any_of(palette.begin(), palette.begin() + paletteCount,
								[&](auto id) { return compositor.animated(id); });
				page->results.push_back(std::move(result));
			}
			page->cellsPerJob = 16;
			page->jobs = (page->results.size() + page->cellsPerJob - 1) / page->cellsPerJob;
			page->remaining.store(page->jobs, std::memory_order_relaxed);
			page->wanted = frame;
			pending.push_back(page);
			submitPage(page, ahead ? BackgroundWork::Priority::Ahead : BackgroundWork::Priority::Visible);
		};
		// Graphics-thread work this frame for the workers: cells described for
		// launches, and native pages copied for reductions, a quarter as costly,
		// up to sixteen pages and two milliseconds; one launch always goes.
		constexpr std::size_t LaunchUnitsPerFrame = 4096;
		constexpr std::size_t ReductionUnits = ChunkTiles * ChunkTiles / 4;
		constexpr auto LaunchTime = std::chrono::microseconds(2000);
		std::size_t launchUnits = 0;
		// Time spent describing and copying for launches; checking and drawing
		// pages already composed does not count against it.
		std::chrono::steady_clock::duration launchSpent{};
		const auto launchTimeLeft = [&] { return !launchUnits || launchSpent < LaunchTime; };
		const auto timed = [&](auto &&work)
		{
			const auto started = std::chrono::steady_clock::now();
			work();
			launchSpent += std::chrono::steady_clock::now() - started;
		};
		const std::size_t pendingLimit = std::size_t(std::max(1u, BackgroundWork::workers())) * 4 + 4;
		// In-flight buffers are bounded in bytes too: at four times density a
		// page is 16 MiB. One launch always goes when nothing is in flight.
		constexpr std::size_t PendingBytes = 64u * 1024 * 1024;
		const auto pendingBytes = [&]
		{
			std::size_t total = 0;
			for (const auto &page : pending)
				total += (page->pixels ? std::size_t(page->pixels->pitch) * page->pixels->h
									   : page->cellPixels.size() * sizeof(Uint32)) +
						 page->native.size() * sizeof(Uint32);
			return total;
		};
		const auto roomFor = [&](std::size_t bytes) { return pending.empty() || pendingBytes() + bytes <= PendingBytes; };
		const int pageSide = pageTiles * 32 * resolution / downsample;
		const std::size_t pageBytes = std::size_t(pageSide) * pageSide * 4;
		const auto refreshesPending = [&]
		{ return std::size_t(std::count_if(pending.begin(), pending.end(), [](const auto &page) { return page->refresh && !page->done(); })); };
		// Pages composing ahead of the camera take no slot from visible work.
		const auto visiblePending = [&]
		{ return std::size_t(std::count_if(pending.begin(), pending.end(), [](const auto &page) { return !page->ahead; })); };
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
				const double dx = (x0 + int(i % columns)) * pageTiles + pageTiles * 0.5 - centreX;
				const double dy = (y0 + int(i / columns)) * pageTiles + pageTiles * 0.5 - centreY;
				return dx * dx + dy * dy;
			};
			// Missing pages with nothing to stand in for them, which show the
			// overview, go before those another density already covers.
			std::vector<char> covered(pageCount, 1);
			for (std::size_t i = 0; i < pageCount; ++i)
			{
				const int cx = x0 + int(i % columns), cy = y0 + int(i / columns);
				const Chunk *entry = findPage(cx, cy);
				if (!entry || !entry->valid)
					covered[i] = findStandIn((cx * pageTiles) & map.getMaskW(), (cy * pageTiles) & map.getMaskH(), standInPages);
			}
			std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
			{ return covered[a] != covered[b] ? covered[a] < covered[b] : distance(a) < distance(b); });
		}
		for (const std::size_t i : order)
			{
				const int cx = x0 + int(i % columns), cy = y0 + int(i / columns);
				const int wx = (cx * pageTiles) & map.getMaskW();
				const int wy = (cy * pageTiles) & map.getMaskH();
				const int screenX = (cx * pageTiles - vx) * 32, screenY = (cy * pageTiles - vy) * 32;
				// The page's cells this view shows.
				const std::uint64_t shownCells =
					std::uint64_t(std::min(right + vx + 1, (cx + 1) * pageTiles) - std::max(left + vx, cx * pageTiles)) *
					(std::min(bottom + vy + 1, (cy + 1) * pageTiles) - std::max(top + vy, cy * pageTiles));
				coverage.pageCells += shownCells;
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
					for (int y = -SourceHalo; y <= pageTiles + SourceHalo; ++y)
						for (int x = -SourceHalo; x <= pageTiles + SourceHalo; ++x)
							sources[(y + SourceHalo) * SourceSpan + x + SourceHalo] = map.vertexTerrainAt(wx + x, wy + y);
				}
				const auto revisionChanged = [&](const auto &revision)
				{ return pageRevision(compositor, revision.first) != revision.second; };
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
					for (int y = 0; y < pageTiles && sameContent; ++y)
						for (int x = 0; x < pageTiles; ++x)
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
					const int side = entry->pixels();
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
					// A page that arrived over a fallback fades in over it: the
					// other density beneath, or the overview where there is none.
					if (entry->arrived && deferring && now - entry->arrived < FadeMs)
					{
						drawStandIn(cx, cy, wx, wy);
						coverage.fadingCells += shownCells;
						copies.push_back({entry, screenX, screenY, Uint8(1 + 254 * (now - entry->arrived) / FadeMs)});
						continue;
					}
					entry->arrived = 0;
					copies.push_back({entry, screenX, screenY});
					continue;
				}
				const auto describeTiles = [&](std::array<Tile, ChunkTiles * ChunkTiles> &tiles)
				{
					if (sameTerrain && discoveryCurrent)
						tiles = entry->tiles;
					else
					for (int y = 0; y < pageTiles; ++y)
						for (int x = 0; x < pageTiles; ++x)
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
						if (!page->base && page->x == wx && page->y == wy && page->scale == scale &&
							page->downsample == reduction)
						{
							// The view wants a page composing ahead of it: queue it
							// again as visible work.
							if (page->ahead && !page->refresh)
							{
								page->ahead = false;
								submitPage(page);
							}
							return page.get();
						}
					return nullptr;
				};
				// A cold page in an interactive view: workers compose it, or reduce it
				// from a copy of a retained native page, launched within the frame's
				// budget, and the view draws a stand-in meanwhile.
				if (deferring && (!entry || !entry->valid))
				{
					const SDL_Surface *nativePixels = nullptr;
					const Chunk *nativeSource = nullptr;
					if (downsample > 1)
						for (auto &c : chunks)
							if (c->x == wx && c->y == wy && c->scale == 1 && c->downsample == 1 && c->valid &&
								c->seed == map.terrainSeed() && c->recipeTerrainRevision == terrainRevision &&
								c->discoveryStamped && c->discoveryWhole == wholeMap &&
								(wholeMap || (c->discoveryRevision == map.discoveryRevision() &&
											  c->discoveryTeams == visibleTeams)) &&
								!pageChanged(*c))
								if (auto *pixels = c->image ? c->image->getSDLSurface() : c->parked.get())
								{
									nativeSource = c.get();
									nativePixels = pixels;
								}
					const std::size_t units = nativeSource ? ReductionUnits : std::size_t(pageTiles) * pageTiles;
					if (auto *queued = pendingFor(resolution, downsample))
					{
						queued->wanted = frame;
						queued->fellBack = true;
					}
					else if (launchUnits + units <= LaunchUnitsPerFrame && launchTimeLeft() && visiblePending() < pendingLimit &&
							 roomFor(pageBytes + (nativeSource ? std::size_t(ChunkPixels) * ChunkPixels * 4 : 0)))
					{
						timed([&]
						{
							if (nativeSource)
								launch(wx, wy, nativeSource->tiles, sources, nativePixels);
							else
							{
								std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
								describeTiles(tiles);
								if (sheetsReady(tiles))
									launch(wx, wy, tiles, sources);
							}
						});
						if (!pending.empty() && pending.back()->x == wx && pending.back()->y == wy)
							pending.back()->fellBack = true;
						launchUnits += units;
					}
					else
						waiting = true;
					if (drawStandIn(cx, cy, wx, wy))
					{
						++standIns;
						coverage.standInCells += shownCells;
					}
					else
					{
						++uncovered;
						coverage.backdropCells += shownCells;
					}
					continue;
				}
				std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
				describeTiles(tiles);
				if (!sheetsReady(tiles))
				{
					// Its sheets are loading: draw what the page showed before,
					// or what stands in for a missing page.
					if (entry && entry->valid)
					{
						entry->used = frame;
						copies.push_back({entry, screenX, screenY});
					}
					else if (drawStandIn(cx, cy, wx, wy))
					{
						++standIns;
						coverage.standInCells += shownCells;
					}
					else
					{
						++uncovered;
						coverage.backdropCells += shownCells;
					}
					continue;
				}
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
				// An animation step of a valid page: the animation round below
				// recomposes its changed cells while the view keeps drawing the
				// shown phase.
				if (deferring && entry && entry->valid && entry->cpuComposed && artChanged && entry->tiles == tiles)
				{
					stampDiscovery(*entry);
					entry->discoveryStamped = true;
					entry->used = frame;
					copies.push_back({entry, screenX, screenY});
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
					fresh->span = pageTiles;
					fresh->downsample = downsample;
					std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
						SDL_CreateSurface(pageSide, pageSide, SDL_PIXELFORMAT_ARGB8888),
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
						const int side = entry->pixels();
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
                        for (int y = 0; y < pageTiles; ++y)
						for (int x = 0; x < pageTiles; ++x)
						{
							const int i = y * ChunkTiles + x, n = 32 * resolution / downsample;
							// Device pages draw uniform animated cells from shared tiles
							// at their density; the page keeps an earlier frame below.
							SDL_Rect tileSource;
							const bool overlay = !keyframing && gpu && tiles[i].discovered &&
								tiles[i].recipe.uniform() && !tiles[i].recipe.calmed() &&
								animatedTile(compositor, tiles[i].recipe, tileSource, resolution, downsample);
							if (wasValid && overlay && entry->overlay[i] && entry->tiles[i] == tiles[i])
								continue;
							if (wasValid && entry->tiles[i] == tiles[i] && !entry->stale[i] &&
								(!tiles[i].discovered ||
								 std::none_of(tiles[i].recipe.corners.begin(),
											  tiles[i].recipe.corners.end(), materialChanged) &&
                                 (keyframing || !tileSurfChanged(tiles[i].recipe, entry->surfTime, compositor))))
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
								else if (downsample > 1 && !recipe.uniform() && mixedCellsAtDensity())
								{
									// Seams and surf at the page's density, as the
									// direct pass draws them (Compositor::compose).
									SDL_Surface tile = *target;
									tile.w = tile.h = n;
									tile.pixels = static_cast<Uint8 *>(target->pixels) + y * n * target->pitch + x * n * 4;
									compositor.compose(recipe, &tile, 0, 0, maskScale, cellMask, nullptr, downsample);
								}
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
									if (wasValid && entry->tiles[i] == tiles[i] && !entry->stale[i] && cellMask)
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
							entry->image->markPixelsChanged(dirtyRects(dirty, 32 * resolution / downsample));
					}
					entry->tiles = tiles;
					++entry->tileStamp;
					stampDiscovery(*entry);
					entry->discoveryStamped = true;
					entry->materialRevisions.clear();
					entry->stale = {};
					entry->surfTime = compositor.surfTime();
					entry->hasShoreline = false;
					for (const auto &tile : tiles)
					{
						if (!tile.discovered)
							continue;
						entry->hasShoreline |= !keyframing && hasShoreline(tile.recipe, compositor);
						std::array<TerrainVisual::MaterialId, 4> palette;
						for (unsigned k = 0, n = tile.recipe.palette(palette); k < n; ++k)
						{
							const auto id = palette[k];
							if (std::none_of(entry->materialRevisions.begin(), entry->materialRevisions.end(),
										 [id](const auto &revision) { return revision.first == id; }))
								entry->materialRevisions.emplace_back(id, pageRevision(compositor, id));
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
				copies.push_back({entry, screenX, screenY});
			}
		// Animation rounds (see shownPhase), over the pages this frame draws, each
		// once: the view's own, stand-ins and pages fading in over them.
		if (deferring)
		{
			std::vector<Chunk *> drawn;
			// The cells of each drawn page the view shows, with a margin of one
			// cell for the camera's next step.
			std::vector<std::array<bool, ChunkTiles * ChunkTiles>> shownCells;
			for (const auto &copy : copies)
			{
				Chunk *page = copy.chunk;
				// A page drawn from its texture alone has no pixels to refresh; a
				// change of its content composes it again.
				if (!page->valid || !page->cpuComposed || page->resident ||
					(page->image && page->image->portableReleased()))
					continue;
				auto found = std::find(drawn.begin(), drawn.end(), page);
				if (found == drawn.end())
				{
					drawn.push_back(page);
					shownCells.emplace_back();
					found = drawn.end() - 1;
				}
				auto &shown = shownCells[std::size_t(found - drawn.begin())];
				const SDL_Rect cells = drawnCells(copy);
				const int x0 = std::max(cells.x, left - copy.x / 32 - 1), x1 = std::min(cells.x + cells.w - 1, right - copy.x / 32 + 1);
				const int y0 = std::max(cells.y, top - copy.y / 32 - 1), y1 = std::min(cells.y + cells.h - 1, bottom - copy.y / 32 + 1);
				for (int y = y0; y <= y1; ++y)
					for (int x = x0; x <= x1; ++x)
						shown[y * ChunkTiles + x] = true;
			}
			// Whether a page shows another phase than these revisions and surf
			// time: its pixels hold another, or cells it shows went stale.
			const auto behind = [&](std::size_t k, const std::vector<std::uint64_t> &revisions, unsigned now)
			{
				const Chunk &page = *drawn[k];
				for (int i = 0; i < ChunkTiles * ChunkTiles; ++i)
					if (page.stale[i] && shownCells[k][i])
						return true;
				return std::any_of(page.materialRevisions.begin(), page.materialRevisions.end(), [&](const auto &revision)
				{
					return revisions[revision.first] != revision.second ||
						(page.hasShoreline && surfStepChanged(revision.first, page.surfTime, compositor, now));
				});
			};
			// A round that settles lets the next one launch in the same frame,
			// unless its uploads were large: views with many animated page cells
			// then step every other frame rather than upload every frame.
			constexpr std::size_t RoundPipelineBytes = 8u * 1024 * 1024;
			for (int pass = 0; pass < 2; ++pass)
			{
				if (!round)
				{
					std::vector<std::uint64_t> live(compositor.catalog().materials.size());
					for (std::size_t id = 0; id < live.size(); ++id)
						live[id] = pageRevision(compositor, TerrainVisual::MaterialId(id));
					bool any = false;
					for (std::size_t k = 0; k < drawn.size() && !any; ++k)
						any = behind(k, live, compositor.surfTime());
					if (any)
						round = AnimationRound{{frameMaterials(), std::move(live)}, {}};
				}
				if (round)
				{
					const auto &phase = round->phase;
					// Finished refreshes write their cells into the page pixels; the
					// page's texture keeps the shown phase until the round publishes.
					const auto stage = [&](TerrainPendingPage &page)
					{
						if (page.failed || page.seed != map.terrainSeed())
							return;
						Chunk *target = nullptr;
						for (auto &chunk : chunks)
							if (chunk->x == page.x && chunk->y == page.y && chunk->scale == page.scale &&
								chunk->downsample == page.downsample)
								target = chunk.get();
						if (!target || !target->valid || target->tiles != page.tiles)
							return;
						auto *pixels = target->image ? target->image->getSDLSurface() : target->parked.get();
						if (!pixels)
							return;
						const int n = 32 * page.scale / page.downsample;
						for (auto &result : page.results)
						{
							const int i = int(result.index);
							for (int row = 0; row < n; ++row)
								std::memcpy(static_cast<Uint8 *>(pixels->pixels) + (i / ChunkTiles * n + row) * pixels->pitch +
												i % ChunkTiles * n * 4,
											result.pixels + std::size_t(row) * n * 4, std::size_t(n) * 4);
							target->opaque[i] = result.opaque;
							target->empty[i] = result.empty;
							target->staged[i] = true;
							target->stale[i] = false;
							auto &held = target->masks[i];
							if (!held && result.mask && releaseMasks(result.mask->bytes()))
								held = std::make_unique<HeldMask>(std::move(*result.mask), maskTotal);
						}
						target->hasStaged = true;
						for (int i = 0; i < ChunkTiles * ChunkTiles; ++i)
							target->stale[i] = target->stale[i] || page.skipped[i];
						target->materialRevisions = std::move(page.materialRevisions);
						target->surfTime = page.materials->surfTime;
					};
					std::erase_if(round->pages, [&](const std::shared_ptr<TerrainPendingPage> &page)
					{
						if (page->cancelled.load(std::memory_order_relaxed))
							return true;
						if (!page->done())
							return false;
						stage(*page);
						std::erase(pending, page);
						return true;
					});
					for (std::size_t k = 0; k < drawn.size(); ++k)
					{
						Chunk *page = drawn[k];
						if (!behind(k, phase.revisions, phase.materials->surfTime))
							continue;
						bool queued = false;
						for (auto &refresh : round->pages)
							if (refresh->x == page->x && refresh->y == page->y && refresh->scale == page->scale &&
								refresh->downsample == page->downsample)
							{
								refresh->wanted = frame;
								queued = true;
							}
						// Missing pages come first: animation would otherwise keep every
						// slot busy and leave pages away from the centre unbuilt. The
						// round then waits, and the view keeps showing one phase.
						if (queued || coldBacklog || launchUnits >= LaunchUnitsPerFrame || !launchTimeLeft() ||
							refreshesPending() >= pendingLimit / 2 || !roomFor(pageBytes / 4))
							continue;
						std::size_t units = 0;
						timed([&] { launchRefresh(*page, page->tiles, phase, shownCells[k], units); });
						launchUnits += units;
					}
					// No drawn page waits for this phase: every staged cell and the
					// overlay tiles change to it in this frame. A page whose sheets are
					// loading keeps its pixels rather than holding every other page.
					bool settled = round->pages.empty();
					for (std::size_t k = 0; k < drawn.size() && settled; ++k)
						settled = !behind(k, phase.revisions, phase.materials->surfTime) || !sheetsReady(drawn[k]->tiles);
					if (!settled)
						break;
					const std::size_t published = publishStaged();
					shownPhase = std::move(round->phase);
					round.reset();
					// A large step's uploads take this frame; the next round
					// launches in the next one.
					if (published > RoundPipelineBytes)
						break;
				}
			}
		}
		coldBacklog = waiting;
		// Pages around the view and ahead of the camera (`around`) compose in
		// advance, at worker priority behind visible work, once every visible
		// page has arrived; those already composing stay wanted meanwhile. They
		// take only leftover launch budget and in-flight room, and only cache
		// room that pages the view used recently do not need.
		if (deferring)
		{
			const auto pagesAround = ringAround(pageTiles);
			const int ax0 = pagesAround.x0, ax1 = pagesAround.x1, ay0 = pagesAround.y0, ay1 = pagesAround.y1;
			// Nearest the point the camera is heading to first.
			const double aimX = (left + right + 1) * 0.5 + vx + motionX * LookaheadFrames;
			const double aimY = (top + bottom + 1) * 0.5 + vy + motionY * LookaheadFrames;
			std::vector<std::pair<double, std::pair<int, int>>> ring;
			for (int cy = ay0; cy <= ay1; ++cy)
				for (int cx = ax0; cx <= ax1; ++cx)
				{
					if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1)
						continue;
					const double ex = cx * pageTiles + pageTiles * 0.5 - aimX, ey = cy * pageTiles + pageTiles * 0.5 - aimY;
					ring.push_back({ex * ex + ey * ey, {cx, cy}});
				}
			std::sort(ring.begin(), ring.end());
			// Cache room: what is free, plus pages idle long enough to give way.
			std::size_t idleBytes = 0, aheadBytes = 0, aheadPages = 0;
			for (const auto &chunk : chunks)
				if (chunk->used + AheadIdleFrames < frame)
					idleBytes += storageFor(chunk->scale, chunk->downsample);
			for (const auto &page : pending)
				if (page->ahead)
				{
					aheadBytes += storageFor(page->scale, page->downsample);
					++aheadPages;
				}
			const std::size_t aheadLimit = std::max(2u, BackgroundWork::workers());
			// Only once every visible page has arrived: work ahead must not delay it.
			const bool coldVisible = std::any_of(pending.begin(), pending.end(),
				[](const auto &page) { return !page->ahead && !page->refresh; });
			const auto pageStorageHere = storageFor(resolution, downsample);
			for (const auto &[distance, place] : ring)
			{
				const int cx = place.first, cy = place.second;
				const int wx = (cx * pageTiles) & map.getMaskW(), wy = (cy * pageTiles) & map.getMaskH();
				if (Chunk *entry = findPage(cx, cy); entry && entry->valid)
				{
					// Kept while the view may reach it.
					entry->used = std::max(entry->used, frame - 1);
					continue;
				}
				TerrainPendingPage *queued = nullptr;
				for (auto &page : pending)
					if (!page->base && page->x == wx && page->y == wy && page->scale == resolution &&
						page->downsample == downsample)
						queued = page.get();
				if (queued)
				{
					queued->wanted = frame;
					continue;
				}
				if (waiting || coldVisible || aheadPages >= aheadLimit || launchUnits + pageTiles * pageTiles > LaunchUnitsPerFrame ||
					!launchTimeLeft() ||
					pendingBytes() + pageBytes > PendingBytes / 2 ||
					bytes() + aheadBytes + pageStorageHere > budget + idleBytes)
					continue;
				timed([&]
				{
					std::array<Uint32, SourceSpan * SourceSpan> sources{};
					for (int y = -SourceHalo; y <= pageTiles + SourceHalo; ++y)
						for (int x = -SourceHalo; x <= pageTiles + SourceHalo; ++x)
							sources[(y + SourceHalo) * SourceSpan + x + SourceHalo] = map.vertexTerrainAt(wx + x, wy + y);
					std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
					for (int y = 0; y < pageTiles; ++y)
						for (int x = 0; x < pageTiles; ++x)
						{
							auto &t = tiles[y * ChunkTiles + x];
							t.discovered = wholeMap ||
								map.isMapPartiallyDiscovered(wx + x - 1, wy + y - 1, wx + x + 1, wy + y + 1, visibleTeams);
							if (t.discovered)
								t.recipe = compositor.describe(map, wx + x, wy + y);
						}
					if (sheetsReady(tiles))
						launch(wx, wy, tiles, sources, nullptr, true);
				});
				launchUnits += pageTiles * pageTiles;
				aheadBytes += pageStorageHere;
				++aheadPages;
			}
		}
		// The resident base pages this frame prepared materials for compose
		// behind visible work and the pages around the view, unless a renderer
		// change cleared the base since they were chosen.
		if (deferring && !waiting && baseReduction > 0)
			for (const SDL_Point &place : baseLaunches)
			{
				const int side = ChunkPixels / baseReduction, n = 32 / baseReduction;
				if (!launchTimeLeft() || launchUnits + ChunkTiles * ChunkTiles > LaunchUnitsPerFrame ||
					pendingBytes() + std::size_t(side) * side * 4 > PendingBytes)
					break;
				timed([&]
				{
					const int wx = place.x, wy = place.y;
					auto page = std::make_shared<TerrainPendingPage>();
					for (int y = -SourceHalo; y <= ChunkTiles + SourceHalo; ++y)
						for (int x = -SourceHalo; x <= ChunkTiles + SourceHalo; ++x)
							page->sources[(y + SourceHalo) * SourceSpan + x + SourceHalo] = map.vertexTerrainAt(wx + x, wy + y);
					for (int y = 0; y < ChunkTiles; ++y)
						for (int x = 0; x < ChunkTiles; ++x)
						{
							auto &t = page->tiles[y * ChunkTiles + x];
							t.discovered = wholeMap ||
								map.isMapPartiallyDiscovered(wx + x - 1, wy + y - 1, wx + x + 1, wy + y + 1, visibleTeams);
							if (t.discovered)
								t.recipe = compositor.describe(map, wx + x, wy + y);
						}
					if (!sheetsReady(page->tiles))
						return;
					page->base = true;
					page->ahead = true;
					page->x = wx;
					page->y = wy;
					page->downsample = baseReduction;
					page->mixedAtDensity = mixedCellsAtDensity();
					page->seed = map.terrainSeed();
					page->terrainRevision = terrainRevision;
					page->materials = frameMaterials();
					page->discoveryRevision = map.discoveryRevision();
					page->discoveryTeams = visibleTeams;
					page->discoveryWhole = wholeMap;
					page->pixels.reset(SDL_CreateSurface(side, side, SDL_PIXELFORMAT_ARGB8888));
					if (!page->pixels)
						throw std::bad_alloc();
					auto *pixels = static_cast<Uint8 *>(page->pixels->pixels);
					page->pitch = page->pixels->pitch;
					page->results.reserve(page->tiles.size());
					for (unsigned i = 0; i < page->tiles.size(); ++i)
					{
						TerrainCellResult result{};
						result.index = i;
						result.input = page->tiles[i];
						result.pixels = pixels + (i / ChunkTiles) * n * page->pitch + (i % ChunkTiles) * n * 4;
						page->results.push_back(std::move(result));
					}
					page->cellsPerJob = 16;
					page->jobs = (page->results.size() + page->cellsPerJob - 1) / page->cellsPerJob;
					page->remaining.store(page->jobs, std::memory_order_relaxed);
					page->wanted = frame;
					pending.push_back(page);
					submitPage(page, BackgroundWork::Priority::Ahead);
				});
				launchUnits += ChunkTiles * ChunkTiles;
			}
		if (keyframing)
			updateKeyframes(compositor, presentation, highResolution);
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
			if (page->base || page->wanted + 8 >= frame)
				return false;
			page->cancelled.store(true, std::memory_order_relaxed);
			return true;
		});
		// Compose the overlay tiles the view's cells use now, so each atlas
		// uploads once rather than again for every variant the draw meets.
		for (const auto &copy : copies)
		{
			const SDL_Rect cells = drawnCells(copy);
			const int x0 = std::max(cells.x, left - copy.x / 32), x1 = std::min(cells.x + cells.w - 1, right - copy.x / 32);
			const int y0 = std::max(cells.y, top - copy.y / 32), y1 = std::min(cells.y + cells.h - 1, bottom - copy.y / 32);
			for (int y = y0; y <= y1; ++y)
				for (int x = x0; x <= x1; ++x)
					if (copy.chunk->overlay[y * ChunkTiles + x])
					{
						SDL_Rect source;
						animatedTile(compositor, copy.chunk->tiles[y * ChunkTiles + x].recipe, source,
									 copy.chunk->scale, copy.chunk->downsample);
					}
		}
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
	// A capture is drawn once: it waits for the material sheets the map holds.
	if (tiledCapture) compositor.finishMaterials(map);
	prepareMaterials();
	if (tiledCapture && highResolution)
	{
		GAGCore::Sprite::finishHighResolution();
		prepareMaterials();
	}
	const auto plan =
		viewSamplingPlan(map, left, top, right, bottom, vx, vy, pageScale(compositor, tiledCapture), tiledCapture);
	if (mode == FallbackMode::EmergencyTiles || !plan.pageFits)
	{
		drawEmergencyTiles(map, left, top, right, bottom, vx, vy, visibleTeams, wholeMap,
						   plan.resolution);
		return;
	}
	const int tiles = pageTilesAt(plan.resolution);
	const int x0 = chunkOf(left + vx, tiles), x1 = chunkOf(right + vx, tiles);
	const int y0 = chunkOf(top + vy, tiles), y1 = chunkOf(bottom + vy, tiles);
	for (int cy = y0; cy <= y1; ++cy)
		for (int cx = x0; cx <= x1; ++cx)
		{
			const int pageLeft = std::max(left, cx * tiles - vx);
			const int pageTop = std::max(top, cy * tiles - vy);
			const int pageRight = std::min(right, (cx + 1) * tiles - vx - 1);
			const int pageBottom = std::min(bottom, (cy + 1) * tiles - vy - 1);
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
            const SDL_Rect cells=drawnCells(copy);
            SDL_Rect area{copy.x+cells.x*32,copy.y+cells.y*32,cells.w*32,cells.h*32},visible;
            if (SDL_GetRectIntersection(&area,&paintBounds,&visible)) {
                GAGCore::DrawPacket packet;packet.kind=GAGCore::DrawPacket::Kind::Blit;
                packet.texture=std::move(page);packet.blend=GAGCore::DrawPacket::Blend::Premultiplied;
                packet.linear=copy.chunk->scale>1 || copy.chunk->downsample>1;
                packet.source={(visible.x-copy.x)*resolution/downsample,(visible.y-copy.y)*resolution/downsample,
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
			const SDL_Rect cells = drawnCells(copy);
			SDL_Rect area{copy.x + cells.x * 32, copy.y + cells.y * 32, cells.w * 32, cells.h * 32}, visible;
			if (SDL_GetRectIntersection(&area, &paintBounds, &visible))
			{
				// Stand-in pages can have another density than the view.
				const int scale = copy.chunk->scale, reduction = copy.chunk->downsample;
				const auto draw = [&](SDL_Rect rect)
				{
					target.drawSurface(rect.x, rect.y, rect.w, rect.h, copy.chunk->image.get(),
						(rect.x - copy.x) * scale / reduction, (rect.y - copy.y) * scale / reduction,
						rect.w * scale / reduction, rect.h * scale / reduction, copy.alpha);
				};
                draw(visible);
				// Native pages stay readable for reductions; others keep their
				// pixels only while animation writes them.
				if (deferBuilds && !(copy.chunk->scale == 1 && copy.chunk->downsample == 1) &&
					copy.chunk->image->hasPixels() && overlayCompositor && !copy.chunk->hasStaged &&
					(copy.chunk->resident || !needsPagePixels(*copy.chunk, *overlayCompositor)))
					copy.chunk->image->releasePortablePixels();
				// Uniform animated cells over the page, from their shared tiles.
				if (overlayCompositor)
					for (int i = 0; i < ChunkTiles * ChunkTiles; ++i)
					{
						if (!copy.chunk->overlay[i]) continue;
						SDL_Rect cell{copy.x + i % ChunkTiles * 32, copy.y + i / ChunkTiles * 32, 32, 32}, shown, source;
						if (!SDL_GetRectIntersection(&cell, &visible, &shown)) continue;
						auto *set = animatedTile(*overlayCompositor, copy.chunk->tiles[i].recipe, source, scale, reduction);
						if (!set) continue;
						if (set->changed)
						{
							set->atlas->markPixelsChanged();
							set->changed = false;
						}
						target.drawSurface(shown.x, shown.y, shown.w, shown.h, set->atlas.get(),
							source.x + (shown.x - cell.x) * scale / reduction, source.y + (shown.y - cell.y) * scale / reduction,
							shown.w * scale / reduction, shown.h * scale / reduction, copy.alpha);
					}
			}
			// Animated cells over the page: this frame's keyframe, then the
			// next one over it as far as the animation has gone towards it.
			// Each pass's cells do not overlap, so each atlas batches per pass.
			for (int pass = 0; pass < 2; ++pass)
				for (auto k = copy.keyframeBegin; k < copy.keyframeEnd; ++k)
				{
					const auto &cell = keyframeDraws[k];
					auto *atlas = cell.set->atlas.get();
					const auto &source = pass ? cell.second : cell.first;
					const Uint8 alpha = pass ? cell.secondAlpha : cell.firstAlpha;
					if (!atlas || !alpha)
						continue;
					const auto &d = cell.destination;
					target.drawSurface(d.x, d.y, d.w, d.h, atlas, source.x, source.y, source.w, source.h, alpha);
				}
			continue;
		}
		const SDL_Rect cells = drawnCells(copy);
		SDL_Rect bounds{copy.x + cells.x * 32, copy.y + cells.y * 32, cells.w * 32, cells.h * 32};
		if (!SDL_GetRectIntersection(&bounds, &paintBounds, &bounds))
			continue;
		for (const auto &run : copy.chunk->opaqueRuns)
		{
			SDL_Rect destination{copy.x + run.rect.x, copy.y + run.rect.y, run.rect.w, run.rect.h},
				visible;
			if (SDL_GetRectIntersection(&destination, &bounds, &visible))
				target.drawSurface(visible.x, visible.y, run.image.get(), visible.x - destination.x,
								   visible.y - destination.y, visible.w, visible.h);
		}
		for (int y = cells.y; y < cells.y + cells.h; ++y)
			for (int x = cells.x; x < cells.x + cells.w; ++x)
			{
				if (copy.chunk->opaque[y * ChunkTiles + x] || copy.chunk->empty[y * ChunkTiles + x])
					continue;
				SDL_Rect area{copy.x + x * 32, copy.y + y * 32, 32, 32}, visible;
				if (SDL_GetRectIntersection(&area, &bounds, &visible))
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
	for (auto &page : basePages)
		if (page && page->keyframes && page->used != frame)
			idle.push_back(page.get());
	std::sort(idle.begin(), idle.end(), [](auto *a, auto *b) { return a->used < b->used; });
	for (auto *chunk : idle)
	{
		if (*maskTotal + needed <= budget)
			break;
		for (auto &held : chunk->masks)
			held.reset();
		if (chunk->keyframes)
			for (auto &entry : chunk->keyframes->entries)
				entry.mask.reset();
	}
	return *maskTotal + needed <= budget;
}
void SoftwareTerrainCache::trimIdlePages(std::size_t limit)
{
	std::size_t erased = 0;
	std::erase_if(chunks, [&](const auto &chunk) {
		if (erased >= limit || chunk->used >= trimBefore) return false;
		if (gpuPages) gpuPages->erase(*chunk);
		++erased;
		return true;
	});
	if (erased >= limit) return;
	// Every idle page is gone: so are the idle animated tiles and, without
	// pending pages, the material snapshot workers composed from.
	std::erase_if(animatedTiles, [&](const auto &entry) { return entry.second.used < trimBefore; });
	std::erase_if(uniformKeyframes, [&](const auto &entry) { return entry.second->used < trimBefore; });
	if (pending.empty()) workerMaterials.reset();
	trimBefore = 0;
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
			const std::size_t side = std::size_t(chunk->pixels());
			total -= side * side * 4;
		}
	}
	return total;
}

std::size_t SoftwareTerrainCache::pixelBytes() const
{
	std::size_t total = 0;
	const auto add = [&](const SDL_Surface *pixels) { if (pixels) total += std::size_t(pixels->pitch) * pixels->h; };
	for (const auto &chunk : chunks)
	{
		if (chunk->image) add(chunk->image->getSDLSurface());
		add(chunk->parked.get());
	}
	return total;
}

void SoftwareTerrainCache::clearBase()
{
	basePages.clear();
	baseReduction = baseColumns = baseRows = 0;
}

std::size_t SoftwareTerrainCache::baseTextureBytes() const
{
	std::size_t total = 0;
	for (const auto &page : basePages)
		if (page && page->image)
			total += std::size_t(page->pixels()) * page->pixels() * 4;
	return total;
}

std::size_t SoftwareTerrainCache::basePixelBytes() const
{
	std::size_t total = 0;
	for (const auto &page : basePages)
		if (page && page->image)
			if (const auto *pixels = page->image->getSDLSurface())
				total += std::size_t(pixels->pitch) * pixels->h;
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
