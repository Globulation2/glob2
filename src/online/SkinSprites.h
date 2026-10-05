// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SkinDownloads.h"
#include "SkinSpriteManifest.h"
#include <array>
#include <map>

namespace GAGCore
{
class GraphicContext;
class DrawableSurface;
} // namespace GAGCore
namespace Online
{
// View-owned presentation cache. Hashes share pages across authorized teams;
// drawing records demand, and polling performs bounded download/decode work.
class SkinSprites
{
  public:
	SkinSprites(OnlineStorage &, std::string origin, SkinDownloads::FetchStarter);
	~SkinSprites();
	void install(const AuthorizedSkin &);
	void remove(int team);
	void poll();
	bool draw(GAGCore::GraphicContext &, int team, unsigned clip, unsigned frame, float x, float y,
			  float w, float h, GAGCore::DrawableSurface *shadow = nullptr,
			  unsigned char alpha = 255);
	std::size_t decodedBytes() const { return decoded; }
	struct Counters
	{
		std::uint64_t decodes = 0, evictions = 0, hits = 0, misses = 0;
	};
	const Counters &counters() const { return metrics; }

  private:
	struct Team
	{
		AuthorizedSkin skin;
		SkinSpriteManifest manifest;
		std::unique_ptr<HttpFetch::Fetch> fetch;
		bool tried = false;
	};
	struct Frame
	{
		unsigned x = 0, y = 0, w = 1, h = 1;
	};
	struct Page
	{
		SkinSpritePage info;
		std::string version, bundle;
		std::unique_ptr<HttpFetch::Fetch> fetch;
		// Only needed when persistent storage rejects a verified download.
		// These buffers share the four-slot budget with in-flight requests.
		std::string readyBytes;
		std::unique_ptr<GAGCore::DrawableSurface> surface;
		std::array<Frame, 64> frames;
		unsigned cellW = 128, cellH = 128;
		std::size_t cost = 0;
		std::uint64_t touched = 0;
		bool requested = false;
		unsigned failures = 0;
	};
	struct DiskPage
	{
		std::size_t bytes = 0;
		std::uint64_t touched = 0;
	};

	OnlineStorage &storage;
	std::string origin;
	SkinDownloads::FetchStarter start;
	std::array<Team, 32> teams;
	std::map<std::string, Page> pages;
	std::map<std::string, DiskPage> disk;
	std::size_t decoded = 0, diskBytes = 0;
	std::uint64_t clock = 0;
	bool diskChanged = false;
	Counters metrics;

	bool authorized(const std::string &hash) const;
	bool cache(const std::string &path, const std::string &bytes);
	void eraseDisk(std::string path);
	void evictDecoded(std::size_t incomingBytes);
	void finishDownloads(unsigned &active);
	void pollManifests(unsigned &active);
	bool loadPageBytes(Page &, std::string &bytes, unsigned &active);
	bool decodePage(Page &, const std::string &bytes);
};
} // namespace Online
