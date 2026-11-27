// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDLGraphicContext.h>
#include "terrain/TerrainCompositor.h"
#include "AnimationClock.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

class SceneMap;
class TerrainRegistry;
class MapAssetBundle;
class GpuTerrainPageCache;
struct TerrainPendingPage;
// Shared presentation page cache. The historical name remains for existing view
// controls/benchmarks; software and GPU consume the same composed coverage.
class SoftwareTerrainCache
{
  public:
	static constexpr int ChunkTiles = 16, ChunkPixels = ChunkTiles * 32;
	// Tiles along a page's side at a composition scale: ChunkTiles at native
	// density and fewer above, so every page is ChunkPixels (512) wide. A view
	// at three or four times density then needs little more than its own
	// area in pages, where 16-tile pages (2048 pixels at four times) cost up
	// to twice it for their alignment and left the view at a coarser density.
	// Page arrays keep the ChunkTiles stride; a smaller page uses their
	// top-left corner.
	static constexpr int pageTilesAt(int scale)
	{
		return std::clamp(ChunkTiles / std::max(1, scale), 4, ChunkTiles);
	}
	// Vertices beyond a page's corners that its cells read: contextual borders
	// need one, shore calm searches ShoreCalmSearch toward land and whole-stone
	// path edges StoneSearch.
	static constexpr int SourceHalo =
		std::max({1, TerrainVisual::ShoreCalmSearch, TerrainVisual::StoneSearch});
	static constexpr int SourceSpan = ChunkTiles + 1 + 2 * SourceHalo;
	// Software pages; the GPU limits follow GAGCore::platformRenderBudget().terrain.
	static constexpr std::size_t Budget = 32u * 1024u * 1024u;
	// GPU density selection and resident page textures.
	static std::size_t GPUBudget();
	// GPU-mode pages in all: the density allowance, plus inactive zoom pixels
	// kept in a bounded CPU reserve rather than recomposed.
	static std::size_t GPUCacheBudget();
	// Coverage kept for mixed cells next to animated materials, so a phase change
	// re-blends their textures instead of re-sampling coverage. Half of the software
	// density budget (a quarter of the GPU one); cells beyond it compose without a kept mask.
	static constexpr std::size_t MaskBudget = Budget / 2;
	static std::size_t GPUMaskBudget() { return GPUBudget() / 4; }
	struct HeldMask
	{
		TerrainVisual::Compositor::CellMask mask;
		// Atomic: a refresh job on a worker can hold the last reference.
		std::shared_ptr<std::atomic<std::size_t>> total;
		HeldMask(TerrainVisual::Compositor::CellMask m, std::shared_ptr<std::atomic<std::size_t>> t)
			: mask(std::move(m)), total(std::move(t))
		{
			*total += mask.bytes();
		}
		~HeldMask() { *total -= mask.bytes() + animated.capacity() * sizeof(std::uint32_t); }
		// Compositor::animatedSamples of the mask, made on its first animation refresh.
		std::vector<std::uint32_t> animated;
		bool planned = false;
		const std::vector<std::uint32_t> &animatedSamples(const TerrainVisual::Compositor &compositor)
		{
			if (!planned)
			{
				animated = compositor.animatedSamples(mask);
				animated.shrink_to_fit();
				*total += animated.capacity() * sizeof(std::uint32_t);
				planned = true;
			}
			return animated;
		}
		HeldMask(const HeldMask &) = delete;
		HeldMask &operator=(const HeldMask &) = delete;
	};
	struct Tile
	{
		TerrainVisual::Recipe recipe;
		bool discovered = false;
		bool operator==(const Tile &) const = default;
	};
	struct OpaqueRun
	{
		SDL_Rect rect;
		std::unique_ptr<GAGCore::DrawableSurface> image;
	};
	struct KeyframeSet;
	struct Chunk
	{
		int x = 0, y = 0, scale = 1, downsample = 1;
		int span = ChunkTiles; // Tiles along each side: pageTilesAt(scale).
		// Pixels along each side of the page's surface.
		int pixels() const { return span * 32 * scale / downsample; }
		Uint32 seed = 0; // Map terrain seed the page was composed with.
		Uint64 recipeTerrainRevision = 0;
		// Corner vertices and the SourceHalo used by contextual borders and shore calm.
		std::array<Uint32, SourceSpan * SourceSpan> sources{};
		std::array<Tile, ChunkTiles * ChunkTiles> tiles{};
		// Empty cells expose only the separately drawn ocean and submit no software blit.
		std::array<bool, ChunkTiles * ChunkTiles> opaque{}, empty{};
		// Device pages draw these uniform animated cells from animatedTiles
		// over the page, which keeps an earlier frame of them underneath.
		std::array<bool, ChunkTiles * ChunkTiles> overlay{};
		// An uploaded drawable owns the pixels; a retired page keeps them in parked.
	// Exactly one owner exists, so texture retirement never discards composition.
	std::unique_ptr<TerrainVisual::Surface> image;
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> parked{nullptr, SDL_DestroySurface};
		std::vector<OpaqueRun> opaqueRuns;
		// Shared with refresh jobs, which read a mask while the page keeps it.
		std::array<std::shared_ptr<HeldMask>, ChunkTiles * ChunkTiles> masks;
		bool valid = false;
		// Device-composed pages have no matching pixels in image/parked.
		bool cpuComposed = true;
		// Texture changes and shoreline animation invalidate independently.
		unsigned surfTime = 0;
		bool hasShoreline = false;
		std::vector<std::pair<TerrainVisual::MaterialId, std::uint64_t>> materialRevisions;
		std::uint64_t used = 0;
		std::uint64_t textureUsed = 0;
		// The discovery state `tiles` was checked against, so unchanged frames
		// skip checking every cell.
		Uint64 discoveryRevision = 0;
		Uint32 discoveryTeams = 0;
		bool discoveryWhole = false, discoveryStamped = false;
		// When (SDL_GetTicks) a page arrived where the view had drawn a stand-in
		// or the overview; it fades in over FadeMs. Zero once settled.
		Uint64 arrived = 0;
		// When the portable renderer first held a fade until the keyframes of
		// the page's animated cells arrived (see updateKeyframes).
		Uint64 arrivalHeld = 0;
		// Cells an animation round wrote into the pixels but not yet to the
		// texture, which keeps the shown phase until the round publishes.
		std::array<bool, ChunkTiles * ChunkTiles> staged{};
		bool hasStaged = false;
		// Cells out of view that rounds left at an earlier phase than the
		// page's revisions; a round recomposes them once the view shows them.
		std::array<bool, ChunkTiles * ChunkTiles> stale{};
		// A page of the resident base (see basePages): its pixels go once its
		// texture holds them.
		bool resident = false;
		// The portable renderer's keyframes of this page's animated cells
		// (see KeyframeSet), and a count of changes to `tiles` they follow.
		std::shared_ptr<KeyframeSet> keyframes;
		// Calm overlays of the page's uniform calmed animated cells: each such
		// cell draws its material's shared keyframes, then this still overlay
		// (the calm mean at 1 - keep) over them, instead of keyframes of its own.
		std::shared_ptr<KeyframeSet> calmOverlays;
		std::uint64_t tileStamp = 0;
	};
	static constexpr std::size_t ChunkStorageBytes =
		ChunkPixels * ChunkPixels * 4 + sizeof(Chunk) +
		ChunkTiles * ChunkTiles *
			(sizeof(OpaqueRun) + sizeof(GAGCore::DrawableSurface) + sizeof(SDL_Surface) +
			 std::tuple_size_v<decltype(TerrainVisual::Recipe::corners)> *
				 sizeof(std::pair<TerrainVisual::MaterialId, std::uint64_t>));
	struct Copy
	{
		Chunk *chunk;
		int x, y; // The page's origin on screen, in logical pixels.
		Uint8 alpha = 255; // Below opaque while a page fades in.
		// The part of the page drawn, in logical pixels from its origin: a
		// coarser stand-in draws only the cells of the page it stands in for.
		SDL_Rect clip{0, 0, ChunkPixels, ChunkPixels};
		// The page's keyframe draws (keyframeDraws), drawn over it, then its
		// calm overlay draws (overlayDraws) over those.
		std::uint32_t keyframeBegin = 0, keyframeEnd = 0;
		std::uint32_t overlayBegin = 0, overlayEnd = 0;
	};
	//! How long a page that arrives over a fallback takes to fade in.
	static constexpr Uint64 FadeMs = 150;
	//! The opacity the portable renderer draws an animated cell's next keyframe
	//! at over its current one: the page's opacity times the share of the
	//! keyframe interval elapsed (keyframeBlend), continuous in time.
	static Uint8 keyframeAlpha(Uint8 pageAlpha, float weight)
	{
		return Uint8(std::lround(float(pageAlpha) * std::clamp(weight, 0.f, 1.f)));
	}
	//! The keyframe the portable renderer draws an animated cell from at
	//! `time` (keys are `interval` ticks apart) and the weight of the next one:
	//! the share of the interval elapsed, fraction of a tick included.
	struct KeyframeBlend
	{
		std::int64_t key;
		float weight;
	};
	static KeyframeBlend keyframeBlend(AnimationTime time, int interval)
	{
		const auto cycle = time.cycle(interval);
		return {cycle.index, cycle.within};
	}

