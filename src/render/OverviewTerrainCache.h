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
	void draw(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy,
			  Uint32 visibleTeams, bool wholeMap, int samples, GAGCore::DrawableSurface &target,
			  Uint8 alpha, bool complete);
	//! Composes the whole map at few samples in the background, within the same
	//! per-frame budget, so a first zoom out finds every page ready. Call every
	//! frame from the start of a game; it stops once the map is done, and skips
	//! maps whose pages would take more than half the cache allowance.
	void prewarm(const SceneMap &, Uint32 visibleTeams, bool wholeMap);
	std::uint64_t composedCells() const { return compositions; }
	//! Samples per cell axis of the last update or draw, or 0 before the first.
	int samples() const { return pageSamples; }
	~OverviewTerrainCache();

  private:
	static constexpr int Tiles = 16;
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
		std::unique_ptr<TerrainVisual::Surface> image;
	};
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
	static std::size_t pageStorage(int samples);
	static void composeCell(const TerrainVisual::Catalog &, const CellJob &, SDL_Surface *, int x, int y);
	void resetForMap(const SceneMap &);
	Page &page(const SceneMap &, int wx, int wy, int samples);
	int cellCode(const SceneMap &, int mx, int my, Uint32 visibleTeams, bool wholeMap, int samples, int &type,
				 int &square) const;
	//! Checks a page against the map and queues, up to `jobLimit` in all, its
	//! cells in `region` that need composing. False when the limit stopped it.
	bool validate(const SceneMap &, Page &, int wx, int wy, const Region &, Uint32 visibleTeams, bool wholeMap,
				  std::vector<CellJob> &jobs, std::size_t jobLimit);
	void composeNow(std::vector<CellJob> &jobs);
	void submit(std::vector<CellJob> &jobs);
	void collect();
	void installCell(Page &, const CellJob &, const Uint32 *pixels, int pitch);
	bool covers(const Page &, int wx, int wy, const Region &) const;
	void drawPage(Page &, int wx, int wy, const Region &, int screenX, int screenY, GAGCore::DrawableSurface &,
				  Uint8 alpha);
	template <class Visit>
	void forEachPage(const SceneMap &, int left, int top, int right, int bottom, int vx, int vy, bool centreFirst,
					 Visit visit);
	void trim(std::size_t keepBytes);
	std::size_t queuedCells() const;
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
	std::uint64_t use = 0, compositions = 0;
	std::size_t pageBytes = 0;
	int pageSamples = 0;
	std::size_t prewarmCursor = 0; // Next page of the map to prewarm.
	bool prewarmDone = false;
};
