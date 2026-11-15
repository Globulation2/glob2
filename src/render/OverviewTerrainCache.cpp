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
	capacities.clear();
	output.reset();
	mapIdentity = map.identity();
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
		const int pixels = Tiles * samples;
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
			SDL_CreateSurface(pixels, pixels, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
		if (!surface)
			throw std::bad_alloc();
		// Cells not composed yet stay transparent.
		std::memset(surface->pixels, 0, std::size_t(surface->pitch) * pixels);
		Page fresh;
		fresh.samples = samples;
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

std::size_t OverviewTerrainCache::pageStorage(int samples)
{
	// CPU pixels plus the texture a drawn page uploads.
	return sizeof(Page) + std::size_t(Tiles * samples) * (Tiles * samples) * 4 * 2;
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
									std::size_t jobLimit)
{
	const int samples = page.samples;
	const bool fresh = !page.valid || page.seed != map.terrainSeed();
	if (fresh || page.terrainRevision != map.terrainRevision())
	{
		std::array<Uint32, (Tiles + 3) * (Tiles + 3)> vertices{};
		for (int y = -1; y <= Tiles + 1; ++y)
			for (int x = -1; x <= Tiles + 1; ++x)
				vertices[(y + 1) * (Tiles + 3) + x + 1] = map.vertexTerrainAt(wx + x, wy + y);
		if (fresh || vertices != page.vertices)
		{
			page.resources.fill(Uncomposed);
			page.inFlight.fill(Uncomposed);
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
		for (int y = 0; y < Tiles; ++y)
			for (int x = 0; x < Tiles; ++x)
			{
				const int i = y * Tiles + x;
				if (page.resources[i] == Uncomposed)
					continue;
				if (wx + x >= region.x0 && wx + x < region.x1 && wy + y >= region.y0 && wy + y < region.y1)
					continue;
				int type, square;
				if (cellCode(map, wx + x, wy + y, visibleTeams, wholeMap, samples, type, square) != page.resources[i])
					page.resources[i] = Uncomposed;
			}
		page.resourceRevision = map.resourcePresentationRevision();
		page.discoveryRevision = discovery;
		page.visibleTeams = teams;
		page.wholeMap = wholeMap;
	}
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
			if (page.resources[i] == code || page.inFlight[i] == code)
				continue;
			if (jobs.size() >= jobLimit)
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
		page.resources[job.index] = job.code;
		page.inFlight[job.index] = Uncomposed;
		page.changed = true;
		++compositions;
	}
	jobs.clear();
}

void OverviewTerrainCache::submit(std::vector<CellJob> &jobs)
{
	for (std::size_t first = 0; first < jobs.size(); first += CellsPerBatch)
	{
		auto batch = std::make_shared<Batch>();
		const auto last = std::min(jobs.size(), first + CellsPerBatch);
		batch->jobs.assign(std::make_move_iterator(jobs.begin() + first), std::make_move_iterator(jobs.begin() + last));
		batch->catalog = catalog;
		batches.push_back(batch);
		BackgroundWork::submit([batch]
		{
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
		});
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
									   int vy, bool centreFirst, Visit visit)
{
	const auto chunkOf = [](int tile) { return int(std::floor(double(tile) / Tiles)); };
	const int cy0 = chunkOf(top + vy), cy1 = chunkOf(bottom + vy);
	const int cx0 = chunkOf(left + vx), cx1 = chunkOf(right + vx);
	const int columns = cx1 - cx0 + 1;
	std::vector<int> order(std::size_t(columns) * (cy1 - cy0 + 1));
	for (std::size_t i = 0; i < order.size(); ++i)
		order[i] = int(i);
	if (centreFirst)
	{
		const double centreX = (left + right + 1) * .5 + vx, centreY = (top + bottom + 1) * .5 + vy;
		const auto distance = [&](int i)
		{
			const double dx = (cx0 + i % columns) * Tiles + Tiles * .5 - centreX;
			const double dy = (cy0 + i / columns) * Tiles + Tiles * .5 - centreY;
			return dx * dx + dy * dy;
		};
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return distance(a) < distance(b); });
	}
	for (const int i : order)
	{
		const int cx = cx0 + i % columns, cy = cy0 + i / columns;
		const int wx = (cx * Tiles) & map.getMaskW(), wy = (cy * Tiles) & map.getMaskH();
		// The visible part of the page, in unwrapped and in page-wrapped tiles.
		const int x0 = std::max(left + vx, cx * Tiles), x1 = std::min(right + vx + 1, (cx + 1) * Tiles);
		const int y0 = std::max(top + vy, cy * Tiles), y1 = std::min(bottom + vy + 1, (cy + 1) * Tiles);
		const Region region{wx + x0 - cx * Tiles, wy + y0 - cy * Tiles, wx + x1 - cx * Tiles, wy + y1 - cy * Tiles};
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
	std::size_t viewBytes = 0;
	std::vector<CellJob> jobs;
	auto *pixels = target.getSDLSurface();
	forEachPage(map, left, top, right, bottom, vx, vy, false,
				[&](int wx, int wy, const Region &region, int screenX, int screenY)
	{
		auto &page = this->page(map, wx, wy, samples);
		viewBytes += pageStorage(samples);
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
	trim(viewBytes);
	target.markPixelsChanged();
	next.surfaceRevision = target.contentRevision();
	output = next;
}

void OverviewTerrainCache::draw(const SceneMap &map, int left, int top, int right, int bottom, int vx, int vy,
								Uint32 visibleTeams, bool wholeMap, int samples, GAGCore::DrawableSurface &target,
								Uint8 alpha, bool complete)
{
	if (samples != FewSamples && samples != ManySamples)
		throw std::invalid_argument("Overview samples must be 4 or 8 per cell");
	pageSamples = samples;
	resetForMap(map);
	collect();
	const auto started = std::chrono::steady_clock::now();
	const auto elapsed = [&] { return std::chrono::steady_clock::now() - started; };
	const int other = samples == FewSamples ? ManySamples : FewSamples;
	// Cells described per frame for the workers; batches still running count too.
	std::size_t queued = 0;
	for (const auto &batch : batches)
		queued += batch->jobs.size();
	const std::size_t queueLimit = std::size_t(std::max(1u, BackgroundWork::workers())) * CellsPerBatch * 4;
	std::size_t viewBytes = 0;
	std::vector<CellJob> jobs;
	forEachPage(map, left, top, right, bottom, vx, vy, !complete,
				[&](int wx, int wy, const Region &region, int screenX, int screenY)
	{
		viewBytes += pageStorage(samples);
		auto *page = &this->page(map, wx, wy, samples);
		bool shown = true;
		if (complete)
		{
			validate(map, *page, wx, wy, region, visibleTeams, wholeMap, jobs, std::numeric_limits<std::size_t>::max());
			composeNow(jobs);
		}
		else if (elapsed() < FrameBudget && queued + jobs.size() < queueLimit)
		{
			const std::size_t limit = queueLimit - queued;
			if (!validate(map, *page, wx, wy, region, visibleTeams, wholeMap, jobs, limit))
				shown = false;
			if (jobs.size() >= CellsPerBatch)
			{
				queued += jobs.size();
				submit(jobs);
			}
		}
		if (!complete)
			shown = shown && covers(*page, wx, wy, region);
		if (!shown)
		{
			// The other density, if it already shows this part, stands in beneath.
			auto found = pages.find(pageKey(other, wx, wy));
			if (found != pages.end() && found->second.valid && found->second.seed == map.terrainSeed())
				drawPage(found->second, wx, wy, region, screenX, screenY, target, alpha);
		}
		drawPage(*page, wx, wy, region, screenX, screenY, target, alpha);
	});
	if (!jobs.empty())
		submit(jobs);
	trim(viewBytes * 2);
}

bool OverviewTerrainCache::covers(const Page &page, int wx, int wy, const Region &region) const
{
	for (int my = region.y0; my < region.y1; ++my)
		for (int mx = region.x0; mx < region.x1; ++mx)
			if (page.resources[(my - wy) * Tiles + (mx - wx)] == Uncomposed)
				return false;
	return true;
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
