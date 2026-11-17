// SPDX-License-Identifier: GPL-3.0-or-later
#include "OverviewTerrainCache.h"
#include "BackgroundWork.h"
#include "TerrainBuiltins.h"
#include "GlobalContainer.h"
#include "GraphicContext.h"
#include "scene/SceneMap.h"
#include "terrain/TerrainCompositor.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace
{
constexpr int Uncomposed = std::numeric_limits<int>::min();
// Page storage kept across both densities. A view always keeps its own pages.
constexpr std::size_t Budget = 48u * 1024u * 1024u;
// Graphics-thread time per frame for checking pages and describing cells.
constexpr auto FrameBudget = std::chrono::microseconds(1500);
// Cells per worker task, and composed cells copied into pages per frame.
constexpr std::size_t CellsPerBatch = 256, InstallsPerFrame = 8192;

std::uint64_t pageKey(int samples, int wx, int wy)
{
	return std::uint64_t(samples) << 48 | std::uint64_t(unsigned(wy)) << 24 | unsigned(wx);
}
}

//! One cell to compose: everything a worker needs, copied from the scene.
struct OverviewTerrainCache::CellJob
{
	std::uint64_t key = 0;
	int index = 0, samples = 0, code = 0, square = 0;
	TerrainVisual::Recipe recipe;
	std::array<std::array<unsigned char, 3>, 4> colors{};
	unsigned customCorners = 0; // Bits of corners using `colors`.
	Uint32 resourceColor = 0;
};

//! Cells composed by a worker into storage of their own; the graphics thread
//! copies them into their pages once the batch is done.
struct OverviewTerrainCache::Batch
{
	std::vector<CellJob> jobs;
	std::vector<Uint32> pixels;
	std::shared_ptr<const TerrainVisual::Catalog> catalog;
	std::atomic<bool> done{false}, cancelled{false};
	// Ahead of the view (prewarm): queued behind visible work until a view
	// wants one of its pages, which queues it again as visible work; whichever
	// task claims it first composes it.
	bool ahead = false, promoted = false;
	std::vector<std::uint64_t> keys; // Pages it composes cells of.
	std::atomic<bool> claimed{false};
};

//! The whole-map base: one colour per cell, the mean of its corners' overview
//! colours, from the immutable vertex storage of one snapshot.
struct OverviewTerrainCache::BaseJob
{
	std::shared_ptr<const std::vector<TerrainType>> vertices;
	std::vector<std::array<unsigned char, 3>> colors; // By terrain type.
	int w = 0, h = 0, reduction = 1;
	Uint64 terrainRevision = 0;
	Uint32 seed = 0;
	std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels{nullptr, SDL_DestroySurface};
	std::atomic<bool> done{false}, cancelled{false};
};

int OverviewTerrainCache::samplesFor(double pixelsPerCell, int current, int cells,
									int maximumTexture)
{
	// Many samples show five distinct stock sizes; below about six device
	// pixels per cell they would only quadruple the image for no visible gain.
	if (maximumTexture > 0 && std::int64_t(cells) * ManySamples > maximumTexture)
		return FewSamples;
	return pixelsPerCell >= (current == ManySamples ? 5.0 : 6.0) ? ManySamples : FewSamples;
}

int OverviewTerrainCache::resourceSquare(unsigned stock, unsigned capacity, int samples)
{
	const int largest = samples / 2;
	if (!capacity)
		return largest;
	return 1 + int((largest - 1) * std::min(stock, capacity) / capacity);
}

OverviewTerrainCache::~OverviewTerrainCache()
{
	for (auto &batch : batches)
		batch->cancelled.store(true, std::memory_order_relaxed);
	if (baseJob)
		baseJob->cancelled.store(true, std::memory_order_relaxed);
}

void OverviewTerrainCache::resetForMap(const SceneMap &map)
{
	if (mapIdentity == map.identity() && terrain == map.frozenTerrainRegistry() &&
		resources == map.frozenResourceRegistry() && assets == map.frozenAssetBundle() && catalog)
		return;
	for (auto &batch : batches)
		batch->cancelled.store(true, std::memory_order_relaxed);
	batches.clear();
	pages.clear();
	pageBytes = 0;
	prewarmSamples = 0;
	prewarmDone = false;
	viewBytes = 0;
	if (baseJob)
		baseJob->cancelled.store(true, std::memory_order_relaxed);
	baseJob.reset();
	base.reset();
	capacities.clear();
	output.reset();
	mapIdentity = map.identity();
	mapWidth = std::max(1, map.getW());
	mapHeight = std::max(1, map.getH());
	side = std::min({Tiles, mapWidth, mapHeight});
	terrain = map.frozenTerrainRegistry();
	resources = map.frozenResourceRegistry();
	assets = map.frozenAssetBundle();
	// The catalog snapshot alone: overview cells read no material textures.
	catalog = globalContainer->terrainCompositor(assets).cpuMaterialsForRecipes({})->definitions;
	const auto &registry = map.resourceRegistry();
	capacities.resize(registry.size());
	for (unsigned id = 0; id < registry.size(); ++id)
		for (const auto &yield : registry.yields(static_cast<ResourceId>(id)))
			capacities[id] += yield.capacity;
}

