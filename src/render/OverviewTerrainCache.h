// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace GAGCore { class DrawableSurface; }
namespace TerrainVisual { class Surface; class Catalog; }
class SceneMap;
class TerrainRegistry;
class ResourceRegistry;
class MapAssetBundle;

// Canonical CPU palette pages, each its own texture. Neither depends on HD
// artwork or animation phase.
//
// A page covers 64 by 64 cells (the whole map when it is smaller): a 256 by
// 256 map is 16 textures, so a device that uploads a few new textures a frame
// (DrawableSurface::takeDeviceUpload) fills a view in a frame or two rather
// than in seconds, and prewarmed pages upload ahead of the zoom that shows
// them (uploadAhead).
//
// Each cell is `samples` x `samples` palette samples. A visible resource adds a
// square in its minimap colour, centred in the cell and growing with its stock,
// so the terrain around it stays readable.
class OverviewTerrainCache
{
  public:
	static constexpr int FewSamples = 4, ManySamples = 8;
	//! Samples per cell axis for a cell covering `pixelsPerCell` device pixels:
	//! many once a cell is large enough to show every stock size, few otherwise.
	//! `current` (0 for none) adds hysteresis so a zoom near the threshold does
	//! not recompose every page back and forth. Many samples also need the image,
	//! `cells` long on its longer side, to fit `maximumTexture` (0 for no limit).
	static int samplesFor(double pixelsPerCell, int current, int cells, int maximumTexture);
	//! Side of the resource square, in samples: 1 at the lowest stock, rounding
	//! down to half the cell only at full stock. Zero capacity counts as full.
	static int resourceSquare(unsigned stock, unsigned capacity, int samples);

	//! Composes the view into `target`, `columns * samples` by `rows * samples`.
	void update(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
				Uint32 visibleTeams, bool wholeMap, int samples, GAGCore::DrawableSurface &target);
	//! Draws the view's pages into `target` at map pixels (tile * 32), each page
	//! its own texture, so panning and zooming upload nothing new. Unless
	//! `complete`, cold pages compose within a per-frame budget; until they are
	//! ready a page draws its other density, or nothing.
	//! `shownPages`, when given, lists the world tile origins (multiples of
	//! BackdropTiles) of the only blocks whose drawing shows, such as a backdrop
	//! beneath missing detailed pages; metrics count only those.
	void draw(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
			  Uint32 visibleTeams, bool wholeMap, int samples, GAGCore::DrawableSurface &target,
			  Uint8 alpha, bool complete, const std::vector<SDL_Point> *shownPages = nullptr);
	//! Visible cells of the last draw by source (see TerrainFallbackMetrics).
	struct DrawMetrics
	{
		std::uint64_t cells = 0, other = 0, base = 0, holes = 0, fading = 0;
	};
	const DrawMetrics &drawMetrics() const { return metrics; }
	//! Composes pages at `samples` in the background, nearest the world tile
	//! (centreX, centreY) first and spiralling outward, at worker priority
	//! behind visible work, so a zoom out or a jump finds its pages ready. Call
	//! every frame from the start of a game; it stops once the map is done or
	//! its pages would take three quarters of the cache allowance (less what the view
	//! holds), and starts again around a new centre after a jump or a pan.
	//! It also keeps the whole-map base current (see drawBase).
	void prewarm(const SceneMap &, Uint32 visibleTeams, bool wholeMap, int centreX, int centreY, int samples);
	//! Admits complete pages at the prewarmed density, nearest the view first,
	//! to a presentation device adapter with what remains of the frame's upload
	//! allowance (DrawableSurface::prepareDeviceTexture), so the zoom out that
	//! shows them draws every page on its first frame. That frame copies them
	//! to the GPU: at most the prewarm allowance, 4 MiB for a 256 by 256 map.
	//! Call after the frame's overview draw. Does nothing on renderers that
	//! upload as they draw.
	void uploadAhead();
	//! Side of the blocks `shownPages` lists (SoftwareTerrainCache::ChunkTiles).
	static constexpr int BackdropTiles = 16;
	//! Cells along a page's side: PageTiles, or the map's shorter side when smaller.
	static constexpr int PageTiles = 64;
	int pageTiles() const { return side; }
	//! Fades a page in over this long once its first cells arrive where the view
	//! showed the base or the other density.
	static constexpr Uint64 FadeMs = 150;
	std::uint64_t composedCells() const { return compositions; }
	//! Pages uploadAhead has uploaded.
	std::uint64_t pagesUploadedAhead() const { return uploadedAhead; }
	//! Whether the whole-map base, one sample per cell composed by a worker as
	//! the map loads, is ready. It draws beneath cells not composed yet, so the
	//! overview never shows a transparent patch.
	bool baseReady() const { return bool(base); }
	//! Samples per cell axis of the last update or draw, or 0 before the first.
	int samples() const { return pageSamples; }
	~OverviewTerrainCache();

