// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkinSprites.h"
#include "OnlineStorage.h"
#include "Sha256.h"
#include <GraphicContext.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>

namespace Online
{
namespace
{
const std::string CacheDirectory = "online/skin-sprites/";
constexpr std::size_t MemoryLimit = 64 * 1024 * 1024;
constexpr std::size_t DiskLimit = 256 * 1024 * 1024;
constexpr std::size_t PageLimit = 2 * 1024 * 1024;
constexpr unsigned DownloadLimit = 4;
constexpr unsigned RetryLimit = 3;
constexpr unsigned TileSize = 128;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

std::string pagePath(const SkinSpritePage &page)
{
	return CacheDirectory + page.hash + ".webp";
}
} // namespace

SkinSprites::SkinSprites(OnlineStorage &s, std::string o, SkinDownloads::FetchStarter f)
	: storage(s), origin(std::move(o)), start(std::move(f))
{
	for (const auto &name : storage.list("online/skin-sprites"))
	{
		if (name.size() != 69 || name.substr(64) != ".webp" ||
			!Sha256::isHexDigest(name.substr(0, 64)))
			continue;
		const auto path = CacheDirectory + name;
		const auto size = storage.size(path);
		if (size > 0 && size <= PageLimit)
		{
			disk[path] = {size, 0};
			diskBytes += size;
		}
		else
		{
			eraseDisk(path);
		}
	}
	while (diskBytes > DiskLimit && !disk.empty())
		eraseDisk(disk.begin()->first);
	if (diskChanged)
		storage.persist();
	diskChanged = false;
}
SkinSprites::~SkinSprites() = default;

void SkinSprites::install(const AuthorizedSkin &skin)
{
	if (skin.team < 0 || skin.team >= 32)
		return;
	auto &team = teams[skin.team];
	if (team.skin.manifestHash == skin.manifestHash &&
		team.skin.spriteManifestHash == skin.spriteManifestHash)
	{
		team.skin = skin;
		if (team.manifest.pages.empty())
			team.tried = false;
		for (const auto &page : team.manifest.pages)
			if (auto found = pages.find(page.hash); found != pages.end())
				found->second.failures = 0;
		return;
	}
	remove(skin.team);
	team.skin = skin;
}

bool SkinSprites::authorized(const std::string &hash) const
{
	return std::any_of(teams.begin(), teams.end(),
					   [&](const auto &team)
					   {
						   return std::any_of(team.manifest.pages.begin(),
											  team.manifest.pages.end(),
											  [&](const auto &page) { return page.hash == hash; });
					   });
}

void SkinSprites::remove(int index)
{
	if (index < 0 || index >= 32)
		return;
	auto &team = teams[index];
	if (team.fetch)
		team.fetch->cancel();
	team = Team{};
	// Removing one team must not remove shared pages still authorized by another.
	for (auto it = pages.begin(); it != pages.end();)
	{
		if (authorized(it->first))
		{
			++it;
			continue;
		}
		if (it->second.surface)
			decoded -= it->second.cost;
		if (it->second.fetch)
			it->second.fetch->cancel();
		it = pages.erase(it);
	}
}

void SkinSprites::eraseDisk(std::string path)
{
	if (const auto found = disk.find(path); found != disk.end())
	{
		diskBytes -= found->second.bytes;
		disk.erase(found);
	}
	storage.remove(path);
	diskChanged = true;
}

bool SkinSprites::cache(const std::string &path, const std::string &bytes)
{
	if (!storage.write(path, bytes))
		return false;
	if (const auto found = disk.find(path); found != disk.end())
		diskBytes -= found->second.bytes;
	disk[path] = {bytes.size(), clock};
	diskBytes += bytes.size();
	diskChanged = true;
	while (diskBytes > DiskLimit && !disk.empty())
	{
		const auto oldest =
			std::min_element(disk.begin(), disk.end(), [](const auto &a, const auto &b)
							 { return a.second.touched < b.second.touched; });
		eraseDisk(oldest->first);
	}
	return true;
}

void SkinSprites::finishDownloads(unsigned &active)
{
	for (auto &[hash, page] : pages)
	{
		if (!page.readyBytes.empty() && !page.requested)
		{
			std::string{}.swap(page.readyBytes);
			--active;
		}
		if (!page.fetch || page.fetch->state() == HttpFetch::State::Pending)
			continue;
		std::string bytes;
		if (page.fetch->state() == HttpFetch::State::Done && page.fetch->response().status == 200)
			bytes = page.fetch->response().body;
		page.fetch.reset();
		--active;
		if (!validSkinSpritePage(bytes, page.info))
		{
			++page.failures;
			continue;
		}
		// Always retire completed requests, even after a camera move. Offscreen
		// responses may populate disk, but must never occupy a download slot.
		if (!cache(pagePath(page.info), bytes) && page.requested)
		{
			page.readyBytes = std::move(bytes);
			++active;
		}
	}
}

void SkinSprites::pollManifests(unsigned &active)
{
	for (auto &team : teams)
	{
		if (team.skin.spriteManifestHash.empty() || !team.manifest.pages.empty() || team.tried)
			continue;
		if (team.fetch)
		{
			if (team.fetch->state() == HttpFetch::State::Pending)
				continue;
			if (team.fetch->state() == HttpFetch::State::Done &&
				team.fetch->response().status == 200)
				team.manifest.parse(team.fetch->response().body, team.skin);
			team.fetch.reset();
			--active;
			team.tried = true;
		}
		else if (active < DownloadLimit)
		{
			HttpFetch::Request request;
			request.url = origin + "/api/v1/skins/versions/" + team.skin.versionId + "/sprites/" +
						  team.skin.spriteManifestHash + "/manifest";
			request.responseLimit = 65536;
			request.timeout = std::chrono::seconds(15);
			team.fetch = start(std::move(request));
			if (team.fetch)
				++active;
			else
				team.tried = true;
		}
	}
}

bool SkinSprites::loadPageBytes(Page &page, std::string &bytes, unsigned &active)
{
	if (page.fetch)
		return false;
	if (!page.readyBytes.empty())
	{
		bytes = std::move(page.readyBytes);
		std::string{}.swap(page.readyBytes);
		--active;
		return true;
	}
	const auto path = pagePath(page.info);
	if (storage.size(path) == page.info.bytes && storage.read(path, bytes) &&
		validSkinSpritePage(bytes, page.info))
	{
		if (auto found = disk.find(path); found != disk.end())
			found->second.touched = clock;
		return true;
	}
	// Remove corrupt entries from both storage and byte accounting.
	if (disk.count(path) || storage.size(path))
		eraseDisk(path);
	if (active < DownloadLimit)
	{
		HttpFetch::Request request;
		request.url = origin + "/api/v1/skins/versions/" + page.version + "/sprites/" +
					  page.bundle + "/pages/" + page.info.hash;
		request.responseLimit = page.info.bytes;
		request.timeout = std::chrono::seconds(15);
		page.fetch = start(std::move(request));
		if (page.fetch)
			++active;
		else
			++page.failures;
	}
	return false;
}

void SkinSprites::evictDecoded(std::size_t incomingBytes)
{
	while (decoded + incomingBytes > MemoryLimit)
	{
		auto oldest = pages.end();
		for (auto it = pages.begin(); it != pages.end(); ++it)
			if (it->second.surface &&
				(oldest == pages.end() || it->second.touched < oldest->second.touched))
				oldest = it;
		if (oldest == pages.end())
			break;
		decoded -= oldest->second.cost;
		oldest->second.surface.reset();
		++metrics.evictions;
	}
}

bool SkinSprites::decodePage(Page &page, const std::string &bytes)
{
	Surface loaded(IMG_Load_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true),
				   SDL_DestroySurface);
	if (!loaded || loaded->w != int(page.info.size) || loaded->h != int(page.info.size))
		return false;
	Surface rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
	if (!rgba)
		return false;
	loaded.reset();
	const unsigned columns = page.info.size == TileSize ? 1 : 8;
	const unsigned rows = columns;
	page.cellW = page.cellH = 1;
	// Retain full-resolution pixels, with a transparent filtering guard around
	// each pose. Packing discards only unused margins, not visible coverage.
	for (unsigned frame = 0; frame < page.info.frames; ++frame)
	{
		unsigned left = TileSize, top = TileSize, right = 0, bottom = 0;
		for (unsigned y = 0; y < TileSize; ++y)
		{
			const auto *pixels = reinterpret_cast<const uint32_t *>(
									 static_cast<const char *>(rgba->pixels) +
									 ((frame / columns) * TileSize + y) * rgba->pitch) +
								 (frame % columns) * TileSize;
			for (unsigned x = 0; x < TileSize; ++x)
				if (pixels[x] >> 24)
				{
					left = std::min(left, x);
					top = std::min(top, y);
					right = std::max(right, x + 1);
					bottom = std::max(bottom, y + 1);
				}
		}
		auto &bounds = page.frames[frame];
		bounds = Frame{};
		if (left < right)
		{
			bounds.x = left ? left - 1 : 0;
			bounds.y = top ? top - 1 : 0;
			bounds.w = std::min(TileSize, right + 1) - bounds.x;
			bounds.h = std::min(TileSize, bottom + 1) - bounds.y;
		}
		page.cellW = std::max(page.cellW, bounds.w);
		page.cellH = std::max(page.cellH, bounds.h);
	}
	const std::size_t cost = std::size_t(page.cellW) * columns * page.cellH * rows * 4;
	evictDecoded(cost);
	page.surface =
		std::make_unique<GAGCore::DrawableSurface>(page.cellW * columns, page.cellH * rows);
	SDL_FillSurfaceRect(page.surface->getSDLSurface(), nullptr, 0);
	SDL_SetSurfaceBlendMode(rgba.get(), SDL_BLENDMODE_NONE);
	for (unsigned frame = 0; frame < page.info.frames; ++frame)
	{
		const auto &bounds = page.frames[frame];
		SDL_Rect source{int((frame % columns) * TileSize + bounds.x),
						int((frame / columns) * TileSize + bounds.y), int(bounds.w), int(bounds.h)};
		SDL_Rect destination{int((frame % columns) * page.cellW),
							 int((frame / columns) * page.cellH), int(bounds.w), int(bounds.h)};
		SDL_BlitSurface(rgba.get(), &source, page.surface->getSDLSurface(), &destination);
	}
	page.surface->markPixelsChanged();
	page.cost = cost;
	decoded += cost;
	++metrics.decodes;
	return true;
}