OverviewTerrainCache::Page &OverviewTerrainCache::page(const SceneMap &map, int wx, int wy, int samples)
{
	const auto key = pageKey(samples, wx, wy);
	auto found = pages.find(key);
	if (found == pages.end())
	{
		const int pixels = side * samples;
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
			SDL_CreateSurface(pixels, pixels, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		if (!surface)
			throw std::bad_alloc();
		// Cells not composed yet stay transparent.
		std::memset(surface->pixels, 0, std::size_t(surface->pitch) * pixels);
		Page fresh;
		fresh.samples = samples;
		fresh.uncomposed = side * side;
		fresh.resources.fill(Uncomposed);
		fresh.inFlight.fill(Uncomposed);
		fresh.image = std::make_unique<TerrainVisual::Surface>(surface.get(), false);
		surface.release();
		found = pages.emplace(key, std::move(fresh)).first;
		pageBytes += pageStorage(samples);
	}
	found->second.used = ++use;
	return found->second;
}

std::size_t OverviewTerrainCache::pageStorage(int samples) const
{
	// CPU pixels plus the texture a drawn page uploads.
	return sizeof(Page) + std::size_t(side * samples) * (side * samples) * 4 * 2;
}

int OverviewTerrainCache::cellCode(const SceneMap &map, int mx, int my, Uint32 visibleTeams, bool wholeMap,
								   int samples, int &type, int &square) const
{
	const auto &resource = map.getResource(mx, my);
	type = resource.type != NO_RES_TYPE &&
				   (wholeMap || map.isMapPartiallyDiscovered(mx - 1, my - 1, mx + 1, my + 1, visibleTeams))
			   ? resource.type
			   : NO_RES_TYPE;
	square = 0;
	if (type != NO_RES_TYPE)
		square = resourceSquare(resource.amount, unsigned(type) < capacities.size() ? capacities[type] : 0, samples);
	// The type and square size identify everything composed for the cell.
	return type == NO_RES_TYPE ? -1 : type << 4 | square;
}

bool OverviewTerrainCache::validate(const SceneMap &map, Page &page, int wx, int wy, const Region &region,
									Uint32 visibleTeams, bool wholeMap, std::vector<CellJob> &jobs,
									std::size_t jobLimit, Uint64 deadline)
{
	const int samples = page.samples;
	const bool fresh = !page.valid || page.seed != map.terrainSeed();
	if (fresh || page.terrainRevision != map.terrainRevision())
	{
		std::array<Uint32, (Tiles + 3) * (Tiles + 3)> vertices{};
		for (int y = -1; y <= side + 1; ++y)
			for (int x = -1; x <= side + 1; ++x)
				vertices[(y + 1) * (Tiles + 3) + x + 1] = map.vertexTerrainAt(wx + x, wy + y);
		if (fresh || vertices != page.vertices)
		{
			page.resources.fill(Uncomposed);
			page.inFlight.fill(Uncomposed);
			page.uncomposed = side * side;
			page.vertices = vertices;
		}
		page.seed = map.terrainSeed();
		page.terrainRevision = map.terrainRevision();
		page.valid = true;
	}
	const Uint64 discovery = wholeMap ? 0 : map.discoveryRevision();
	const Uint32 teams = wholeMap ? 0 : visibleTeams;
	const bool stamped = page.resourceRevision == map.resourcePresentationRevision() &&
		page.discoveryRevision == discovery && page.visibleTeams == teams && page.wholeMap == wholeMap;
	if (!stamped)
	{
		// Composed cells outside the view must not outlive the new stamp: those
		// whose resource changed compose again once they are shown.
		for (int y = 0; y < side; ++y)
			for (int x = 0; x < side; ++x)
			{
				const int i = y * Tiles + x;
				if (page.resources[i] == Uncomposed)
					continue;
				if (wx + x >= region.x0 && wx + x < region.x1 && wy + y >= region.y0 && wy + y < region.y1)
					continue;
				int type, square;
				if (cellCode(map, wx + x, wy + y, visibleTeams, wholeMap, samples, type, square) != page.resources[i])
				{
					page.resources[i] = Uncomposed;
					++page.uncomposed;
				}
			}
		page.resourceRevision = map.resourcePresentationRevision();
		page.discoveryRevision = discovery;
		page.visibleTeams = teams;
		page.wholeMap = wholeMap;
	}
	// A page with every cell composed against the current stamp has nothing to queue.
	if (stamped && !page.uncomposed)
		return true;
	const auto key = pageKey(samples, wx, wy);
	auto &compositor = globalContainer->terrainCompositor(assets);
	for (int my = region.y0; my < region.y1; ++my)
		for (int mx = region.x0; mx < region.x1; ++mx)
		{
			const int i = (my - wy) * Tiles + (mx - wx);
			if (stamped && page.resources[i] != Uncomposed)
				continue;
			int type, square;
			const int code = cellCode(map, mx, my, visibleTeams, wholeMap, samples, type, square);
			// A complete draw (no job limit) composes cells still in flight itself:
			// it has no later frame to install them on.
			const bool complete = jobLimit == std::numeric_limits<std::size_t>::max();
			if (page.resources[i] == code || (!complete && page.inFlight[i] == code))
				continue;
			// A page holds 4096 cells: past the deadline the rest wait for a
			// later frame (checked every 64 described cells).
			if (jobs.size() >= jobLimit ||
				(jobs.size() % 64 == 63 && SDL_GetTicksNS() > deadline))
			{
				// Cells after this one were not checked against the new stamp.
				if (!stamped)
					page.resourceRevision = ~Uint64(0);
				return false;
			}
			CellJob job;
			job.key = key;
			job.index = i;
			job.samples = samples;
			job.code = code;
			job.square = square;
			job.recipe = compositor.describeOverview(map, mx, my);
			const auto corners = map.cellCorners(mx, my);
			for (unsigned k = 0; k < corners.size(); ++k)
				if (unsigned(corners[k]) >= TERRAIN_COUNT)
				{
					const auto color = map.terrainPresentation(corners[k]).overview;
					job.colors[k] = {color.r, color.g, color.b};
					job.customCorners |= 1u << k;
				}
			if (type != NO_RES_TYPE)
			{
				const auto &color = map.resourceRegistry().presentation(static_cast<ResourceId>(type)).minimap;
				job.resourceColor = 0xFF000000u | Uint32(color[0]) << 16 | Uint32(color[1]) << 8 | color[2];
			}
			page.inFlight[i] = code;
			jobs.push_back(std::move(job));
		}
	return true;
}

void OverviewTerrainCache::composeCell(const TerrainVisual::Catalog &definitions, const CellJob &job,
									   SDL_Surface *surface, int x, int y)
{
	TerrainVisual::Compositor::CornerColors custom{};
	for (unsigned k = 0; k < 4; ++k)
		if (job.customCorners & (1u << k))
			custom[k] = &job.colors[k];
	TerrainVisual::Compositor::composeOverview(definitions, job.recipe, surface, x, y, job.samples,
											   job.customCorners ? &custom : nullptr);
	if (job.code >= 0)
	{
		// Centred, with any odd sample left over below and to the right.
		const int inset = (job.samples - job.square) / 2;
		for (int row = 0; row < job.square; ++row)
			std::fill_n(reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) +
												   (y + inset + row) * surface->pitch) + x + inset,
						job.square, job.resourceColor);
	}
}