  private:
	static constexpr int Tiles = PageTiles; // Stride of a page's cell arrays.
	int side = Tiles;					   // Cells along a page's side for this map.
	struct Page
	{
		int samples = 0;
		Uint32 seed = 0;
		bool valid = false, changed = false;
		std::uint64_t used = 0;
		// What the composed cells were checked against.
		Uint64 terrainRevision = 0, resourceRevision = 0, discoveryRevision = 0;
		Uint32 visibleTeams = 0;
		bool wholeMap = false;
		std::array<Uint32, (Tiles + 3) * (Tiles + 3)> vertices{};
		// Per cell: resource type and square size last composed, -1 for none, or
		// the minimum for a cell not composed yet; and the code being composed.
		std::array<int, Tiles * Tiles> resources{}, inFlight{};
		int uncomposed = 0; // Cells of `resources` still Uncomposed.
		std::unique_ptr<TerrainVisual::Surface> image;
		// Drawn with nothing composed in view; its first cells then fade in from
		// fadeStart (SDL_GetTicks).
		bool shownEmpty = false;
		Uint64 fadeStart = 0;
		// The content revision uploadAhead last uploaded or found held.
		std::uint64_t aheadRevision = ~std::uint64_t(0);
	};
	struct BaseJob;
	struct CellJob;
	struct Batch;
	struct Region
	{
		int x0, y0, x1, y1; // World tiles of the page that the view shows, half-open.
	};
	struct Output
	{
		int x, y, columns, rows, samples;
		Uint32 seed, visibleTeams;
		bool wholeMap;
		Uint64 terrainRevision, resourceRevision, discoveryRevision, surfaceIdentity, surfaceRevision;
		bool operator==(const Output &) const = default;
	};
	std::size_t pageStorage(int samples) const;
	static void composeCell(const TerrainVisual::Catalog &, const CellJob &, SDL_Surface *, int x, int y);
	void resetForMap(const SceneMap &);
	Page &page(const SceneMap &, int wx, int wy, int samples);
	int cellCode(const SceneMap &, int mx, int my, Uint32 visibleTeams, bool wholeMap, int samples, int &type,
				 int &square) const;
	//! Checks a page against the map and queues, up to `jobLimit` in all, its
	//! cells in `region` that need composing, describing cells until `deadline`
	//! (SDL_GetTicksNS). False when the limit or the deadline stopped it.
	bool validate(const SceneMap &, Page &, int wx, int wy, const Region &, Uint32 visibleTeams, bool wholeMap,
				  std::vector<CellJob> &jobs, std::size_t jobLimit, Uint64 deadline = ~Uint64(0));
	void composeNow(std::vector<CellJob> &jobs);
	void submit(std::vector<CellJob> &jobs, bool ahead = false);
	void updateBase(const SceneMap &);
	void drawBase(int wx, int wy, const Region &, int screenX, int screenY, GAGCore::DrawableSurface &, Uint8 alpha);
	void collect();
	void installCell(Page &, const CellJob &, const Uint32 *pixels, int pitch);
	bool covers(const Page &, int wx, int wy, const Region &) const;
	//! Whether the page draws this frame: its current pixels, the previous
	//! revision the device holds, or nothing (a device's upload allowance is spent).
	enum class Admission { None, Previous, Current };
	Admission admit(Page &);
	void drawPage(Page &, int wx, int wy, const Region &, int screenX, int screenY, GAGCore::DrawableSurface &,
				  Uint8 alpha);
	//! Visits the view in blocks of `step` cells (a page, or BackdropTiles),
	//! each with the origin of the page holding it.
	template <class Visit>
	void forEachPage(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy, bool centreFirst,
					 int step, Visit visit);
	void trim(std::size_t keepBytes);
	std::size_t queuedCells(bool ahead) const;
	std::unordered_map<std::uint64_t, Page> pages;
	std::vector<std::shared_ptr<Batch>> batches;
	std::optional<Output> output;
	Uint64 mapIdentity = 0;
	std::shared_ptr<const TerrainRegistry> terrain;
	std::shared_ptr<const ResourceRegistry> resources;
	std::shared_ptr<const TerrainVisual::Catalog> catalog; // Snapshot the workers read.
	// Total yield capacity per resource type of `resources`, built on first use.
	std::vector<unsigned> capacities;
	std::shared_ptr<const MapAssetBundle> assets;
	std::uint64_t use = 0, compositions = 0, uploadedAhead = 0;
	int mapWidth = 1, mapHeight = 1;
	DrawMetrics metrics;
	std::size_t pageBytes = 0;
	int pageSamples = 0;
	// The prewarm spiral: its centre page, ring and step within the ring, and
	// pages visited; done once it has covered the map or its allowance.
	int prewarmX = 0, prewarmY = 0, prewarmSamples = 0, prewarmRing = 0, prewarmStep = 0;
	int viewX = 0, viewY = 0; // World tile at the centre of the last interactive draw or prewarm.
	std::size_t prewarmVisited = 0;
	bool prewarmDone = false;
	std::size_t viewBytes = 0; // Page storage the last interactive draw kept.
	// The whole-map base and the worker composing its next version.
	std::unique_ptr<TerrainVisual::Surface> base;
	int baseReduction = 1;
	Uint64 baseTerrainRevision = 0;
	Uint32 baseSeed = 0;
	std::shared_ptr<BaseJob> baseJob;
};