  private:
    bool tryBuildCpuPage(Chunk&, const std::array<Tile, ChunkTiles*ChunkTiles>&,
        TerrainVisual::Compositor&, SDL_Surface*, bool wasValid, std::size_t budget,
        bool nativeAvailable = false, std::array<bool, ChunkTiles*ChunkTiles> *dirty = nullptr);
	bool prepareAtResolution(const SceneMap &, GAGCore::Sprite &, int left, int top, int right,
							 int bottom, int vx, int vy, Uint32 visibleTeams, bool wholeMap,
							 AnimationTime animationTime, int preferredResolution, bool tiledCapture = false,
							 int preferredDownsample = 0);
	// Frees kept masks of pages not drawn this frame, oldest first, until
	// `needed` more bytes fit the mask budget. Returns whether they now fit.
	bool releaseMasks(std::size_t needed);
	std::vector<std::unique_ptr<Chunk>> chunks;
    std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> cpuMaterialLease;
	std::shared_ptr<const TerrainRegistry> registry;
    std::shared_ptr<const MapAssetBundle> assets;
	std::vector<Copy> copies;
	// One composed tile per variant of each animated opaque material at each
	// page density, shared by every uniform cell of it on device pages. An
	// animation step then recomposes and uploads one small atlas instead of
	// each page's water.
	struct AnimatedTiles
	{
		std::unique_ptr<TerrainVisual::Surface> atlas;
		std::vector<std::uint64_t> revisions; // Composed texture revision + 1, per variant.
		std::vector<char> opaque;
		int columns = 0;
		bool changed = false;
		std::uint64_t used = 0; // Frame it last served a cell.
	};
	std::map<std::tuple<unsigned, int, int>, AnimatedTiles> animatedTiles; // By material, scale and reduction.
	TerrainVisual::Compositor *overlayCompositor = nullptr; // Set by prepare for draw.
	// An animation phase: a material snapshot and the texture revisions it holds.
	struct AnimationPhase
	{
		std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> materials;
		std::vector<std::uint64_t> revisions; // By material.
	};
	// While pages build in the background, the view shows one animation phase
	// everywhere: the overlay tiles compose from it, and a round brings every
	// drawn page's animated cells to the next phase off-thread, staging them
	// into page pixels, then publishes all of them and the overlay tiles in one
	// frame. Empty when pages compose synchronously at the live phase.
	AnimationPhase shownPhase;
	struct AnimationRound
	{
		AnimationPhase phase;
		std::vector<std::shared_ptr<TerrainPendingPage>> pages; // Refreshes composing.
	};
	std::optional<AnimationRound> round;
	// Writes the staged cells of every page to its texture; returns their bytes.
	std::size_t publishStaged();
	// The portable renderer's animated terrain (see "Animated terrain on the
	// portable renderer" in docs/assets/terrain-rendering.md). Pages hold
	// still pixels; every animated cell draws over its page each frame as a
	// crossfade of two keyframes, composed by the workers at fixed animation
	// times, keyframeInterval ticks apart, ahead of when they show.
	bool keyframing = false;
	int keyframeInterval = 0; // Animation ticks between keyframes; 0 without animated materials.
	// The material snapshot a keyframe composes from: the compositor
	// prepared at the keyframe's time.
	struct KeySnapshot
	{
		std::int64_t key = 0;
		std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> materials;
		// Changes when the snapshot's pixels do; a keyframe composed from an
		// older generation is drawn until one from this generation replaces it.
		std::uint64_t generation = 0;
		// Revisions of the still materials, which every keyframe also holds.
		std::uint64_t epoch = 0;
		// Per animated material, per variant: the artwork the snapshot holds (0: none).
		std::vector<std::vector<std::uint64_t>> stamps;
		bool stale = false; // Taken from older artwork, or missing a variant a cell needs.
		std::uint64_t taken = 0; // Frame.
	};
	std::vector<KeySnapshot> keySnapshots;
	std::uint64_t keyGeneration = 0;
	// The key every animated cell of the view shows (see updateKeyframes):
	// the presentation's key while the workers keep up, else an earlier one
	// all of them hold, which walks forward as theirs arrive. INT64_MIN: none.
	std::int64_t viewKey = std::numeric_limits<std::int64_t>::min();
	// Keys the view may fall behind the presentation before it jumps to it.
	static constexpr std::int64_t MaxKeyLag = 8;
	// The first key keyframes are composed and snapshots kept from: the view's.
	std::int64_t keyBase(std::int64_t now) const
	{
		return viewKey != std::numeric_limits<std::int64_t>::min() && viewKey <= now && now - viewKey <= MaxKeyLag
				   ? viewKey : now;
	}
	struct KeyframeJob;
	std::vector<std::shared_ptr<KeyframeJob>> keyframeJobs;
	// Uniform uncalmed animated cells share one keyframe per variant at each
	// page density: by material, scale and reduction.
	std::map<std::tuple<unsigned, int, int>, std::shared_ptr<KeyframeSet>> uniformKeyframes;
	struct KeyframeDraw
	{
		KeyframeSet *set;
		SDL_Rect destination, first, second;
		Uint8 firstAlpha, secondAlpha; // 0: not drawn.
	};
	std::vector<KeyframeDraw> keyframeDraws;
	struct OverlayJob;
	std::vector<std::shared_ptr<OverlayJob>> overlayJobs;
	struct OverlayDraw
	{
		KeyframeSet *set;
		SDL_Rect destination, source;
		Uint8 alpha;
	};
	std::vector<OverlayDraw> overlayDraws;
	std::size_t keyframeMisses = 0;
	std::size_t sharedKeyframeCells = 0, calmOverlayCells = 0, ownKeyframeCells = 0, stillKeyframeCells = 0,
				tornKeyframeCells = 0;
	// The keyframe this frame's animation time prepares the compositor for.
	std::int64_t keyframeTarget(int time);
	// Takes or refreshes that keyframe's snapshot after the compositor prepared it.
	void takeKeySnapshot(TerrainVisual::Compositor &, std::int64_t key, bool highResolution);
	std::uint64_t keyStamp(TerrainVisual::Compositor &, TerrainVisual::MaterialId, unsigned variant,
						   std::int64_t key, bool highResolution);
	std::uint64_t stillEpoch(const TerrainVisual::Compositor &) const;
	KeySnapshot *keySnapshot(std::int64_t key);
	// Installs finished keyframes, launches missing ones for the cells the
	// view shows (and a margin) and lists this frame's keyframe draws.
	void updateKeyframes(TerrainVisual::Compositor &, AnimationTime time, bool highResolution);
	void classifyKeyframes(Chunk &, const TerrainVisual::Compositor &);
	void dropKeyframes();
	// The revision a page records for a material: animated materials do not
	// change keyframed pages, so they record none.
	std::uint64_t pageRevision(const TerrainVisual::Compositor &, TerrainVisual::MaterialId) const;
	// The overlay tile of a uniform cell, composed for the current revision.
	// Null when its material does not animate or its tile is not opaque.
	AnimatedTiles *animatedTile(TerrainVisual::Compositor &, const TerrainVisual::Recipe &, SDL_Rect &source,
								int scale, int reduction);
	std::uint64_t frame = 0, hits = 0, rebuilds = 0;
	std::uint64_t textureUse = 0, reductions = 0;
	// Visible cold pages drawn from another density, and those with none, where
	// the overview shows through; counted once a frame each, for benchmarks.
	std::uint64_t standIns = 0, uncovered = 0;
	// This frame's visible cells by source, and the pages (world tile origins)
	// with nothing to draw, where the overview must show beneath.
	struct FrameCoverage
	{
		std::uint64_t pageCells = 0, standInCells = 0, backdropCells = 0, fadingCells = 0;
	};
	FrameCoverage coverage;
	std::vector<SDL_Point> uncoveredPages;
	// The portable renderer's resident base: the whole map in native-density
	// pages (ChunkTiles wide) reduced to baseReduction, composed on the workers
	// behind visible work from the first interactive frame and never evicted.
	// A missing page whose other densities do not cover it draws its base
	// page, textured art, rather than the flat-coloured overview. Indexed by
	// page row and column; null until composed.
	std::vector<std::unique_ptr<Chunk>> basePages;
	int baseReduction = 0, baseColumns = 0, baseRows = 0;
	void clearBase();
	// Bytes of every chunk's kept masks; shared so a mask outliving the cache is safe.
	std::shared_ptr<std::atomic<std::size_t>> maskTotal = std::make_shared<std::atomic<std::size_t>>(0);
	SDL_Rect paintBounds{};
	// Composition scale and subsequent page reduction are separate: reduction
	// filters the native material result, never its individual source textures.
	// Only GPU pages reduce; at most one of these factors can exceed one.
	int resolution = 1;
	int downsample = 1;
	int pageTiles = ChunkTiles; // pageTilesAt(resolution).
	// The last page admission logged (TERRAIN_PAGE_ADMISSION): wanted scale,
	// scale, reduction and why it is coarser, to log changes only.
	std::tuple<int, int, int, const char *> lastAdmission{};
	bool gpu = false;
	bool composedHighResolution = false; // Whether the last view composed HD material frames.
    std::shared_ptr<GpuTerrainPageCache> gpuPages;
    std::uint64_t gpuPageGeneration = 0;
	// Pages composed by background workers while the view draws stand-ins.
	std::vector<std::shared_ptr<TerrainPendingPage>> pending;
	bool incomplete = false;
	// The last material snapshot given to workers (see prepareAtResolution).
	std::shared_ptr<const TerrainVisual::Compositor::CpuMaterials> workerMaterials;
	std::uint64_t materialsUsed = 0; // Frame of the last launch that used it.
	std::uint64_t resetGeneration = 0; // GraphicContext::renderResetGeneration last seen.
	// A missing page found no build slot last frame, so animation refreshes wait.
	bool coldBacklog = false;
	// The camera's recent motion, in tiles per prepared frame, smoothed, and
	// the view's growth (a zoom out): pages ahead of it compose in advance.
	double motionX = 0, motionY = 0, growth = 0, lastCentreX = 0, lastCentreY = 0;
	int lastSpan = 0;
	bool motionKnown = false;
	void cancelPending();