void OverviewTerrainCache::installCell(Page &page, const CellJob &job, const Uint32 *pixels, int pitch)
{
	auto *surface = page.image->getSDLSurface();
	const int x = job.index % Tiles * job.samples, y = job.index / Tiles * job.samples;
	for (int row = 0; row < job.samples; ++row)
		std::memcpy(static_cast<Uint8 *>(surface->pixels) + (y + row) * surface->pitch + x * 4,
					reinterpret_cast<const Uint8 *>(pixels) + row * pitch, std::size_t(job.samples) * 4);
	page.uncomposed -= page.resources[job.index] == Uncomposed;
	page.resources[job.index] = job.code;
	if (page.inFlight[job.index] == job.code)
		page.inFlight[job.index] = Uncomposed;
	page.changed = true;
	++compositions;
}

void OverviewTerrainCache::composeNow(std::vector<CellJob> &jobs)
{
	for (const auto &job : jobs)
	{
		auto found = pages.find(job.key);
		if (found == pages.end())
			continue;
		auto &page = found->second;
		const int x = job.index % Tiles * job.samples, y = job.index / Tiles * job.samples;
		composeCell(*catalog, job, page.image->getSDLSurface(), x, y);
		page.uncomposed -= page.resources[job.index] == Uncomposed;
		page.resources[job.index] = job.code;
		page.inFlight[job.index] = Uncomposed;
		page.changed = true;
		++compositions;
	}
	jobs.clear();
}

namespace
{
template <class Batch, class Compose> void runBatch(const std::shared_ptr<Batch> &batch, Compose composeCell)
{
	if (batch->claimed.exchange(true, std::memory_order_acq_rel))
		return;
	if (!batch->cancelled.load(std::memory_order_relaxed))
	{
		std::size_t bytes = 0;
		for (const auto &job : batch->jobs)
			bytes += std::size_t(job.samples) * job.samples;
		batch->pixels.resize(bytes);
		std::size_t offset = 0;
		for (const auto &job : batch->jobs)
		{
			SDL_Surface cell{};
			cell.format = SDL_PIXELFORMAT_ARGB8888;
			cell.w = cell.h = job.samples;
			cell.pitch = job.samples * 4;
			cell.pixels = batch->pixels.data() + offset;
			composeCell(*batch->catalog, job, &cell, 0, 0);
			offset += std::size_t(job.samples) * job.samples;
		}
	}
	batch->done.store(true, std::memory_order_release);
}
}

