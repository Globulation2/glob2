// SPDX-License-Identifier: GPL-3.0-or-later
// Drives the models behind the quick-match, profile and map screens against a
// real platform instance. Prints one JSON object per line. Manual live checks
// (docs/multiplayer/client.md, "Online screens"); not run by CI.
//
//   online-screens-probe <origin> <state directory> quickmatch <queue id> [--no-ai] [--sim <key>]
//   online-screens-probe <origin> <state directory> maps <map file> [--sim <key>]
//   online-screens-probe <origin> <state directory> profile [--sim <key>]
//
// quickmatch: probes the relays, joins the queue, follows the search and
// accepts a ranked prompt, until the match is handed off (or 240 s pass).
// maps: lists the catalog, reads a map's detail and preview, uploads the file
// as an unlisted map and follows validation, then downloads it by hash.
// profile: reads the account and its match history and prints the summary.
#include "InstanceConfig.h"
#include "MapCatalog.h"
#include "NetTransport.h"
#include "OnlineResources.h"
#include "OnlineStorage.h"
#include "PlatformClient.h"
#include "QuickMatch.h"
#include "RelayProbe.h"
#include "Sha256.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

using namespace Online;
namespace fs = std::filesystem;

namespace
{
class DirectoryStorage final : public OnlineStorage
{
	fs::path root;

  public:
	explicit DirectoryStorage(fs::path root) : root(std::move(root)) {}
	bool read(const std::string &path, std::string &contents) override
	{
		std::ifstream file(root / path, std::ios::binary);
		if (!file)
			return false;
		std::ostringstream buffer;
		buffer << file.rdbuf();
		contents = buffer.str();
		return true;
	}
	bool write(const std::string &path, const std::string &contents) override
	{
		const auto target = root / path;
		fs::create_directories(target.parent_path());
		std::ofstream file(target, std::ios::binary | std::ios::trunc);
		return bool(file << contents);
	}
	void remove(const std::string &path) override
	{
		std::error_code ignored;
		fs::remove(root / path, ignored);
	}
	std::vector<std::string> list(const std::string &directory) override
	{
		std::vector<std::string> names;
		std::error_code ignored;
		for (const auto &entry : fs::directory_iterator(root / directory, ignored))
			names.push_back(entry.path().filename().string());
		return names;
	}
};

std::int64_t steadyMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
std::int64_t wallMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
		.count();
}
void say(Json line)
{
	line["t"] = wallMs();
	std::cout << line.dump() << std::endl;
}

struct Probe
{
	DirectoryStorage storage;
	InstanceConfig config{storage};
	ClientOptions options;
	std::unique_ptr<PlatformClient> client;
	std::string origin;