  public:
    struct WorkMetrics
    {
        std::uint64_t batches = 0, jobs = 0, cells = 0, serialPages = 0, batchNs = 0;
        std::size_t peakReservedBytes = 0;
    };
    WorkMetrics workMetrics;
	enum class FallbackMode
	{
		StreamPages,
		// Same coverage geometry, but fractional sampling and HD mip neighborhoods
		// can differ from pages. Requires only one reusable native/HD tile.
		EmergencyTiles
	};
	// Stream the same canonical pages as a full-view cache, with at most one
	// page alive. EmergencyTiles is explicit for diagnostics and severe limits.
	static void drawUncached(const SceneMap &, GAGCore::Sprite &, int left, int top, int right,
							 int bottom, int vx, int vy, Uint32 visibleTeams, bool wholeMap,
							 int animationTime = 0, FallbackMode mode = FallbackMode::StreamPages,
							 bool tiledCapture = false);
	bool enabled = true;
	// Interactive views on accelerated page renderers compose missing pages on
	// background workers instead of within the frame. Until a page arrives the
	// view draws the same page at another density, or nothing, and complete()
	// is false so the caller can draw the overview beneath. Captures, the
	// editor and other complete draws leave this off.
	bool deferBuilds = false;
	bool complete() const { return !incomplete; }
	//! Animated cells the last prepare showed without both of their current
	//! keyframes (portable renderer): they hold a still or a single keyframe.
	std::size_t missingKeyframeCells() const { return keyframeMisses; }
	//! Animated cells the portable renderer drew last frame (or tried to), by how
	//! they animate: from a material's shared keyframes alone, from them with a
	//! still calm overlay, or from keyframes composed for the cell itself; and
	//! of them, those that showed their page's still pixels, with no keyframe
	//! of any density to draw, and those drawn at another phase (key and
	//! crossfade) than most of the view's cells.
	struct KeyframeCells
	{
		std::size_t shared = 0, calmOverlay = 0, own = 0, still = 0, torn = 0;
	};
	KeyframeCells keyframeCells() const
	{
		return {sharedKeyframeCells, calmOverlayCells, ownKeyframeCells, stillKeyframeCells, tornKeyframeCells};
	}
	std::size_t pendingPages() const { return pending.size(); }
	// Benchmark override for comparing stable page density with adaptive sampling.
	int benchmarkDownsample = 0;
	bool prepare(const SceneMap &, GAGCore::Sprite &, int left, int top, int right, int bottom,
				 int vx, int vy, Uint32 visibleTeams, bool wholeMap, AnimationTime animationTime = {},
				 bool tiledCapture = false);
	void draw(GAGCore::GraphicContext &);
	std::size_t bytes() const;
	std::size_t residentTextureBytes() const;
	// CPU pixels pages hold: drawables that kept theirs plus parked pixels.
	// bytes() is the budget's accounting, which also reserves texture storage.
	std::size_t pixelBytes() const;
	int samplingResolution() const { return resolution; }
	int samplingReduction() const { return downsample; }
	std::uint64_t cacheHits() const { return hits; }
	std::uint64_t cacheRebuilds() const { return rebuilds; }
	std::uint64_t cacheReductions() const { return reductions; }
	std::uint64_t cacheStandIns() const { return standIns; }
	std::uint64_t cacheUncovered() const { return uncovered; }
	//! The last prepare's visible cells by source (see TerrainFallbackMetrics).
	const FrameCoverage &frameCoverage() const { return coverage; }
	//! World tile origins of the last prepare's pages with nothing to draw:
	//! the only places the overview backdrop shows.
	const std::vector<SDL_Point> &backdropPages() const { return uncoveredPages; }
	std::size_t maskBytes() const { return *maskTotal; }
	//! Bytes of the resident base's page textures and the pixels it still
	//! holds, outside the page budget (see basePages).
	std::size_t baseTextureBytes() const;
	std::size_t basePixelBytes() const;
	//! Bytes of the portable renderer's animation keyframes: their textures
	//! and the CPU pixels they upload from (each half of it).
	std::size_t keyframeBytes() const;
	//! Pixels per cell of the resident base, or 0 without one.
	int basePixelsPerCell() const { return baseReduction ? 32 / baseReduction : 0; }
	// A torus visits every page each frame. Keep its complete terrain working
	// set resident instead of selecting density independently for each tile.
	static int capturePixelsPerCell(int width, int height);

  private:
	// Pages not drawn since a presentation trim go, a few a frame (all at once
	// when backgrounded); zero when no trim is pending.
	std::uint64_t trimBefore = 0;
	void trimIdlePages(std::size_t limit);
	void trimmed(GAGCore::PresentationTrim reason)
	{
		trimBefore = frame;
		if (reason == GAGCore::PresentationTrim::Background) trimIdlePages(SIZE_MAX);
	}
	// Stays bound to the cache it was built in: assigning another cache's
	// pages leaves this cache's subscription as it is.
	struct TrimHook
	{
		GAGCore::PresentationTrimSubscription subscription;
		explicit TrimHook(SoftwareTerrainCache *cache)
			: subscription([cache](GAGCore::PresentationTrim reason) { cache->trimmed(reason); }) {}
		TrimHook(const TrimHook &) = delete;
		TrimHook &operator=(const TrimHook &) { return *this; }
		TrimHook &operator=(TrimHook &&) { return *this; }
	};
	// Last, so it unsubscribes before the pages it trims are destroyed.
	TrimHook trimHook{this};
};