void OverviewTerrainCache::submit(std::vector<CellJob> &jobs, bool ahead)
{
	for (std::size_t first = 0; first < jobs.size(); first += CellsPerBatch)
	{
		auto batch = std::make_shared<Batch>();
		const auto last = std::min(jobs.size(), first + CellsPerBatch);
		batch->jobs.assign(std::make_move_iterator(jobs.begin() + first), std::make_move_iterator(jobs.begin() + last));
		batch->catalog = catalog;
		batch->ahead = ahead;
		for (const auto &job : batch->jobs)
			if (std::find(batch->keys.begin(), batch->keys.end(), job.key) == batch->keys.end())
				batch->keys.push_back(job.key);
		batches.push_back(batch);
		BackgroundWork::submit([batch] { runBatch(batch, &OverviewTerrainCache::composeCell); },
							   ahead ? BackgroundWork::Priority::Ahead : BackgroundWork::Priority::Visible);
	}
	jobs.clear();
}

void OverviewTerrainCache::collect()
{
	std::size_t installed = 0;
	std::erase_if(batches, [&](const std::shared_ptr<Batch> &batch)
	{
		if (installed >= InstallsPerFrame || !batch->done.load(std::memory_order_acquire))
			return false;
		std::size_t offset = 0;
		for (const auto &job : batch->jobs)
		{
			const auto *pixels = batch->pixels.data() + offset;
			offset += std::size_t(job.samples) * job.samples;
			auto found = pages.find(job.key);
			// A page edited or replaced since the cell was described queues it again.
			if (found == pages.end() || found->second.inFlight[job.index] != job.code)
				continue;
			installCell(found->second, job, pixels, job.samples * 4);
			++installed;
		}
		return true;
	});
}

void OverviewTerrainCache::trim(std::size_t keepBytes)
{
	const std::size_t limit = std::max(Budget, keepBytes);
	if (pageBytes <= limit)
		return;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> ages;
	ages.reserve(pages.size());
	for (const auto &[key, page] : pages)
		ages.emplace_back(page.used, key);
	std::sort(ages.begin(), ages.end());
	for (const auto &[used, key] : ages)
	{
		if (pageBytes <= limit)
			break;
		auto found = pages.find(key);
		pageBytes -= pageStorage(found->second.samples);
		pages.erase(found);
	}
}

template <class Visit>
void OverviewTerrainCache::forEachPage(const SceneMap &map, int left, int top, int right, int bottom, int vx,
									   int vy, bool centreFirst, int step, Visit visit)
{
	// Blocks of `step` cells, each within one page of `side` (a multiple of it).
	const auto chunkOf = [](int tile, int size) { return int(std::floor(double(tile) / size)); };
	const int cy0 = chunkOf(top + vy, step), cy1 = chunkOf(bottom + vy, step);
	const int cx0 = chunkOf(left + vx, step), cx1 = chunkOf(right + vx, step);
	const int columns = cx1 - cx0 + 1;
	std::vector<int> order(std::size_t(columns) * (cy1 - cy0 + 1));
	for (std::size_t i = 0; i < order.size(); ++i)
		order[i] = int(i);
	if (centreFirst)
	{
		const double centreX = (left + right + 1) * .5 + vx, centreY = (top + bottom + 1) * .5 + vy;
		const auto distance = [&](int i)
		{
			const double dx = (cx0 + i % columns) * step + step * .5 - centreX;
			const double dy = (cy0 + i / columns) * step + step * .5 - centreY;
			return dx * dx + dy * dy;
		};
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return distance(a) < distance(b); });
	}
	for (const int i : order)
	{
		const int cx = cx0 + i % columns, cy = cy0 + i / columns;
		// The page holding the block, unwrapped, and its wrapped origin.
		const int px = chunkOf(cx * step, side) * side, py = chunkOf(cy * step, side) * side;
		const int wx = px & map.getMaskW(), wy = py & map.getMaskH();
		// The visible part of the block, in unwrapped and in page-wrapped tiles.
		const int x0 = std::max(left + vx, cx * step), x1 = std::min(right + vx + 1, (cx + 1) * step);
		const int y0 = std::max(top + vy, cy * step), y1 = std::min(bottom + vy + 1, (cy + 1) * step);
		const Region region{wx + x0 - px, wy + y0 - py, wx + x1 - px, wy + y1 - py};
		visit(wx, wy, region, x0 - vx, y0 - vy);
	}
}

