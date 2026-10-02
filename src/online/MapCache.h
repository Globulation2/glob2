// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "HttpFetch.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

// Content-addressed cache of maps and saved games in the user directory, shared
// by online matches and LAN games.
//
// Rooms and matches name maps by the SHA-256 of their decompressed bytes
// (map_versions.sha256, MatchSetup map.hash). The cache stores each as
// online/maps/<hash>.map.gz (saved games: <hash>.game.gz), which
// FileManager::openInflatingInputStreamBackend reads like any gzip map, and
// evicts the least recently used files beyond a size cap.
namespace Online
{
class OnlineStorage;

class MapCache
{
  public:
	static constexpr const char *DIRECTORY = "online/maps";
	static constexpr const char *INDEX = "online/maps/index.json";
	static constexpr std::uint64_t DEFAULT_CAPACITY = 256ull * 1024 * 1024;
	// Same bound as imported maps (docs/browser/storage.md).
	static constexpr std::size_t MAX_MAP_BYTES = 64 * 1024 * 1024;

	// Where an instance serves a map blob. The endpoint arrives with M4; this
	// is the one place that names it.
	static std::string blobPath(const std::string &hash);

	// startFetch is HttpFetch::start outside tests.
	using FetchStarter = std::function<std::unique_ptr<HttpFetch::Fetch>(HttpFetch::Request)>;
	MapCache(OnlineStorage &storage, FetchStarter startFetch,
			 std::uint64_t capacity = DEFAULT_CAPACITY);

	bool contains(const std::string &hash) const;
	// FileManager path of a cached map, marking it recently used; empty when
	// it is not cached or its file disappeared.
	std::optional<std::string> path(const std::string &hash);
	// Stores a map given as raw or gzip bytes after checking that the SHA-256
	// of its decompressed bytes is `hash`. Evicts older maps beyond capacity
	// (never the one just stored). savedGame names the file <hash>.game.gz.
	bool insert(const std::string &hash, const std::string &bytes, std::string *error = nullptr,
				bool savedGame = false);
	void remove(const std::string &hash);

	// Bytes of the stored (compressed) files.
	std::uint64_t storedBytes() const;
	std::uint64_t capacity() const
	{
		return limit;
	}
	std::size_t count() const
	{
		return entries.size();
	}

	// A polled download (see HttpFetch). Done once the map is cached.
	class Download
	{
	  public:
		enum class State
		{
			Pending,
			Done,
			Failed
		};
		virtual ~Download() = default;
		virtual State state() = 0;
		virtual std::string path() const = 0;
		virtual std::string error() const = 0;
		virtual void cancel() = 0;
	};
	// Fetches origin + blobPath(hash) unless the map is cached. headers may
	// carry the bearer token for private maps. The cache must outlive it.
	std::unique_ptr<Download> fetch(const std::string &origin, const std::string &hash,
									HttpFetch::Headers headers = {});

  private:
	struct Entry
	{
		std::uint64_t size = 0;
		std::uint64_t lastUse = 0;
		bool save = false;
	};
	static std::string fileFor(const std::string &hash, bool save = false);
	void loadIndex();
	void saveIndex();
	void evict(const std::string &keep);

	OnlineStorage &storage;
	std::uint64_t limit;
	FetchStarter startFetch;
	std::map<std::string, Entry> entries;
	std::uint64_t useCounter = 0;
};

// The decompressed bytes of a map or save file, read through the Toolkit
// FileManager (".gz" is inflated). False if the file cannot be read.
bool readMapBytes(const std::string &path, std::string &bytes);
} // namespace Online
