// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapCache.h"
#include "OnlineStorage.h"
#include "Sha256.h"

#include <GzipUtil.h>
#include <nlohmann/json.hpp>

namespace Online
{
namespace
{
bool isGzip(const std::string &bytes)
{
	return bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0x1f &&
		   static_cast<unsigned char>(bytes[1]) == 0x8b;
}
} // namespace

std::string MapCache::blobPath(const std::string &hash)
{
	return "/api/v1/blobs/maps/" + hash;
}

std::string MapCache::fileFor(const std::string &hash)
{
	return std::string(DIRECTORY) + "/" + hash + ".map.gz";
}

MapCache::MapCache(OnlineStorage &storage, FetchStarter startFetch, std::uint64_t capacity)
	: storage(storage), limit(capacity), startFetch(std::move(startFetch))
{
	loadIndex();
}

void MapCache::loadIndex()
{
	entries.clear();
	useCounter = 0;
	std::string text;
	if (storage.read(INDEX, text))
	{
		auto index = nlohmann::json::parse(text, nullptr, false);
		if (index.is_object() && index.contains("maps") && index["maps"].is_object())
			for (const auto &[hash, entry] : index["maps"].items())
			{
				if (!Sha256::isHexDigest(hash) || !entry.is_object())
					continue;
				Entry value;
				value.size = entry.value("size", std::uint64_t(0));
				value.lastUse = entry.value("lastUse", std::uint64_t(0));
				entries[hash] = value;
				useCounter = std::max(useCounter, value.lastUse);
			}
	}
	// Files without an index entry (an interrupted write, a lost index) are
	// unaccounted for and would never be evicted: remove them.
	for (const auto &name : storage.list(DIRECTORY))
	{
		const auto suffix = std::string(".map.gz");
		if (name.size() == 64 + suffix.size() && name.compare(64, suffix.size(), suffix) == 0 &&
			entries.count(name.substr(0, 64)))
			continue;
		if (name != "index.json")
			storage.remove(std::string(DIRECTORY) + "/" + name);
	}
}

void MapCache::saveIndex()
{
	nlohmann::json maps = nlohmann::json::object();
	for (const auto &[hash, entry] : entries)
		maps[hash] = {{"size", entry.size}, {"lastUse", entry.lastUse}};
	storage.write(INDEX, nlohmann::json{{"version", 1}, {"maps", std::move(maps)}}.dump());
	storage.persist();
}

bool MapCache::contains(const std::string &hash) const
{
	return entries.count(hash) != 0;
}

std::optional<std::string> MapCache::path(const std::string &hash)
{
	auto found = entries.find(hash);
	if (found == entries.end())
		return std::nullopt;
	std::string probe;
	if (!storage.read(fileFor(hash), probe) || probe.size() != found->second.size)
	{
		entries.erase(found);
		storage.remove(fileFor(hash));
		saveIndex();
		return std::nullopt;
	}
	found->second.lastUse = ++useCounter;
	saveIndex();
	return fileFor(hash);
}

bool MapCache::insert(const std::string &hash, const std::string &bytes, std::string *error)
{
	auto fail = [&](const char *reason)
	{
		if (error)
			*error = reason;
		return false;
	};
	if (!Sha256::isHexDigest(hash))
		return fail("invalid map hash");
	std::string raw, compressed;
	if (isGzip(bytes))
	{
		if (!GAGCore::gzipDecompress(bytes, raw, MAX_MAP_BYTES + 1) || raw.size() > MAX_MAP_BYTES)
			return fail("map is corrupt or too large");
		compressed = bytes;
	}
	else
	{
		if (bytes.size() > MAX_MAP_BYTES)
			return fail("map is too large");
		raw = bytes;
		if (!GAGCore::gzipCompress(raw, 6, compressed))
			return fail("map could not be compressed");
	}
	if (Sha256::hex(raw) != hash)
		return fail("map does not match its hash");
	if (compressed.size() > limit)
		return fail("map is larger than the cache");
	if (!storage.write(fileFor(hash), compressed))
		return fail("map could not be written");
	entries[hash] = Entry{compressed.size(), ++useCounter};
	evict(hash);
	saveIndex();
	return true;
}

void MapCache::remove(const std::string &hash)
{
	if (!entries.erase(hash))
		return;
	storage.remove(fileFor(hash));
	saveIndex();
}

std::uint64_t MapCache::storedBytes() const
{
	std::uint64_t total = 0;
	for (const auto &[hash, entry] : entries)
		total += entry.size;
	return total;
}

void MapCache::evict(const std::string &keep)
{
	auto total = storedBytes();
	while (total > limit)
	{
		auto oldest = entries.end();
		for (auto i = entries.begin(); i != entries.end(); ++i)
			if (i->first != keep && (oldest == entries.end() || i->second.lastUse < oldest->second.lastUse))
				oldest = i;
		if (oldest == entries.end())
			break;
		total -= oldest->second.size;
		storage.remove(fileFor(oldest->first));
		entries.erase(oldest);
	}
}

namespace
{
class CacheDownload final : public MapCache::Download
{
  public:
	CacheDownload(MapCache &cache, std::string hash, std::unique_ptr<HttpFetch::Fetch> fetch)
		: cache(cache), hash(std::move(hash)), fetch(std::move(fetch))
	{
	}
	// Already finished: a cached map (Done, path) or a rejected request.
	CacheDownload(MapCache &cache, State state, std::string path, std::string failure)
		: cache(cache), done(state), file(std::move(path)), failure(std::move(failure))
	{
	}
	State state() override
	{
		if (done != State::Pending || !fetch)
			return done;
		switch (fetch->state())
		{
		case HttpFetch::State::Pending:
			return State::Pending;
		case HttpFetch::State::Done:
		{
			const auto &response = fetch->response();
			if (response.status != 200)
				finish(State::Failed, "map download failed with HTTP status " +
										  std::to_string(response.status));
			else if (std::string reason; !cache.insert(hash, response.body, &reason))
				finish(State::Failed, reason);
			else if (auto path = cache.path(hash))
			{
				file = *path;
				finish(State::Done, {});
			}
			else
				finish(State::Failed, "map could not be read back from the cache");
			break;
		}
		case HttpFetch::State::TimedOut:
			finish(State::Failed, "map download timed out");
			break;
		case HttpFetch::State::Cancelled:
			finish(State::Failed, "map download cancelled");
			break;
		case HttpFetch::State::Failed:
			finish(State::Failed, "map download failed: " + fetch->error());
			break;
		}
		return done;
	}
	std::string path() const override
	{
		return file;
	}
	std::string error() const override
	{
		return failure;
	}
	void cancel() override
	{
		if (done != State::Pending)
			return;
		if (fetch)
			fetch->cancel();
		finish(State::Failed, "map download cancelled");
	}