void OverviewTerrainCache::update(const SceneMap &map, int left, int top, int right, int bottom,
								  int vx, int vy, Uint32 visibleTeams, bool wholeMap, int samples,
								  GAGCore::DrawableSurface &target)
{
	if (samples != FewSamples && samples != ManySamples)
		throw std::invalid_argument("Overview samples must be 4 or 8 per cell");
	pageSamples = samples;
	Output next{
		.x = (left + vx) & map.getMaskW(),
		.y = (top + vy) & map.getMaskH(),
		.columns = right - left + 1,
		.rows = bottom - top + 1,
		.samples = samples,
		.seed = map.terrainSeed(),
		.visibleTeams = wholeMap ? 0u : visibleTeams,
		.wholeMap = wholeMap,
		.terrainRevision = map.terrainRevision(),
		.resourceRevision = map.resourcePresentationRevision(),
		.discoveryRevision = wholeMap ? 0 : map.discoveryRevision(),
		.surfaceIdentity = target.lifetimeIdentity(),
		.surfaceRevision = target.contentRevision(),
	};
	if (output == next && mapIdentity == map.identity() && terrain == map.frozenTerrainRegistry() &&
		resources == map.frozenResourceRegistry() && assets == map.frozenAssetBundle())
		return;
	resetForMap(map);
	output.reset();
	collect();
	std::size_t keptBytes = 0;
	std::vector<CellJob> jobs;
	auto *pixels = target.getSDLSurface();
	forEachPage(map, left, top, right, bottom, vx, vy, false, side,
				[&](int wx, int wy, const Region &region, int screenX, int screenY)
	{
		auto &page = this->page(map, wx, wy, samples);
		keptBytes += pageStorage(samples);
		validate(map, page, wx, wy, region, visibleTeams, wholeMap, jobs, std::numeric_limits<std::size_t>::max());
		composeNow(jobs);
		const auto *source = page.image->getSDLSurface();
		for (int y = 0; y < (region.y1 - region.y0) * samples; ++y)
			std::memcpy(static_cast<Uint8 *>(pixels->pixels) + ((screenY - top) * samples + y) * pixels->pitch +
							(screenX - left) * samples * 4,
						static_cast<const Uint8 *>(source->pixels) + ((region.y0 - wy) * samples + y) * source->pitch +
							(region.x0 - wx) * samples * 4,
						std::size_t(region.x1 - region.x0) * samples * 4);
	});
	trim(keptBytes);
	target.markPixelsChanged();
	next.surfaceRevision = target.contentRevision();
	output = next;
}