void SkinSprites::poll()
{
	++clock;
	unsigned active = 0;
	for (const auto &team : teams)
		if (team.fetch)
			++active;
	for (const auto &[hash, page] : pages)
	{
		if (page.fetch)
			++active;
		if (!page.readyBytes.empty())
			++active;
	}
	finishDownloads(active);
	pollManifests(active);
	// Decode at most one currently demanded page per poll, outside drawing.
	std::vector<Page *> requested;
	for (auto &[hash, page] : pages)
		if (page.requested && !page.surface && page.failures < RetryLimit)
			requested.push_back(&page);
	std::sort(requested.begin(), requested.end(),
			  [](auto *a, auto *b) { return a->touched > b->touched; });
	bool decodedOne = false;
	for (auto *page : requested)
	{
		if (decodedOne)
			break;
		std::string bytes;
		if (!loadPageBytes(*page, bytes, active))
			continue;
		decodedOne = true;
		if (!decodePage(*page, bytes))
			++page->failures;
	}
	for (auto &[hash, page] : pages)
		page.requested = false;
	if (diskChanged)
		storage.persist();
	diskChanged = false;
}

bool SkinSprites::draw(GAGCore::GraphicContext &gfx, int team, unsigned clip, unsigned frame,
					   float x, float y, float w, float h, GAGCore::DrawableSurface *shadow,
					   unsigned char alpha)
{
	if (team < 0 || team >= 32 || clip >= 8)
		return false;
	const auto &manifest = teams[team].manifest;
	const auto found = std::find_if(
		manifest.pages.begin(), manifest.pages.end(), [&](const auto &page)
		{ return page.clip == clip && frame >= page.first && frame < page.first + page.frames; });
	if (found == manifest.pages.end())
		return false;
	auto &page = pages[found->hash];
	page.info = *found;
	page.version = teams[team].skin.versionId;
	page.bundle = teams[team].skin.spriteManifestHash;
	page.touched = clock;
	page.requested = true;
	if (!page.surface)
	{
		++metrics.misses;
		return false;
	}
	++metrics.hits;
	if (!alpha)
		return true;
	if (shadow)
		gfx.drawSurface(x, y, w, h, shadow, alpha);
	x -= w * 0.125f;
	y -= h * 0.125f;
	w *= 1.25f;
	h *= 1.25f;
	const unsigned cell = frame - found->first;
	const auto &bounds = page.frames[cell];
	x += w * bounds.x / TileSize;
	y += h * bounds.y / TileSize;
	w *= float(bounds.w) / TileSize;
	h *= float(bounds.h) / TileSize;
	gfx.drawSkinSprite(x, y, w, h, page.surface.get(), (cell % 8) * page.cellW,
					   (cell / 8) * page.cellH, bounds.w, bounds.h, alpha);
	return true;
}
} // namespace Online