  private:
	void finish(State state, std::string reason)
	{
		done = state;
		failure = std::move(reason);
		fetch.reset();
	}
	MapCache &cache;
	std::string hash;
	std::unique_ptr<HttpFetch::Fetch> fetch;
	State done = State::Pending;
	std::string file, failure;
};
} // namespace

std::unique_ptr<MapCache::Download> MapCache::fetch(const std::string &origin,
													const std::string &hash,
													HttpFetch::Headers headers)
{
	if (!Sha256::isHexDigest(hash))
		return std::make_unique<CacheDownload>(*this, Download::State::Failed, std::string(),
											   "invalid map hash");
	if (auto cached = path(hash))
		return std::make_unique<CacheDownload>(*this, Download::State::Done, *cached,
											   std::string());
	HttpFetch::Request request;
	request.url = origin + blobPath(hash);
	request.headers = std::move(headers);
	request.timeout = std::chrono::milliseconds(120000);
	// Gzip maps are far smaller than MAX_MAP_BYTES; raw ones may approach it.
	request.responseLimit = MAX_MAP_BYTES;
	auto started = startFetch ? startFetch(std::move(request)) : nullptr;
	if (!started)
		return std::make_unique<CacheDownload>(*this, Download::State::Failed, std::string(),
											   "maps cannot be downloaded here");
	return std::make_unique<CacheDownload>(*this, hash, std::move(started));
}
} // namespace Online