void OverviewTerrainCache::draw(const SceneMap &map, int left, int top, int right, int bottom, int vx, int vy,
								Uint32 visibleTeams, bool wholeMap, int samples, GAGCore::DrawableSurface &target,
								Uint8 alpha, bool complete, const std::vector<SDL_Point> *shownPages)
{
	metrics = {};
	if (samples != FewSamples && samples != ManySamples)
		throw std::invalid_argument("Overview samples must be 4 or 8 per cell");
	pageSamples = samples;
	resetForMap(map);
	collect();
	if (!complete)
		updateBase(map);
	// Graphics-thread time spent checking pages and describing cells; drawing
	// the pages already composed does not count against it.
	std::chrono::steady_clock::duration describing{};
	const Uint64 now = SDL_GetTicks();
	const int other = samples == FewSamples ? ManySamples : FewSamples;
	// Cells described per frame for the workers; visible batches still running count too.
	std::size_t queued = queuedCells(false);
	const std::size_t queueLimit = std::size_t(std::max(1u, BackgroundWork::workers())) * CellsPerBatch * 4;
	// A backdrop draws, and composes, only the blocks it shows through.
	std::unordered_set<std::uint64_t> shownKeys;
	if (shownPages)
		for (const auto &origin : *shownPages)
			shownKeys.insert(pageKey(0, origin.x, origin.y));
	const int step = shownPages ? std::min(int(BackdropTiles), side) : side;
	if (!complete && !shownPages)
	{
		viewX = ((left + right) / 2 + vx) & map.getMaskW();
		viewY = ((top + bottom) / 2 + vy) & map.getMaskH();
	}
	std::size_t keptBytes = 0;
	std::unordered_set<std::uint64_t> kept;
	std::vector<std::uint64_t> wanted; // Visible pages still waiting for cells.
	std::vector<CellJob> jobs;
	forEachPage(map, left, top, right, bottom, vx, vy, !complete, step,
				[&](int wx, int wy, const Region &region, int screenX, int screenY)
	{
		if (shownPages && !shownKeys.count(pageKey(0, wx + (region.x0 - wx) / step * step,
													 wy + (region.y0 - wy) / step * step)))
			return;
		if (kept.insert(pageKey(samples, wx, wy)).second)
			keptBytes += pageStorage(samples);
		auto *page = &this->page(map, wx, wy, samples);
		bool shown = true;
		if (complete)
		{
			validate(map, *page, wx, wy, region, visibleTeams, wholeMap, jobs, std::numeric_limits<std::size_t>::max());
			composeNow(jobs);
		}
		else if (describing < FrameBudget && queued + jobs.size() < queueLimit)
		{
			const auto started = std::chrono::steady_clock::now();
			const std::size_t limit = queueLimit - queued;
			const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(FrameBudget - describing);
			if (!validate(map, *page, wx, wy, region, visibleTeams, wholeMap, jobs, limit,
						  SDL_GetTicksNS() + Uint64(std::max<std::int64_t>(0, remaining.count()))))
				shown = false;
			if (jobs.size() >= CellsPerBatch)
			{
				queued += jobs.size();
				submit(jobs);
			}
			describing += std::chrono::steady_clock::now() - started;
		}
		const std::uint64_t cells = std::uint64_t(region.x1 - region.x0) * (region.y1 - region.y0);
		metrics.cells += cells;
		if (complete)
		{
			drawPage(*page, wx, wy, region, screenX, screenY, target, alpha);
			return;
		}
		// Cells of the page this view shows that are composed, and those the
		// other density or only the base can show instead.
		std::uint64_t composed = 0, fromOther = 0;
		auto found = pages.find(pageKey(other, wx, wy));
		const Page *otherPage = found != pages.end() && found->second.valid && found->second.seed == map.terrainSeed()
			? &found->second : nullptr;
		if (!page->uncomposed)
			composed = cells;
		else
			for (int my = region.y0; my < region.y1; ++my)
				for (int mx = region.x0; mx < region.x1; ++mx)
				{
					const int i = (my - wy) * Tiles + (mx - wx);
					if (page->resources[i] != Uncomposed)
						++composed;
					else if (otherPage && otherPage->resources[i] != Uncomposed)
						++fromOther;
				}
		shown = composed == cells;
		if (!shown)
			wanted.push_back(pageKey(samples, wx, wy));
		// A page drawn empty fades its first cells in; one composed before the
		// view reached it (prewarmed) shows at once. Emptiness is the whole
		// page's, so blocks of one page agree.
		if (page->uncomposed == side * side)
			page->shownEmpty = true;
		else if (page->shownEmpty)
		{
			page->shownEmpty = false;
			page->fadeStart = now;
		}
		// A page the device cannot upload this frame shows what lies beneath
		// (or its previous upload), and fades in once it uploads.
		const auto admitted = admit(*page);
		const bool drawable = admitted != Admission::None;
		if (!drawable && composed)
		{
			page->shownEmpty = true;
			page->fadeStart = 0;
		}
		const bool otherDrawable = otherPage && admit(found->second) != Admission::None;
		if (!otherDrawable)
			fromOther = 0;
		if (!drawable)
		{
			shown = false;
			composed = 0;
		}
		// Its previous upload may lack cells composed since: the base beneath covers them.
		if (admitted == Admission::Previous)
			shown = false;
		const bool fading = page->fadeStart && now - page->fadeStart < FadeMs;
		if (!fading)
			page->fadeStart = 0;
		if (!shown || fading)
		{
			// Beneath: the base wherever the other density cannot stand in, then the other density.
			if (base && (!otherDrawable || fromOther + composed < cells || fading))
				drawBase(wx, wy, region, screenX, screenY, target, alpha);
			if (otherDrawable)
				drawPage(found->second, wx, wy, region, screenX, screenY, target, alpha);
		}
		const std::uint64_t missing = cells - composed - fromOther;
		metrics.other += fromOther;
		(base ? metrics.base : metrics.holes) += missing;
		if (fading)
			metrics.fading += composed;
		if (drawable)
			drawPage(*page, wx, wy, region, screenX, screenY, target,
					 fading ? Uint8(alpha * (now - page->fadeStart) / FadeMs) : alpha);
	});
	if (!jobs.empty())
		submit(jobs);
	// Prewarmed batches for pages the view now waits on run as visible work.
	if (!wanted.empty())
		for (auto &batch : batches)
			if (batch->ahead && !batch->promoted && !batch->claimed.load(std::memory_order_relaxed) &&
				std::any_of(batch->keys.begin(), batch->keys.end(),
							[&](std::uint64_t key) { return std::find(wanted.begin(), wanted.end(), key) != wanted.end(); }))
			{
				batch->promoted = true;
				BackgroundWork::submit([batch] { runBatch(batch, &OverviewTerrainCache::composeCell); });
			}
	if (!complete && !shownPages)
		viewBytes = keptBytes;
	trim(keptBytes * 2);
}

bool OverviewTerrainCache::covers(const Page &page, int wx, int wy, const Region &region) const
{
	for (int my = region.y0; my < region.y1; ++my)
		for (int mx = region.x0; mx < region.x1; ++mx)
			if (page.resources[(my - wy) * Tiles + (mx - wx)] == Uncomposed)
				return false;
	return true;
}

OverviewTerrainCache::Admission OverviewTerrainCache::admit(Page &page)
{
	// New pixels upload within the device's per-frame allowance; past it a
	// page whose previous revision the device holds keeps drawing that.
	const bool held = page.image->deviceHoldsPixels();
	if ((page.changed || !held) && !page.image->takeDeviceUpload())
		return held ? Admission::Previous : Admission::None;
	if (page.changed)
	{
		page.image->markPixelsChanged();
		page.changed = false;
	}
	return Admission::Current;
}

void OverviewTerrainCache::drawPage(Page &page, int wx, int wy, const Region &region, int screenX, int screenY,
									GAGCore::DrawableSurface &target, Uint8 alpha)
{
	if (page.changed)
	{
		page.image->markPixelsChanged();
		page.changed = false;
	}
	const int samples = page.samples;
	target.drawSurface(screenX * 32, screenY * 32, (region.x1 - region.x0) * 32, (region.y1 - region.y0) * 32,
					   page.image.get(), (region.x0 - wx) * samples, (region.y0 - wy) * samples,
					   (region.x1 - region.x0) * samples, (region.y1 - region.y0) * samples, alpha);
}