	Probe(const std::string &origin, const fs::path &state, std::optional<SimVersion> sim)
		: storage(state), origin(origin)
	{
		config.load();
		options.clientVersion = "online-screens-probe";
		if (sim)
			options.simVersion = *sim;
		ClientEnvironment env;
		env.makeTransport = [] { return makeNetTransport({}, NetMessageMode::Text); };
		env.startFetch = [](HttpFetch::Request request) { return HttpFetch::start(std::move(request)); };
		env.now = steadyMs;
		env.wallClock = wallMs;
		auto generator = std::make_shared<std::mt19937_64>(std::random_device{}());
		env.random = [generator] { return std::uniform_real_distribution<double>(0, 1)(*generator); };
		env.openUrl = [](const std::string &) { return false; };
		client = std::make_unique<PlatformClient>(config, options, env);
	}
	template <class Done> bool until(Done done, std::int64_t timeoutMs, std::function<void()> tick = {})
	{
		const auto deadline = steadyMs() + timeoutMs;
		while (!done())
		{
			if (steadyMs() > deadline)
				return false;
			client->update();
			if (tick)
				tick();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return true;
	}
	bool online()
	{
		client->start(origin);
		const bool ok = until([this] { return client->connection() == PlatformClient::Connection::Online; }, 30000);
		say({{"step", "online"},
			 {"ok", ok},
			 {"account", client->account() ? client->account()->raw : Json()},
			 {"simSupported", client->simSupported()},
			 {"error", client->lastError()}});
		return ok;
	}
	PlatformClient::Response call(HttpFetch::Method method, const std::string &path, Json body = Json())
	{
		std::optional<PlatformClient::Response> got;
		client->rest(method, path, std::move(body), [&got](const PlatformClient::Response &r) { got = r; });
		until([&got] { return got.has_value(); }, 30000);
		return got.value_or(PlatformClient::Response{false, Json(), {"timeout", "no answer", Json()}});
	}
	PlatformClient::Response raw(HttpFetch::Method method, const std::string &path, std::string body = {},
								 const std::string &type = {})
	{
		std::optional<PlatformClient::Response> got;
		client->restRaw(method, path, std::move(body), type, [&got](const PlatformClient::Response &r) { got = r; });
		until([&got] { return got.has_value(); }, 60000);
		return got.value_or(PlatformClient::Response{false, Json(), {"timeout", "no answer", Json()}});
	}
};

int quickmatch(Probe &probe, const std::string &queueId, bool allowAi)
{
	if (!probe.online())
		return 1;
	auto instance = probe.call(HttpFetch::Method::Get, "/api/v1/instance");
	std::optional<QueueInfo> queue;
	for (const auto &q : queuesFromInstance(instance.result))
		if (q.id == queueId)
			queue = q;
	if (!queue)
	{
		say({{"step", "queue"}, {"ok", false}, {"queues", instance.result.value("queues", Json())}});
		return 1;
	}
	QuickMatch::Environment env;
	env.probe = [](const std::string &origin)
	{
		return std::make_unique<RelayProbe>(
			origin, [](HttpFetch::Request r) { return HttpFetch::start(std::move(r)); }, steadyMs);
	};
	env.wallClock = wallMs;
	env.attention = [] { say({{"step", "attention"}}); };
	std::optional<MatchAssignment> handed;
	env.handoff = [&handed](const MatchAssignment &a) { handed = a; };
	QuickMatch quick(*probe.client, env);
	std::uint64_t seen = 0;
	QuickMatch::Phase last = QuickMatch::Phase::Idle;
	quick.search(*queue, allowAi);
	const bool done = probe.until(
		[&] { return handed.has_value() || quick.phase() == QuickMatch::Phase::Failed; }, 240000,
		[&]
		{
			quick.update();
			if (quick.revision() == seen)
				return;
			seen = quick.revision();
			Json line = {{"step", "quickmatch"}, {"phase", phaseName(quick.phase())}, {"waited", quick.waitedSeconds()}};
			if (quick.phase() != last && quick.phase() == QuickMatch::Phase::Joining)
			{
				Json regions = Json::array();
				for (const auto &r : quick.regions())
					regions.push_back(r.toJson());
				line["regions"] = regions;
			}
			if (const auto &s = quick.status())
				line["status"] = {{"waitedSeconds", s->waitedSeconds},
								  {"ratingMin", s->ratingMin ? Json(*s->ratingMin) : Json()},
								  {"ratingMax", s->ratingMax ? Json(*s->ratingMax) : Json()},
								  {"region", s->region},
								  {"backfillAi", s->backfillAi},
								  {"backfillInMs", quick.backfillInMs() ? Json(*quick.backfillInMs()) : Json()}};
			if (const auto &p = quick.proposal())
			{
				Json seats = Json::array();
				for (const auto &seat : p->seats)
					seats.push_back({{"name", seat.displayName}, {"ai", seat.ai}, {"rating", seat.rating ? Json(*seat.rating) : Json()},
									 {"response", seat.response}, {"you", seat.you}});
				line["proposal"] = {{"requiresAccept", p->requiresAccept}, {"humans", p->humans}, {"ais", p->ais},
									{"generator", p->generatorId}, {"seats", seats}};
			}
			if (quick.phase() == QuickMatch::Phase::Proposed && !quick.answer())
				quick.respond(true);
			if (quick.phase() == QuickMatch::Phase::Failed)
				line["error"] = {{"code", quick.error().code}, {"message", quick.error().message}};
			last = quick.phase();
			say(line);
		});
	if (handed)
		say({{"step", "handoff"},
			 {"ok", true},
			 {"matchId", handed->matchId},
			 {"seat", handed->seat},
			 {"relayUrl", handed->relayUrl},
			 {"mapUrl", handed->mapUrl},
			 {"mapHash", handed->mapHash()},
			 {"seats", handed->setup.value("seats", Json())}});
	else
		say({{"step", "handoff"}, {"ok", false}, {"phase", phaseName(quick.phase())}, {"timedOut", !done}});
	return handed ? 0 : 1;
}

int maps(Probe &probe, const std::string &file, const std::string &simKey)
{
	if (!probe.online())
		return 1;
	MapQuery query;
	auto list = probe.call(HttpFetch::Method::Get, query.path());
	auto page = parseMapList(list.result);
	say({{"step", "list"}, {"ok", list.ok}, {"count", page.items.size()}, {"error", list.error.message}});
	for (const auto &map : page.items)
	{
		auto detail = probe.call(HttpFetch::Method::Get, "/api/v1/maps/" + map.id);
		auto parsed = MapDetail::fromJson(detail.result);
		Json line = {{"step", "detail"}, {"ok", bool(parsed)}, {"title", map.title}, {"versions", parsed ? parsed->versions.size() : 0}};
		if (map.latestVersion && !map.latestVersion->previewUrl.empty())
		{
			auto png = probe.raw(HttpFetch::Method::Get, map.latestVersion->previewUrl);
			line["preview"] = {{"ok", png.ok}, {"bytes", png.body.size()}, {"png", png.body.rfind("\x89PNG", 0) == 0},
							   {"contentType", png.contentType}};
		}
		say(line);
		break;
	}
	std::ifstream in(file, std::ios::binary);
	std::ostringstream buffer;
	buffer << in.rdbuf();
	std::string bytes = buffer.str();
	if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0x1f && (unsigned char)bytes[1] == 0x8b)
	{
		say({{"step", "upload"}, {"ok", false}, {"error", "give an uncompressed map file"}});
		return 1;
	}
	MapShare::Details details;
	details.title = "Probe upload " + std::to_string(wallMs() / 1000);
	details.description = "Uploaded by online-screens-probe.";
	MapShare share(*probe.client, {}, details, bytes, simKey, wallMs, 2000);
	int lastStage = -1;
	probe.until([&] { return share.finished(); }, 300000,
				[&]
				{
					share.update();
					if (int(share.stage()) != lastStage)
					{
						lastStage = int(share.stage());
						say({{"step", "share"}, {"stage", lastStage}, {"mapId", share.mapId()}});
					}
				});
	const auto &version = share.version();
	say({{"step", "shared"},
		 {"ok", share.stage() == MapShare::Stage::Ready},
		 {"stage", int(share.stage())},
		 {"mapId", share.mapId()},
		 {"validation", version ? version->validation : ""},
		 {"preview", version ? version->preview : ""},
		 {"reason", version ? version->reason : ""},
		 {"error", share.error().message}});
	if (!version || share.stage() != MapShare::Stage::Ready)
		return 1;
	auto mine = probe.call(HttpFetch::Method::Get, [] { MapQuery q; q.mine = true; q.sort = "recent"; return q.path(); }());
	say({{"step", "mine"}, {"ok", mine.ok}, {"count", parseMapList(mine.result).items.size()}});
	if (!version->previewUrl.empty())
	{
		auto png = probe.raw(HttpFetch::Method::Get, version->previewUrl);
		say({{"step", "preview"}, {"ok", png.ok}, {"bytes", png.body.size()}, {"png", png.body.rfind("\x89PNG", 0) == 0}});
	}
	auto blob = probe.raw(HttpFetch::Method::Get, "/api/v1/blobs/maps/" + version->hash);
	say({{"step", "download"}, {"ok", blob.ok}, {"bytes", blob.body.size()}, {"error", blob.error.message}});
	return 0;
}

