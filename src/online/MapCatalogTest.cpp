// SPDX-License-Identifier: GPL-3.0-or-later
// Catalog queries and the share flow (create, upload the bytes, follow the
// server's validation and preview) against scripted HTTP; the room hand-off.

#include "Glob2Test.h"
#include "InstanceConfig.h"
#include "MapCatalog.h"
#include "OnlineFakes.h"
#include "OnlineHandoff.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"

using namespace Online;
using OnlineFakes::Json;

namespace
{
const std::string ORIGIN = "https://play.example.org";
const std::string MAP_ID = "2a1b0c9d-8e7f-4a6b-9c5d-3e2f1a0b9c8d";
const std::string HASH(64, 'a');

struct Fixture
{
	OnlineFakes::World world;
	MemoryStorage storage;
	InstanceConfig config{storage};
	PlatformClient client{config, ClientOptions{"test", "desktop"}, world.environment()};
	Fixture()
	{
		client.start(ORIGIN);
		world.http.pending("/api/v1/auth/guest")
			->reply(200, Json{{"account", OnlineFakes::account()},
							  {"tokens", OnlineFakes::tokens("r1", 1790000000, 600)},
							  {"deviceCredential", std::string(43, 'c')}});
		client.update();
	}
	Json version(const std::string &validation, const std::string &preview, const std::string &reason = {})
	{
		Json v = {{"hash", HASH},
				  {"size", 5},
				  {"validation", validation},
				  {"preview", preview},
				  {"downloadUrl", ORIGIN + "/api/v1/maps/" + MAP_ID + "/versions/" + HASH + "/file"},
				  {"createdAt", "2026-10-01T12:00:00Z"}};
		if (!reason.empty())
			v["reason"] = reason;
		if (validation == "valid")
		{
			v["width"] = 128;
			v["height"] = 128;
			v["teamCount"] = 4;
		}
		return v;
	}
};
} // namespace

TEST_SUITE("MapCatalog")
{
	TEST_CASE("list queries encode the filters the browser offers")
	{
		MapQuery query;
		CHECK(query.path() == "/api/v1/maps?sort=plays&limit=24");
		query.search = "two rivers & co";
		query.sort = "likes";
		query.teams = 4;
		query.maxSide = 128;
		query.cursor = "abc=";
		CHECK(query.path() ==
			  "/api/v1/maps?sort=likes&limit=24&q=two%20rivers%20%26%20co&teams=4&maxSide=128&cursor=abc%3D");
		MapQuery mine;
		mine.mine = true;
		mine.sort = "recent";
		CHECK(mine.path() == "/api/v1/maps?sort=recent&limit=24&owner=me");
		CHECK(std::string(DEFAULT_MAP_VISIBILITY) == "unlisted");
	}

	TEST_CASE("sharing creates the map, uploads the raw bytes and waits for validation and the preview")
	{
		Fixture f;
		MapShare::Details details;
		details.title = "Test archipelago";
		details.description = "Four islands.";
		MapShare share(f.client, {}, details, "bytes", "125-49-" + std::string(64, 'b'), [&f] { return f.world.wall; },
					   2000);
		CHECK(share.stage() == MapShare::Stage::Creating);
		auto create = f.world.http.pending("/api/v1/maps");
		REQUIRE(create);
		CHECK(create->body() == Json{{"title", "Test archipelago"},
									 {"description", "Four islands."},
									 {"visibility", "unlisted"},
									 {"madeWith", "hand"}});
		CHECK(create->header("Authorization").rfind("Bearer ", 0) == 0);
		create->reply(201, Json{{"id", MAP_ID}, {"title", "Test archipelago"}});
		f.client.update();
		CHECK(share.stage() == MapShare::Stage::Uploading);
		auto upload = f.world.http.pending("/versions?simVersion=125-49-" + std::string(64, 'b'));
		REQUIRE(upload);
		CHECK(upload->request.method == HttpFetch::Method::Post);
		CHECK(upload->request.body == "bytes");
		CHECK(upload->header("Content-Type") == "application/octet-stream");
		upload->response.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
		upload->reply(201, f.version("pending", "pending"));
		f.client.update();
		CHECK(share.stage() == MapShare::Stage::Checking);
		CHECK(share.mapId() == MAP_ID);

		// Polls every two seconds until valid with its preview.
		const std::string versionPath = "/api/v1/maps/" + MAP_ID + "/versions/" + HASH;
		auto poll = f.world.http.pending(versionPath);
		REQUIRE(poll);
		poll->reply(200, f.version("valid", "pending"));
		f.client.update();
		share.update();
		CHECK(share.stage() == MapShare::Stage::Checking);
		CHECK_FALSE(f.world.http.pending(versionPath));
		f.world.wall += 2000;
		share.update();
		poll = f.world.http.pending(versionPath);
		REQUIRE(poll);
		poll->reply(200, f.version("valid", "ready"));
		f.client.update();
		share.update();
		CHECK(share.stage() == MapShare::Stage::Ready);
		REQUIRE(share.version());
		CHECK(share.version()->teamCount == 4);
	}

	TEST_CASE("a map the server cannot load is rejected with its reason; a new version skips creation")
	{
		Fixture f;
		MapShare share(f.client, MAP_ID, {}, "bytes", "", [&f] { return f.world.wall; });
		CHECK(f.world.http.count("/api/v1/maps") == 0);
		auto upload = f.world.http.pending("/api/v1/maps/" + MAP_ID + "/versions");
		REQUIRE(upload);
		upload->reply(201, f.version("invalid", "failed", "colony 3 has no starting building"));
		f.client.update();
		share.update();
		CHECK(share.stage() == MapShare::Stage::Rejected);
		CHECK(share.version()->reason == "colony 3 has no starting building");
	}

	TEST_CASE("upload errors end the flow with the server's message")
	{
		Fixture f;
		MapShare share(f.client, {}, {"Title"}, "bytes", "", [&f] { return f.world.wall; });
		f.world.http.pending("/api/v1/maps")
			->reply(429, Json{{"code", "rate_limited"}, {"message", "Too many uploads; wait a while."}});
		f.client.update();
		CHECK(share.stage() == MapShare::Stage::Failed);
		CHECK(share.error().code == "rate_limited");
		CHECK(share.finished());
	}

	TEST_CASE("hand-offs wait for their handler")
	{
		CHECK_FALSE(takePendingMatch());
		MatchAssignment assignment;
		assignment.matchId = "m1";
		CHECK_FALSE(beginMatch(assignment));
		std::vector<std::string> got;
		setMatchHandler([&got](const MatchAssignment &a) { got.push_back(a.matchId); });
		CHECK(got == std::vector<std::string>{"m1"});
		assignment.matchId = "m2";
		CHECK(beginMatch(assignment));
		CHECK(got.size() == 2);
		setMatchHandler({});
		CHECK_FALSE(takePendingMatch());

		RoomMapChoice choice{MAP_ID, HASH, "Two Rivers"};
		CHECK_FALSE(useMapInRoom(choice));
		auto pending = takePendingRoomMap();
		REQUIRE(pending);
		CHECK(pending->hash == HASH);
		CHECK_FALSE(requestRematch({"m1", HASH, {}}));
	}
}