void OverviewTerrainCache::updateBase(const SceneMap &map)
{
	if (baseJob && baseJob->done.load(std::memory_order_acquire))
	{
		if (baseJob->pixels)
		{
			// Sampled linearly: one texel per cell blends smoothly into its neighbours.
			base = std::make_unique<TerrainVisual::Surface>(baseJob->pixels.get(), true);
			baseJob->pixels.release();
			baseReduction = baseJob->reduction;
			baseTerrainRevision = baseJob->terrainRevision;
			baseSeed = baseJob->seed;
		}
		baseJob.reset();
	}
	if (baseJob || (base && baseTerrainRevision == map.terrainRevision() && baseSeed == map.terrainSeed()))
		return;
	auto vertices = map.vertexStorage();
	if (!vertices || vertices->size() < std::size_t(map.getW()) * map.getH())
		return;
	auto job = std::make_shared<BaseJob>();
	job->vertices = std::move(vertices);
	job->w = map.getW();
	job->h = map.getH();
	job->terrainRevision = map.terrainRevision();
	job->seed = map.terrainSeed();
	const int limit = globalContainer->gfx ? globalContainer->gfx->maximumTextureSize() : 0;
	while (limit > 0 && (job->w / job->reduction > limit || job->h / job->reduction > limit))
		job->reduction *= 2;
	// One colour per terrain type: a few dozen lookups here, the map on the worker.
	const auto &compositor = globalContainer->terrainCompositor(assets);
	job->colors.resize(map.terrainRegistry().size());
	for (std::size_t type = 0; type < job->colors.size(); ++type)
		job->colors[type] = compositor.overviewColor(map, TerrainType(type));
	baseJob = job;
	BackgroundWork::submit([job]
	{
		if (job->cancelled.load(std::memory_order_relaxed))
		{
			job->done.store(true, std::memory_order_release);
			return;
		}
		const int r = job->reduction, w = job->w / r, h = job->h / r;
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(
			SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		if (pixels)
		{
			const auto &vertices = *job->vertices;
			const auto color = [&](int x, int y) -> const std::array<unsigned char, 3> &
			{
				static constexpr std::array<unsigned char, 3> none{};
				const auto type = std::size_t(vertices[std::size_t(y & (job->h - 1)) * job->w + (x & (job->w - 1))]);
				return type < job->colors.size() ? job->colors[type] : none;
			};
			for (int by = 0; by < h; ++by)
			{
				auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels->pixels) + by * pixels->pitch);
				for (int bx = 0; bx < w; ++bx)
				{
					// The mean of the corners of the texel's cells.
					unsigned sum[3] = {};
					unsigned count = 0;
					for (int y = by * r; y <= by * r + r; y += r)
						for (int x = bx * r; x <= bx * r + r; x += r)
						{
							const auto &c = color(x, y);
							for (int k = 0; k < 3; ++k)
								sum[k] += c[k];
							++count;
						}
					row[bx] = 0xFF000000u | (sum[0] + count / 2) / count << 16 | (sum[1] + count / 2) / count << 8 |
						(sum[2] + count / 2) / count;
				}
			}
			job->pixels = std::move(pixels);
		}
		job->done.store(true, std::memory_order_release);
	}, BackgroundWork::Priority::First);
}

void OverviewTerrainCache::drawBase(int wx, int wy, const Region &region, int screenX, int screenY,
									GAGCore::DrawableSurface &target, Uint8 alpha)
{
	(void)wx;
	(void)wy;
	const int r = baseReduction;
	target.drawSurface(screenX * 32, screenY * 32, (region.x1 - region.x0) * 32, (region.y1 - region.y0) * 32,
					   base.get(), region.x0 / r, region.y0 / r, std::max(1, (region.x1 - region.x0) / r),
					   std::max(1, (region.y1 - region.y0) / r), alpha);
}

std::size_t OverviewTerrainCache::queuedCells(bool ahead) const
{
	std::size_t queued = 0;
	for (const auto &batch : batches)
		if (batch->ahead == ahead)
			queued += batch->jobs.size();
	return queued;
}

