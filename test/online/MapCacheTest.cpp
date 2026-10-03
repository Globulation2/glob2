// SPDX-License-Identifier: GPL-3.0-or-later
// Content-addressed map cache: hash verification, LRU eviction, downloads.

#include "Glob2Test.h"
#include "MapCache.h"
#include "OnlineFakes.h"
#include "OnlineStorage.h"
#include "Sha256.h"

#include <GzipUtil.h>

using namespace Online;

namespace
{
MapCache::FetchStarter noFetch()
{
	return [](HttpFetch::Request) -> std::unique_ptr<HttpFetch::Fetch> { return nullptr; };
}

// Incompressible-ish map bytes of a given size.
std::string mapBytes(char seed, std::size_t size)
{
	std::string bytes(size, '\0');
	std::uint32_t state = static_cast<unsigned char>(seed) * 2654435761u + 1;
	for (auto &byte : bytes)
	{
		state = state * 1664525u + 1013904223u;
		byte = char(state >> 24);
	}
	return bytes;
}

std::string gzip(const std::string &raw)
{
	std::string out;
	REQUIRE(GAGCore::gzipCompress(raw, 6, out));
	return out;
}
} // namespace

TEST_SUITE("MapCache")
{
	TEST_CASE("maps are stored by the hash of their decompressed bytes")
	{
		MemoryStorage storage;
		MapCache cache(storage, noFetch());
		const auto raw = mapBytes('a', 5000);
		const auto hash = Sha256::hex(raw);
		std::string error;
		CHECK_FALSE(cache.insert(std::string(64, '0'), raw, &error));
		CHECK_EQ(error, "map does not match its hash");
		CHECK_FALSE(cache.insert("NOTAHASH", raw, &error));
		REQUIRE(cache.insert(hash, raw, &error));
		CHECK(cache.contains(hash));
		auto path = cache.path(hash);
		REQUIRE(path.has_value());
		CHECK_EQ(*path, "online/maps/" + hash + ".map.gz");
		std::string stored, inflated;
		REQUIRE(storage.read(*path, stored));
		REQUIRE(GAGCore::gzipDecompress(stored, inflated));
		CHECK_EQ(inflated, raw);

		// Gzip input is checked against its decompressed bytes and kept as is.
		const auto other = mapBytes('b', 3000);
		const auto compressed = gzip(other);
		REQUIRE(cache.insert(Sha256::hex(other), compressed));
		REQUIRE(storage.read(*cache.path(Sha256::hex(other)), stored));
		CHECK_EQ(stored, compressed);
		CHECK_FALSE(cache.insert(Sha256::hex(compressed), compressed, &error));
		CHECK_FALSE(cache.insert(Sha256::hex(other), compressed.substr(0, compressed.size() / 2), &error));
		CHECK_EQ(error, "map is corrupt or too large");
	}

	TEST_CASE("least recently used maps are evicted beyond the cap")
	{
		MemoryStorage storage;
		const auto a = mapBytes('a', 4000), b = mapBytes('b', 4000), c = mapBytes('c', 4000);
		const auto size = gzip(a).size();
		MapCache cache(storage, noFetch(), size * 2 + size / 2);
		REQUIRE(cache.insert(Sha256::hex(a), a));
		REQUIRE(cache.insert(Sha256::hex(b), b));
		REQUIRE(cache.path(Sha256::hex(a))); // a is now newer than b
		REQUIRE(cache.insert(Sha256::hex(c), c));
		CHECK(cache.contains(Sha256::hex(a)));
		CHECK_FALSE(cache.contains(Sha256::hex(b)));
		CHECK(cache.contains(Sha256::hex(c)));
		CHECK_FALSE(storage.files.count("online/maps/" + Sha256::hex(b) + ".map.gz"));
		CHECK(cache.storedBytes() <= cache.capacity());

		// A map larger than the whole cache is refused rather than emptying it.
		MapCache tiny(storage, noFetch(), 10);
		std::string error;
		CHECK_FALSE(tiny.insert(Sha256::hex(a), a, &error));
		CHECK_EQ(error, "map is larger than the cache");
	}

	TEST_CASE("the index survives restarts; orphans and vanished files are cleaned up")
	{
		MemoryStorage storage;
		const auto a = mapBytes('a', 2000), b = mapBytes('b', 2000);
		{
			MapCache cache(storage, noFetch());
			REQUIRE(cache.insert(Sha256::hex(a), a));
			REQUIRE(cache.insert(Sha256::hex(b), b));
		}
		storage.files["online/maps/" + std::string(64, 'f') + ".map.gz"] = "orphan";
		storage.files["online/maps/partial.tmp"] = "junk";
		storage.files.erase("online/maps/" + Sha256::hex(b) + ".map.gz");
		MapCache cache(storage, noFetch());
		CHECK_EQ(cache.count(), 2);
		CHECK_FALSE(storage.files.count("online/maps/" + std::string(64, 'f') + ".map.gz"));
		CHECK_FALSE(storage.files.count("online/maps/partial.tmp"));
		CHECK(cache.path(Sha256::hex(a)).has_value());
		CHECK_FALSE(cache.path(Sha256::hex(b)).has_value());
		CHECK_EQ(cache.count(), 1);
		cache.remove(Sha256::hex(a));
		CHECK_EQ(cache.count(), 0);
		CHECK_EQ(MapCache(storage, noFetch()).count(), 0);
	}

	TEST_CASE("downloads fetch the blob, verify it and serve it from the cache")
	{
		MemoryStorage storage;
		OnlineFakes::Http http;
		MapCache cache(storage, [&](HttpFetch::Request request) { return http.start(std::move(request)); });
		const auto raw = mapBytes('m', 10000);
		const auto hash = Sha256::hex(raw);
		auto download = cache.fetch("https://play.example.org", hash, {{"Authorization", "Bearer t"}});
		CHECK(download->state() == MapCache::Download::State::Pending);
		auto exchange = http.pending("/api/v1/blobs/maps/" + hash);
		REQUIRE(exchange);
		CHECK_EQ(exchange->request.url, "https://play.example.org" + MapCache::blobPath(hash));
		CHECK_EQ(exchange->header("Authorization"), "Bearer t");
		exchange->replyRaw(200, gzip(raw));
		CHECK(download->state() == MapCache::Download::State::Done);
		CHECK_EQ(download->path(), "online/maps/" + hash + ".map.gz");

		// Cached: no second request.
		auto again = cache.fetch("https://play.example.org", hash);
		CHECK(again->state() == MapCache::Download::State::Done);
		CHECK_EQ(http.exchanges.size(), 1);

		// Wrong bytes, HTTP errors and cancellation fail.
		const auto other = mapBytes('o', 1000);
		auto wrong = cache.fetch("https://play.example.org", Sha256::hex(other));
		http.pending("/api/v1/blobs/maps/" + Sha256::hex(other))->replyRaw(200, raw);
		CHECK(wrong->state() == MapCache::Download::State::Failed);
		CHECK_EQ(wrong->error(), "map does not match its hash");
		auto missing = cache.fetch("https://play.example.org", Sha256::hex(other));
		http.pending("/api/v1/blobs/maps/" + Sha256::hex(other))->replyRaw(404, "{}");
		CHECK(missing->state() == MapCache::Download::State::Failed);
		auto cancelled = cache.fetch("https://play.example.org", Sha256::hex(other));
		cancelled->cancel();
		CHECK(cancelled->state() == MapCache::Download::State::Failed);
		CHECK(http.exchanges.back()->cancelled);
		CHECK(cache.fetch("https://play.example.org", "bad")->state() ==
			  MapCache::Download::State::Failed);
	}
}