int profile(Probe &probe)
{
	if (!probe.online() || !probe.client->account())
		return 1;
	const std::string id = probe.client->account()->id;
	auto account = probe.call(HttpFetch::Method::Get, "/api/v1/accounts/" + id);
	say({{"step", "account"}, {"ok", account.ok}, {"displayName", account.result.value("displayName", "")}});
	auto history = probe.call(HttpFetch::Method::Get, "/api/v1/accounts/" + id + "/matches?limit=50");
	say({{"step", "history"}, {"ok", history.ok}, {"error", history.error.code}, {"message", history.error.message}});
	if (!history.ok)
		return 1;
	auto page = parseMatchList(history.result);
	auto summary = summarizeProfile(id, page.items);
	Json ladders = Json::array();
	for (const auto &l : summary.ladders)
		ladders.push_back({{"ladder", l.ladder}, {"rating", l.rating}, {"provisional", l.provisional}, {"games", l.games}});
	say({{"step", "summary"}, {"matches", page.items.size()}, {"ladders", ladders}, {"wins", summary.wins},
		 {"losses", summary.losses}, {"medianMinutes", summary.medianMinutes ? Json(*summary.medianMinutes) : Json()}});
	return 0;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		std::cerr << "usage: online-screens-probe <origin> <state directory> quickmatch <queue>|maps <file>|profile [--no-ai] [--sim <key>]\n";
		return 2;
	}
	std::optional<SimVersion> sim;
	bool allowAi = true;
	std::vector<std::string> rest;
	for (int i = 4; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--sim" && i + 1 < argc)
		{
			SimVersion parsed;
			if (!SimVersion::parseKey(argv[++i], parsed))
				return 2;
			sim = parsed;
		}
		else if (arg == "--no-ai")
			allowAi = false;
		else
			rest.push_back(arg);
	}
	Probe probe(argv[1], argv[2], sim);
	const std::string scenario = argv[3];
	if (scenario == "quickmatch" && !rest.empty())
		return quickmatch(probe, rest[0], allowAi);
	if (scenario == "maps" && !rest.empty())
		return maps(probe, rest[0], sim ? sim->key() : std::string());
	if (scenario == "profile")
		return profile(probe);
	return 2;
}