void OverviewTerrainCache::prewarm(const SceneMap &map, Uint32 visibleTeams, bool wholeMap, int centreX, int centreY,
								   int samples)
{
	resetForMap(map);
	updateBase(map);
	if (samples != FewSamples && samples != ManySamples)
		throw std::invalid_argument("Overview samples must be 4 or 8 per cell");
	viewX = centreX & map.getMaskW();
	viewY = centreY & map.getMaskH();
	const int columns = std::max(1, map.getW() / side), rows = std::max(1, map.getH() / side);
	const int cx = int(unsigned(centreX & map.getMaskW()) / side), cy = int(unsigned(centreY & map.getMaskH()) / side);
	// Spiral again around a centre that moved two pages or more (a jump or a
	// pan), or for another density; pages composed already cost only a check.
	const auto apart = [](int a, int b, int period)
	{
		const int d = std::abs(a - b) % period;
		return std::min(d, period - d);
	};
	if (samples != prewarmSamples || apart(cx, prewarmX, columns) >= 2 || apart(cy, prewarmY, rows) >= 2)
	{
		prewarmSamples = samples;
		prewarmX = cx;
		prewarmY = cy;
		prewarmRing = prewarmStep = 0;
		prewarmVisited = 0;
		prewarmDone = false;
	}
	// Finished cells install even once the spiral is done.
	collect();
	if (prewarmDone)
		return;
	// The pages three quarters of the allowance hold beside what the view
	// keeps (a 256 by 256 map whole at many samples), and the rings
	// that reach every page of the map.
	const std::size_t allowance = std::min(Budget / 4 * 3, Budget - std::min(Budget, viewBytes));
	const std::size_t limit = std::min(std::size_t(columns) * rows, allowance / pageStorage(samples));
	const int lastRing = std::max(columns, rows) / 2;
	const auto started = std::chrono::steady_clock::now();
	std::size_t queued = queuedCells(true);
	const std::size_t queueLimit = std::size_t(std::max(1u, BackgroundWork::workers())) * CellsPerBatch * 4;
	std::vector<CellJob> jobs;
	while (prewarmVisited < limit && prewarmRing <= lastRing &&
		   std::chrono::steady_clock::now() - started < FrameBudget && queued + jobs.size() < queueLimit)
	{
		// Step `prewarmStep` of ring `prewarmRing` around the centre page.
		const int ring = prewarmRing, edgeSteps = std::max(1, 2 * ring);
		int dx = 0, dy = 0;
		if (ring)
		{
			const int edge = prewarmStep / edgeSteps, along = prewarmStep % edgeSteps;
			dx = edge == 0 ? -ring + along : edge == 1 ? ring : edge == 2 ? ring - along : -ring;
			dy = edge == 0 ? -ring : edge == 1 ? -ring + along : edge == 2 ? ring : ring - along;
		}
		const auto advance = [&]
		{
			if (++prewarmStep >= (ring ? 8 * ring : 1))
			{
				++prewarmRing;
				prewarmStep = 0;
			}
		};
		// Rings wider than the map would wrap onto pages they already passed.
		if (std::abs(dx) > columns / 2 || std::abs(dy) > rows / 2 || (columns % 2 == 0 && dx == columns / 2) ||
			(rows % 2 == 0 && dy == rows / 2))
		{
			advance();
			continue;
		}
		const int wx = ((cx + dx) % columns + columns) % columns * side;
		const int wy = ((cy + dy) % rows + rows) % rows * side;
		// Stamped as used now: the view's own pages, drawn after this, stay more
		// recent, and the pages farthest from the camera go first.
		auto &page = this->page(map, wx, wy, samples);
		if (!validate(map, page, wx, wy, Region{wx, wy, wx + side, wy + side}, visibleTeams, wholeMap, jobs,
					  queueLimit - queued,
					  SDL_GetTicksNS() + Uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(FrameBudget).count())))
			break; // Resume this page next frame.
		advance();
		++prewarmVisited;
		if (jobs.size() >= CellsPerBatch)
		{
			queued += jobs.size();
			submit(jobs, true);
		}
	}
	if (!jobs.empty())
		submit(jobs, true);
	trim(viewBytes);
	prewarmDone = prewarmVisited >= limit || prewarmRing > lastRing;
}

void OverviewTerrainCache::uploadAhead()
{
	if (!prewarmSamples)
		return;
	// Complete pages of the prewarmed density the device does not hold, each
	// revision tried once: a page the device evicted since uploads again when
	// a view draws it, never in a loop against the textures that evicted it.
	std::vector<std::pair<int, Page *>> waiting;
	const int w = mapWidth, h = mapHeight;
	for (auto &[key, page] : pages)
	{
		if (page.samples != prewarmSamples || !page.valid || page.uncomposed || !page.image ||
			page.aheadRevision == page.image->contentRevision() + (page.changed ? 1 : 0))
			continue;
		if (page.image->deviceHoldsPixels())
		{
			// Held already (or a renderer that uploads as it draws).
			page.aheadRevision = page.image->contentRevision() + (page.changed ? 1 : 0);
			continue;
		}
		const auto apart = [](int a, int b, int period)
		{
			const int d = std::abs(a - b) % period;
			return std::min(d, period - d);
		};
		const int x = int(key & 0xFFFFFF), y = int(key >> 24 & 0xFFFFFF);
		const int dx = apart(x + side / 2, viewX, w), dy = apart(y + side / 2, viewY, h);
		waiting.emplace_back(dx * dx + dy * dy, &page);
	}
	std::sort(waiting.begin(), waiting.end(),
			  [](const auto &a, const auto &b) { return a.first < b.first; });
	for (auto &[distance, page] : waiting)
	{
		(void)distance;
		if (page->changed)
		{
			page->image->markPixelsChanged();
			page->changed = false;
		}
		if (!page->image->prepareDeviceTexture())
			break; // The frame's allowance is spent.
		page->aheadRevision = page->image->contentRevision();
		++uploadedAhead;
	}
}
