// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocalCommander.h"
#include "UnitConsts.h"
#include <deque>
#include <set>
#include <optional>
#include <cctype>
#include <algorithm>
#include "Building.h"
#include "Material.h"
#include "Map.h"
#include <map>
#include <cmath>
#include "Team.h"
#include "HiveWorker.h"
#include <hive/PlayerAPIContract.h>
#include "WorkerProcess.h"
#include "ProgramSchedule.h"
#include "HiveObservation.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "Order.h"
#include "scripting/javascript/ScriptOrders.h"
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <limits>
#include "Game.h"
#include "render/scene/Scene.h"
#include "GameStats.h"
#include "EngineTiming.h"
#include "stats/MetricCatalog.h"
namespace Hive
{
namespace
{
LocalCommander *active = nullptr; // main thread only
std::atomic<std::uint64_t> sessions{0};
constexpr std::size_t JournalCapacity = 256;
constexpr unsigned AlertTicks = 1500; // how long game_world keeps reporting an alert
// The HUD event feed's rows, by GameEventType.
const char *const eventKinds[GESize] = {"unit_attacked", "unit_converted_away", "unit_converted_to_you",
										"building_attacked", "building_completed"};
} // namespace
Json playerAPI()
{
	return {
		{"entrypoint", "function step(ctx) { return {output: {}, orders: []}; }"},
		{"contract", PlayerAPIContract},
		{"local_control_contract",
		 "Acquire an MCP control token and get the current session from hive_status. "
		 "hive_run accepts token, session, operation_key and source for one invocation; globals "
		 "do not persist between one-shot calls. Poll hive_result with session and operation_key. "
		 "hive_program action is list, inspect, install, replace, pause, resume or cancel. "
		 "inspect requires name; mutations require token, session, operation_key and name. "
		 "install/replace additionally require source and accept description and interval_ticks "
		 "(default/minimum 25). replace requires expected_revision from inspect. Programs use "
		 "names, with server-assigned monotonic revisions; there are no UUIDs, caller-assigned "
		 "revisions or migration source. Install/replacement preflight validates without effects. "
		 "Replacement resets globals and retains pause state; failure preserves the old program. "
		 "Use a unique operation_key per mutation; repeating identical arguments returns the same "
		 "operation result without re-execution. Queued means admission, not gameplay success. "
		 "Standing-order globals persist across successful invocations only. Programs continue "
		 "after MCP disconnect or lease expiry, but programs/globals are discarded on load/exit "
		 "and are never saved. wakeAgent records local_events only: no authentication, billing, "
		 "supervision, model access or paid work. Reads are fixed to the current human player's "
		 "team. tools/list supplies the authoritative argument schemas."},
		{"declarations", "examples/hive-mind/hive.d.ts"},
		{"limits",
		 {{"work_units_per_run", 250000000},
		  {"heap_bytes", 128 * 1024 * 1024},
		  {"wall_seconds_per_run", 5},
		  {"source_bytes", 131072},
		  {"programs", 8},
		  {"minimum_interval_ticks", 25},
		  {"orders_per_invocation", 32},
		  {"submission_capacity", 64},
		  {"journal_entries", JournalCapacity}}}};
}
namespace
{
// The local Hive helpers, beside the shared player contract (hive_api section "helpers").
constexpr const char *HelpersDoc = R"(Local colony scripts (Hive) have these helpers beside the shared API.
They read only the colony's own snapshot: what it sees or has seen, and its own events.

Queries
- ctx.game.units(filter) / ctx.game.buildings(filter): filter.team is a team number or
  "mine", "enemies" or "allies"; filter.type is a number or a name: "worker", "explorer",
  "warrior" for units, a building family name ("inn", "swarm", "warflag") for buildings.
  Your own buildings and flags also report assigned: units that have taken the job (workers
  is what it requests).
- ctx.game.buildingType(name, {level, site}): the type record a player places for a name
  (level 0's construction site, or the flag), or null.
- ctx.game.events(sinceTick): your colony's events after sinceTick, oldest first:
  {kind, event, x, y, count, tick}. Kinds: unit_attacked, building_attacked,
  building_completed, unit_converted_away, unit_converted_to_you, building_destroyed,
  building_demolished, enemy_building_sighted, hive_program_failed. Keep the last tick in a
  global and react to what is new instead of scanning for enemies every run.

Map
- ctx.game.map.delta(a, b) -> {dx, dy} and ctx.game.map.distance(a, b) -> number: the
  shortest offset and straight-line distance on the wrapping map, for {x, y} records or
  (ax, ay, bx, by). The map wraps at every edge: plain subtraction is wrong near them.
  Available to every script, AI and map scripts included.
- ctx.game.map.scan(x, y, width, height, fields): flat row-major arrays (index
  dy * width + dx) for only the fields you name, much cheaper than region() records.
  Element i is tile ((x + i % width) % map.width, (y + floor(i / width)) % map.height).
  Fields: explored (0/1), visible (0/1), resource (map-local type, 65535 none), amount,
  terrain (the terrain id when all four corners agree, else -1), buildable (0/1, as far as
  the colony knows) and building (occupant id, 65535 none). Default ["resource","buildable"].
- ctx.game.map.canPlace(name, x, y) -> {ok, reason}: whether the colony can see room for
  that building's footprint with its top-left at x, y (explored, buildable terrain on every
  corner, no resource or known building), or a flag on that tile.
- ctx.game.map.findPlacement(name, {x, y, radius}) -> {x, y} or null: the nearest such
  top-left, searching rings out to radius (default 12, at most 64), as game_place near does.
  The game still decides when the order arrives.
- Tile records' resource also has key ("wheat", "rice", "fish") from resourceTypes().

Orders
- {type: "create", building: "inn", x, y}: what a click with that palette entry creates,
  with its default requested units (workers, futureWorkers) and a flag's default range;
  pass any of them to override. buildingType ids still work.

Cost
Each run has 250 million work units, a 128 MiB heap and a 5 s deadline. Reading an
explored tile record costs about 1,500 units; scan() returns compact numbers instead. A
for...of loop costs about three times an index loop (for (let i = 0; i < a.length; i++)).)";
struct DocSection
{
	std::string id, title, text;
};
std::string slug(const std::string &title)
{
	std::string id;
	for (const unsigned char c : title)
		if (std::isalnum(c))
			id += char(std::tolower(c));
		else if (!id.empty() && id.back() != '-')
			id += '-';
	while (!id.empty() && id.back() == '-')
		id.pop_back();
	return id;
}
// The Hive reference as sections: the shared player contract split at its ## and ###
// headings, then the local helpers, control rules and limits.
const std::vector<DocSection> &docSections()
{
	static const std::vector<DocSection> sections = []
	{
		std::vector<DocSection> out;
		std::string contract = PlayerAPIContract, line;
		DocSection current{"contract", "Contract", {}};
		std::size_t start = 0;
		while (start <= contract.size())
		{
			const auto end = contract.find('\n', start);
			line = contract.substr(start, end == std::string::npos ? std::string::npos : end - start);
			start = end == std::string::npos ? contract.size() + 1 : end + 1;
			if (line.rfind("## ", 0) == 0 || line.rfind("### ", 0) == 0)
			{
				if (!current.text.empty())
					out.push_back(current);
				const auto title = line.substr(line.find(' ') + 1);
				current = {slug(title), title, {}};
				continue;
			}
			current.text += line + "\n";
		}
		if (!current.text.empty())
			out.push_back(current);
		out.push_back({"helpers", "Colony helpers (local Hive)", HelpersDoc});
		out.push_back({"local-control", "Local control: hive_run, hive_result, hive_program",
					   playerAPI().at("local_control_contract").get<std::string>()});
		out.push_back({"limits", "Limits", playerAPI().at("limits").dump(1)});
		return out;
	}();
	return sections;
}
std::string lower(std::string text)
{
	for (auto &c : text)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return text;
}
} // namespace
// hive_api: a reference to browse (no arguments: the table of contents; section: one
// section) or search (search: words that must all appear), never one large blob.
Json apiDocs(const Json &args)
{
	const auto &sections = docSections();
	if (args.contains("section"))
	{
		const auto wanted = args.at("section").get<std::string>();
		for (std::size_t i = 0; i < sections.size(); ++i)
			if (sections[i].id == wanted || lower(sections[i].title) == lower(wanted))
			{
				Json result = {{"section", sections[i].id}, {"title", sections[i].title}, {"text", sections[i].text}};
				if (i > 0)
					result["previous"] = sections[i - 1].id;
				if (i + 1 < sections.size())
					result["next"] = sections[i + 1].id;
				return result;
			}
		std::string ids;
		for (const auto &section : sections)
			ids += (ids.empty() ? "" : ", ") + section.id;
		throw std::runtime_error("Unknown section \"" + wanted + "\"; sections: " + ids);
	}
	if (args.contains("search"))
	{
		std::vector<std::string> words;
		{
			const auto query = lower(args.at("search").get<std::string>());
			std::size_t i = 0;
			while (i < query.size())
			{
				const auto j = query.find(' ', i);
				if (j != i)
					words.push_back(query.substr(i, j == std::string::npos ? std::string::npos : j - i));
				if (j == std::string::npos)
					break;
				i = j + 1;
			}
		}
		if (words.empty())
			throw std::runtime_error("search needs at least one word");
		Json results = Json::array();
		for (const auto &section : sections)
		{
			// Paragraphs (blank-line separated) are the unit of a hit.
			std::size_t start = 0;
			const auto &text = section.text;
			while (start < text.size() && results.size() < 12)
			{
				auto end = text.find("\n\n", start);
				if (end == std::string::npos)
					end = text.size();
				const auto paragraph = text.substr(start, end - start);
				const auto folded = lower(section.title + " " + paragraph);
				bool all = true;
				for (const auto &w : words)
					all = all && folded.find(w) != std::string::npos;
				if (all && !paragraph.empty())
					results.push_back({{"section", section.id}, {"title", section.title},
									   {"text", paragraph.size() > 700 ? paragraph.substr(0, 700) + "..." : paragraph}});
				start = end + 2;
			}
		}
		return {{"search", args.at("search")}, {"results", results},
				{"note", "Read a whole section with hive_api section."}};
	}
	Json contents = Json::array();
	for (const auto &section : sections)
	{
		auto first = section.text.substr(0, section.text.find_first_of(".\n", 1) + 1);
		while (!first.empty() && (first.front() == '\n' || first.front() == ' '))
			first.erase(0, 1);
		contents.push_back({{"section", section.id}, {"title", section.title},
							{"summary", first.size() > 140 ? first.substr(0, 140) + "..." : first},
							{"bytes", section.text.size()}});
	}
	return {{"sections", contents},
			{"note", "Browse with hive_api section (one section, with previous/next) or search (words "
					 "that must all appear). Start with contract, helpers and local-control. Every "
					 "script declares step(ctx) and returns {output, orders}. Types: "
					 "examples/hive-mind/hive.d.ts in the repository."}};
}
LocalCommander::LocalCommander(GameGUI &g, std::function<std::future<Json>(Json)> start)
	: gui(g), observations(g.game, g.localTeamNo), player(g.localPlayer), team(g.localTeamNo),
	  session(std::to_string(SDL_GetTicksNS()) + "-" + std::to_string(++sessions)),
	  launch(std::move(start))
{
	active = this;
}
LocalCommander::~LocalCommander()
{
	clear();
	if (active == this)
		active = nullptr;
}
bool LocalCommander::contextCurrent() const
{
	if (!valid || gui.networkMatch.active || globalContainer->replaying ||
		globalContainer->liveSpectating || gui.localPlayer != player || gui.localTeamNo != team)
		return false;
	if (!gui.simulationThreaded)
		return eligible();
	// Player records are simulation-owned. Inspect their published copy while
	// idle or waiting for the worker rather than parking the simulation each frame.
	const auto &published = gui.drawnScene().world.session;
	return !published ||
		   (player >= 0 && std::size_t(player) < published->players.size() &&
			published->players[player].type == BasePlayer::P_LOCAL &&
			published->players[player].teamNumber == team);
}
bool LocalCommander::eligible() const
{
	return valid && !gui.networkMatch.active && !gui.game.gameHeader.hasNetworkPlayer() && !globalContainer->replaying &&
		   !globalContainer->liveSpectating && gui.localPlayer == player &&
		   gui.localTeamNo == team && player >= 0 &&
		   player < gui.game.gameHeader.getNumberOfPlayers() && gui.game.players[player] &&
		   gui.game.players[player]->type == BasePlayer::P_LOCAL && !gui.game.players[player]->ai;
}
void LocalCommander::boundary(const std::function<void()> &work)
{
	if (!gui.parkForClient(work))
		work();
}
void LocalCommander::clear()
{
	valid = false;
	programs.clear();
	revisions.clear();
	queued.clear();
	journal.clear();
	requests.clear();
	++generation;
}
Json LocalCommander::status() const
{
	return {{"available", valid},
			{"session", session},
			{"tick", tick},
			{"running", worker.valid()},
			{"queued", queued.size()},
			{"programs", programs.size()},
			{"limits", playerAPI().at("limits")}};
}
void LocalCommander::remember(const std::string &key, Json result)
{
	if (key.empty())
		return;
	result["operation_key"] = key;
	result["session"] = session;
	journal[key] = std::move(result);
}
Json LocalCommander::standingOrders() const
{
	Json result = Json::array();
	for (const auto &[name, p] : programs)
	{
		Json record = {{"name", p.definition.at("name")},
					   {"description", p.definition.at("description")},
					   {"revision", p.definition.at("revision")},
					   {"intervalTicks", p.definition.at("intervalTicks")}};
		record["id"] = name;
		record["paused"] = p.paused;
		record["nextTick"] = p.nextTick;
		record["recent"] = p.recent;
		result.push_back(std::move(record));
	}
	return result;
}
void LocalCommander::change(const std::string &id, const std::string &action)
{
	auto it = programs.find(id);
	if (it == programs.end())
		return;
	if (action == "remove" || action == "cancel")
		programs.erase(it);
	else
	{
		it->second.paused = action == "pause";
		it->second.nextTick = tick + 25;
		it->second.generation = ++generation;
	}
}
void LocalCommander::stop()
{
	for (auto &[name, p] : programs)
	{
		p.paused = true;
		p.generation = ++generation;
	}
	for (const auto &op : queued)
		remember(op.key, {{"status", "cancelled"}, {"error", "Stopped in game"}});
	queued.clear();
	if (!current.key.empty())
		remember(current.key, {{"status", "cancelled"}, {"error", "Stopped in game"}});
	current.key.clear();
	++generation;
	currentProgram.clear();
}
Json LocalCommander::call(const std::string &tool, const Json &args)
{
	// A pending wait is polled every frame: answer from the published scene and HUD
	// without parking the simulation, until it is done.
	if (args.contains("_poll"))
	{
		if (!contextCurrent())
			return matchOver(tool);
		return tool == "game_wait" ? wait(args) : awaitResult(args);
	}
	bool allowed = false;
	boundary(
		[&]
		{
			allowed = eligible();
			tick = gui.game.stepCounter;
		});
	if (!allowed)
	{
		clear();
		if (tool == "hive_status")
			return {
				{"available", false},
				{"reason", "Local Hive requires the current human player in an active local match"},
				{"limits", playerAPI().at("limits")}};
		throw std::runtime_error(
			"Local Hive requires the current human player in an active local match");
	}
	if (tool == "hive_status")
		return status();
	if (tool == "game_world")
		return world(args);
	if (tool == "game_place")
		return place(args);
	if (tool == "game_view")
		return view(args);
	if (tool == "game_chat")
		return chat(args);
	if (tool == "game_time")
		return time(args);
	if (tool == "game_building")
		return building(args);
	if (tool == "game_zone")
		return zone(args);
	if (tool == "game_stats")
		return stats(args);
	if (tool == "game_wait")
		return wait(args);
	if (tool == "game_alliance")
		return alliance(args);
	if (tool == "hive_result")
	{
		if (args.at("session") != session)
			throw std::runtime_error("Match session expired");
		auto it = journal.find(args.at("operation_key").get<std::string>());
		if (it == journal.end())
			throw std::runtime_error("Unknown operation key in this session");
		if (args.value("wait", false))
			return awaitResult(args);
		return it->second;
	}
	const auto kind =
		tool == "hive_run" ? std::string("run") : args.at("action").get<std::string>();
	// Reads may omit the session, but a stale one must not silently describe
	// the programs of a later match.
	if (args.contains("session") && args.at("session") != session)
		throw std::runtime_error("Match session expired; inspect hive_status");
	if (kind == "list")
		return {{"session", session}, {"programs", standingOrders()}};
	if (kind == "inspect")
	{
		auto it = programs.find(args.at("name").get<std::string>());
		if (it == programs.end())
			throw std::runtime_error("Unknown standing order");
		return {{"session", session},
				{"program", it->second.definition},
				{"paused", it->second.paused},
				{"recent", it->second.recent}};
	}
	if (args.at("session") != session)
		throw std::runtime_error("Match session expired; inspect hive_status");
	const auto key = args.at("operation_key").get<std::string>();
	if (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos)
		throw std::runtime_error("Provide a nonempty operation key of at most 128 bytes");
	if (auto it = requests.find(key); it != requests.end())
	{
		// Tokens may change after reacquiring input control; they are not an effect.
		auto normalized = args;
		normalized.erase("token");
		if (it->second != normalized)
			throw std::runtime_error("Operation key already used with different arguments");
		return journal.at(key);
	}
	if (requests.size() >= JournalCapacity)
		throw std::runtime_error("Match result journal full; start a new match session");
	if (queued.size() >= 16)
		throw std::runtime_error("Local Hive operation queue full");
	if (tool == "hive_program")
	{
		const auto name = args.at("name").get<std::string>();
		if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos)
			throw std::runtime_error("Program name must contain 1..128 UTF-8 bytes without NUL");
		if (kind == "install" || kind == "replace")
		{
			const auto interval = args.value("interval_ticks", 25);
			if (interval < 25 || interval > 1000000)
				throw std::runtime_error("Program cadence must be 25..1000000 ticks");
		}
	}
	if (kind == "run" || kind == "install" || kind == "replace")
	{
		const auto source = args.at("source").get<std::string>();
		if (source.size() > 131072 || source.find('\0') != std::string::npos)
			throw std::runtime_error("Source exceeds 128 KiB or contains NUL");
	}
	auto normalized = args;
	normalized.erase("token");
	requests[key] = std::move(normalized);
	remember(key, {{"status", "queued"}, {"submitted", false}});
	// Pause/cancel must take effect immediately, including during execution.
	if (kind == "pause" || kind == "resume" || kind == "cancel")
	{
		const auto name = args.at("name").get<std::string>();
		if (!programs.contains(name))
			remember(key, {{"status", "failed"}, {"error", "Unknown standing order"}});
		else
		{
			change(name, kind);
			remember(key, {{"status", "completed"}, {"submitted", false}});
		}
	}
	else
		queued.push_back({key, args});
	if (args.value("wait", false))
		return awaitResult({{"session", session}, {"operation_key", key}});
	return journal.at(key);
}
Json LocalCommander::placeByName(Json order)
{
	// {type: "create", building: "inn", x, y}: what a click with that palette entry
	// creates, with its default requested units and a flag's default range, unless given.
	if (!order.is_object() || order.value("type", std::string()) != "create" || !order.contains("building") ||
		!order.at("building").is_string())
		return order;
	const auto made = gui.createDefaultsFromAutomation(order.at("building").get<std::string>());
	if (!made.error.empty())
		throw std::runtime_error(made.error);
	order.erase("building");
	order["buildingType"] = made.typeNum;
	if (!order.contains("workers"))
		order["workers"] = made.workers;
	if (!order.contains("futureWorkers"))
		order["futureWorkers"] = made.futureWorkers;
	if (!order.contains("range") && made.range >= 0)
		order["range"] = made.range;
	return order;
}
void LocalCommander::annotate(Json &snapshot)
{
	// Callers hold the simulation boundary. Only the colony's own information is added.
	// Units that took each own building's or flag's job, beside the units it requests.
	for (auto &b : snapshot.at("buildings"))
		if (b.value("team", -1) == team)
			if (const auto *building = gui.game.teams[team]->myBuildings[Building::GIDtoID(b.value("id", 0))])
				b["assigned"] = int(building->unitsWorking.size());
	// ctx.game.events(): what game_wait reports, with absolute ticks.
	Json events = Json::array();
	for (const auto &row : gui.gameEvents().rows())
		events.push_back({{"kind", row.type < GESize ? eventKinds[row.type] : "event"},
						  {"event", row.label}, {"x", row.x}, {"y", row.y}, {"count", row.count},
						  {"tick", row.lastTick}});
	for (const auto &loss : losses)
		events.push_back({{"kind", loss.demolished ? "building_demolished" : "building_destroyed"},
						  {"event", (loss.demolished ? "Building demolished: " : "Building destroyed: ") + loss.type},
						  {"x", loss.x}, {"y", loss.y}, {"count", 1}, {"tick", loss.tick}});
	for (const auto &seen : sightings)
		events.push_back({{"kind", "enemy_building_sighted"}, {"event", "Building sighted: " + seen.type},
						  {"x", seen.x}, {"y", seen.y}, {"team", seen.team}, {"count", 1}, {"tick", seen.tick}});
	for (const auto &failure : failures)
		events.push_back({{"kind", "hive_program_failed"}, {"event", failure.program + ": " + failure.error},
						  {"program", failure.program}, {"paused", failure.paused}, {"count", 1}, {"tick", failure.tick}});
	std::sort(events.begin(), events.end(),
			  [](const Json &a, const Json &b) { return a.value("tick", 0u) < b.value("tick", 0u); });
	snapshot["events"] = std::move(events);
	// Losses and sightings need the game_world memory; scripts read events, so start it.
	memoryTracked = true;
}
Json LocalCommander::awaitResult(const Json &args)
{
	const auto &ref = args.contains("_poll") ? args.at("_poll") : args;
	if (ref.value("session", std::string()) != session)
		throw std::runtime_error("Match session expired; inspect hive_status");
	const auto key = ref.value("operation_key", std::string());
	const auto it = journal.find(key);
	if (it == journal.end())
		throw std::runtime_error("Unknown operation key in this session");
	const auto status = it->second.value("status", std::string());
	const bool done = status != "queued" && status != "running";
	if (done || args.value("_final", false))
		return it->second;
	return {{"pending", true}, {"poll", {{"session", session}, {"operation_key", key}}}};
}
void LocalCommander::finish(Json result)
{
	const auto name = currentProgram;
	const auto kind = current.request.value("action", std::string("run"));
	bool success = false, submitted = false;
	std::string error;
	auto program = programs.find(name);
	if (!valid || (current.key.empty() && name.empty()))
		return;
	// Pause, resume and cancel deliberately retire an in-flight invocation.
	// Discard it quietly; it is not a worker failure in the replacement card.
	if (!name.empty() && (program == programs.end() || program->second.paused ||
						  program->second.generation != currentGeneration))
		return;
	try
	{
		// Worker output is an IPC boundary, including injected workers in tests.
		// Validate every typed field before publishing orders or program state.
		if (!result.is_object())
			throw std::runtime_error("Malformed Hive worker response: expected an object");
		if (result.contains("ok") && !result.at("ok").is_boolean())
			throw std::runtime_error("Malformed Hive worker response: ok must be boolean");
		for (const char *field : {"error", "diagnostic"})
			if (result.contains(field) && !result.at(field).is_string())
				throw std::runtime_error(std::string("Malformed Hive worker response: ") + field + " must be a string");
		for (const char *field : {"orders", "wakes"})
			if (result.contains(field) && !result.at(field).is_array())
				throw std::runtime_error(std::string("Malformed Hive worker response: ") + field + " must be an array");
		if (result.contains("installedInitialized") && !result.at("installedInitialized").is_boolean())
			throw std::runtime_error("Malformed Hive worker response: installedInitialized must be boolean");
		if (result.contains("random") && (!result.at("random").is_number_integer() ||
			result.at("random") < 0 || result.at("random") > std::numeric_limits<unsigned>::max()))
			throw std::runtime_error("Malformed Hive worker response: random must be an unsigned integer");
		const auto nextRandom = result.value("random", 1u);
		const bool installedInitialized = result.value("installedInitialized", false);
		success = result.value("ok", false);
		error = result.value("error", result.value("diagnostic", std::string()));
		// The budget is the sum of every host read in one run; say what fits.
		if (error.rfind("JavaScript work budget exhausted", 0) == 0)
			error += " (250 million units per run: an explored tile read costs about 1,500, an "
					 "unexplored one far less, and a for...of loop about three times an index loop; "
					 "read only the area you need, or spread scans over runs of a standing program, "
					 "whose globals persist)";
		if (!eligible())
			throw std::runtime_error("Player context changed");
		if (!success)
			throw std::runtime_error(error.empty() ? "Worker failed or timed out" : error);
		std::vector<std::shared_ptr<Order>> orders;
		for (const auto &o : result.value("orders", Json::array()))
			orders.push_back(Script::order(gui.game, team, value(placeByName(o))));
		if (orders.size() > 32)
			throw std::runtime_error("Too many orders");
		if (kind == "install" || kind == "replace")
		{
			const auto id = current.request.at("name").get<std::string>();
			auto old = programs.find(id);
			if (kind == "install" && (old != programs.end() || programs.size() >= 8))
				throw std::runtime_error("Standing order exists or eight-program limit reached");
			if (kind == "replace" &&
				(old == programs.end() ||
				 old->second.definition.at("revision") != current.request.at("expected_revision")))
				throw std::runtime_error("Standing order revision changed");
			// Retain name revisions after cancellation to reject stale replacements
			// if a caller reinstalls a different program under the same name.
			auto revision = revisions.try_emplace(id, 0).first;
			const unsigned nextRevision = revision->second + 1;
			Program p;
			p.definition = {{"id", id},
							{"name", id},
							{"description", current.request.value("description", std::string())},
							{"source", current.request.at("source")},
							{"intervalTicks", current.request.value("interval_ticks", 25)},
							{"revision", nextRevision}};
			p.state = result.value("installedState", Json(nullptr));
			p.initialized = installedInitialized;
			// Pause/resume from the in-game card remains authoritative even if
			// it arrives while replacement preflight is running.
			p.paused = old != programs.end() && old->second.paused;
			p.nextTick = tick + 25;
			p.generation = ++generation;
			programs[id] = std::move(p); // Preflight produced no orders or wake effects.
			revision->second = nextRevision;
		}
		else
		{
			// Read required state before the atomic queue callback mutates a program.
			const Json nextState = name.empty() ? Json(nullptr) : result.at("state");
			if (!gui.enqueueCommanderOrders(orders,
											[&]
											{
												if (!name.empty())
												{
													program->second.state = nextState;
													program->second.initialized = true;
													program->second.random = nextRandom;
												}
												return true;
											}))
				throw std::runtime_error("Human order queue full; no orders submitted");
			submitted = !orders.empty();
		}
	}
	catch (const std::exception &e)
	{
		success = false;
		error = e.what();
		if (!result.is_object()) result = Json::object();
		// Failed IPC must still yield a well-shaped diagnostic record below.
		if (result.contains("wakes") && !result.at("wakes").is_array()) result.erase("wakes");
	}
	Json record = {{"status", success ? "completed" : "failed"},
				   {"submitted", submitted},
				   {"observation_tick", observationTick},
				   {"submission_tick", tick},
				   {"order_count", submitted ? result.value("orders", Json::array()).size() : 0},
				   {"output", result.value("output", Json(nullptr))},
				   {"error", error}};
	if (kind != "install" && kind != "replace")
		record["local_events"] = result.value("wakes", Json::array());
	if (!name.empty() && program != programs.end() &&
		program->second.generation == currentGeneration)
	{
		program->second.recent = record;
		auto &p = program->second;
		if (success)
			p.retries = 0;
		else
		{
			// A worker that could not start says nothing about the script: retry it a few
			// times before pausing. Any other failure pauses it, as before. Either way the
			// failure becomes a game_world alert, so a paused program is not missed.
			const bool transient = error.find("Cannot start Hive worker") != std::string::npos;
			if (transient && p.retries < 3)
			{
				++p.retries;
				p.nextTick = tick + 100;
			}
			else
				p.paused = true;
			failures.push_back({name, error.substr(0, error.find('\n')), tick, p.paused});
			while (failures.size() > 16)
				failures.pop_front();
		}
	}
	remember(current.key, record);
	if (!error.empty())
	{
		// Name the failing program or operation; a bare diagnostic such as "1"
		// or a stack trace cannot be traced back to its source in the HUD.
		const auto subject = !name.empty() ? name
							 : current.request.contains("name") && current.request.at("name").is_string()
								 ? current.request.at("name").get<std::string>()
								 : current.key;
		const auto line = error.substr(0, error.find('\n'));
		reports.push_back(subject.empty() ? line : subject + ": " + line);
		if (reports.size() > 32)
			reports.erase(reports.begin());
	}
}
void LocalCommander::update()
{
	if (!valid)
		return;
	if (!contextCurrent())
	{
		clear();
		return;
	}
	if (memoryTracked)
	{
		const auto observedTick = gui.simulationThreaded ? gui.drawnScene().tick : gui.game.stepCounter;
		if (observedTick >= nextMemoryTick)
		{
			nextMemoryTick = observedTick + 50;
			boundary([&] { refreshMemory(); });
		}
	}
	// An idle commander must not park the simulation on every presentation frame.
	// Scene time is sufficient to decide whether owner-bound work might be due.
	if (worker.valid() && worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;
	if (!worker.valid() && queued.empty())
	{
		const auto observedTick =
			gui.simulationThreaded ? gui.drawnScene().tick : gui.game.stepCounter;
		bool due = false;
		for (const auto &[name, program] : programs)
			if (!program.paused && observedTick >= program.nextTick)
			{
				due = true;
				break;
			}
		if (!due)
			return;
	}
	boundary(
		[&]
		{
			if (!eligible())
			{
				clear();
				return;
			}
			tick = gui.game.stepCounter;
			if (worker.valid())
			{
				if (worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
					return;
				Json result;
				try
				{
					result = worker.get();
				}
				catch (const std::exception &e)
				{
					result = {{"ok", false}, {"error", e.what()}};
				}
				finish(std::move(result));
				current = {};
				currentProgram.clear();
			}
			if (!queued.empty())
			{
				current = std::move(queued.front());
				queued.pop_front();
			}
			else
			{
				const auto name = scheduleDue(programs, tick);
				if (!name.empty())
				{
					auto &p = programs.at(name);
					currentProgram = name;
					currentGeneration = p.generation;
					current.request = {{"source", p.definition.at("source")}};
				}
			}
			if (current.request.is_null())
				return;
			try
			{
				Json snapshot = capture(observations, gui.game, team);
				annotate(snapshot);
				Json request = {{"source", current.request.at("source")}, {"snapshot", std::move(snapshot)}};
				observationTick = tick;
				if (!currentProgram.empty())
				{
					auto &p = programs.at(currentProgram);
					request["state"] = p.state;
					request["initialized"] = p.initialized;
					request["random"] = p.random;
				}
				if (!current.key.empty())
					remember(
						current.key,
						{{"status", "running"}, {"submitted", false}, {"observation_tick", tick}});
				worker = launch ? launch(std::move(request)) : startWorker(std::move(request));
			}
			catch (const std::exception &e)
			{
				finish({{"ok", false}, {"error", e.what()}});
				current = {};
				currentProgram.clear();
			}
		});
}
Json LocalCommander::world(const Json &args)
{
	const auto detail = args.value("detail", std::string("summary"));
	const bool full = detail == "full", alertsOnly = detail == "alerts",
			   brief = detail == "brief" || alertsOnly;
	const int limit = args.value("limit", 100);
	// since_tick keeps only alerts that happened after it (pass the previous reply's tick).
	const unsigned since = args.value("since_tick", 0u);
	int ax = 0, ay = 0, aw = std::numeric_limits<int>::max(), ah = aw;
	if (args.contains("area"))
	{
		const auto &a = args.at("area");
		ax = a.at("x"), ay = a.at("y"), aw = a.at("width"), ah = a.at("height");
	}
	const auto inside = [&](const Json &e)
	{
		const int x = e.value("x", -1), y = e.value("y", -1);
		return x >= ax && y >= ay && x - ax < aw && y - ay < ah;
	};
	Json result;
	boundary(
		[&]
		{
			// What the panel of a selected building shows: each stock the HUD lists with
			// its capacity (sites: delivered of needed), units inside, and for an Inn the
			// happiness it offers (1 with wheat, +1 per fruit it holds more of than units
			// inside; 0 when it cannot feed).
			const auto describeStock = [&](Json &record, const Json &observed, const BuildingType &type)
			{
				Json stock = Json::object();
				const auto materials = observed.value("materials", Json::array());
				for (unsigned i = 0; i < MaterialCount && i < materials.size(); ++i)
					if (type.maxMaterial[i] && gui.drawnScene().materialVisible(i))
						stock[std::string(MaterialKeys[i])] =
							std::to_string(materials[i].get<int>()) + "/" + std::to_string(type.maxMaterial[i]);
				if (type.maxBullets)
					stock["bullets"] = std::to_string(observed.value("bullets", 0)) + "/" +
									   std::to_string(type.maxBullets);
				if (!stock.empty())
					record["stock"] = std::move(stock);
				auto *building = gui.game.teams[team]->myBuildings[Building::GIDtoID(observed.value("id", 0))];
				// Units that have taken this building's or flag's job, beside the workers it
				// requests: a war flag far from food can request 20 and have none.
				if (building)
					record["assigned"] = int(building->unitsWorking.size());
				if (!building || type.isBuildingSite)
					return;
				if (type.maxUnitInside)
					record["inside"] = std::to_string(building->unitsInside.size()) + "/" +
									   std::to_string(building->maxUnitInside);
				if (type.canFeedUnit)
					record["happiness"] = building->availableHappynessLevel();
			};
			// One line per building for detail brief.
			const auto briefLine = [](const Json &record)
			{
				const auto number = [&](const char *field, int fallback)
				{ return std::to_string(record.value(field, fallback)); };
				std::string line = record.value("type", std::string()) + " #" + number("id", 0) + " gen " +
								   number("generation", 0) + " L" + number("level", 1) + " at " +
								   number("x", 0) + "," + number("y", 0);
				if (record.contains("site"))
					line += " site";
				line += " hp " + number("hp", 0) + "/" + number("max_hp", 0);
				if (record.contains("workers"))
					line += " workers " + number("workers", 0);
				if (record.contains("assigned"))
					line += " assigned " + number("assigned", 0);
				if (record.contains("stock"))
					for (const auto &[material, amount] : record.at("stock").items())
						line += " " + material + " " + amount.get<std::string>();
				if (record.contains("inside"))
					line += " inside " + record.at("inside").get<std::string>();
				if (record.contains("happiness"))
					line += " happiness " + number("happiness", 0);
				return line;
			};
			auto scope = observations.captureObservation();
			observations.observe();
			const auto units = json(observations.query("units", {}));
			const auto buildings = json(observations.query("buildings", {}));
			auto &types = gui.game.buildingsTypes;
			const Uint32 enemies = gui.game.teams[team]->enemies;
			Json own = Json::array(), others = Json::array(), seen = Json::array();
			std::map<std::string, int> ownUnits, otherUnits;
			Json counts = {{"buildings", 0}, {"enemy_buildings", 0}, {"enemy_units", 0}};
			// Every own building, whatever area and limit filter the reply, to follow up
			// game_place requests (a filter must not make a placed site look refused).
			Json ownIndex = Json::array();
			for (const auto &b : buildings)
			{
				if (b.value("team", -1) == team)
				{
					const auto *type = types.get(b.value("type", 0));
					const std::string key = b.value("key", std::string());
					ownIndex.push_back({{"type", key.substr(0, key.find('.'))},
										{"x", b.value("x", 0)},
										{"y", b.value("y", 0)},
										{"id", b.value("id", 0)},
										{"generation", b.value("generation", 0)},
										{"site", type && type->isBuildingSite},
										{"flag", type && type->isVirtual}});
				}
				if (!inside(b))
					continue;
				const auto *type = types.get(b.value("type", 0));
				const std::string key = b.value("key", std::string());
				Json record = {{"id", b.value("id", 0)},
							   {"generation", b.value("generation", 0)},
							   {"type", key.substr(0, key.find('.'))},
							   {"x", b.value("x", 0)},
							   {"y", b.value("y", 0)},
							   {"width", type ? type->width : 1},
							   {"height", type ? type->height : 1},
							   {"level", b.value("level", 0) + 1},
							   {"hp", b.value("hp", 0)},
							   {"max_hp", b.value("maxHp", 0)}};
				if (type && type->isBuildingSite)
					record["site"] = true;
				const int owner = b.value("team", -1);
				if (owner == team)
				{
					if (b.contains("workers"))
						record["workers"] = b["workers"];
					if (type && type->isVirtual && b.contains("range"))
						record["range"] = b["range"];
					if (type)
						describeStock(record, b, *type);
					counts["buildings"] = counts["buildings"].get<int>() + 1;
					if (int(own.size()) < limit)
						own.push_back(std::move(record));
				}
				else
				{
					// Another team's building: what is on screen, not its hit points.
					record.erase("hp");
					record.erase("max_hp");
					record["team"] = owner;
					record["enemy"] = owner >= 0 && (enemies & Team::teamNumberToMask(owner)) != 0;
					counts["enemy_buildings"] = counts["enemy_buildings"].get<int>() + 1;
					if (int(others.size()) < limit)
						others.push_back(std::move(record));
				}
			}
			Json ownList = Json::array();
			// Warrior strength decides fights: count warriors by attack level, and the
			// colony's hungry and injured units (medical 1 and 2).
			std::map<std::string, int> ownWarriors;
			int hungry = 0, injured = 0;
			for (const auto &u : units)
			{
				if (!inside(u))
					continue;
				const int owner = u.value("team", -1);
				// Only your own warriors' training is known to you.
				if (owner == team && u.value("type", -1) == WARRIOR && u.contains("levels") &&
					u["levels"].size() > ATTACK_STRENGTH)
					++ownWarriors["level " + std::to_string(u["levels"][ATTACK_STRENGTH].get<int>() + 1)];
				if (owner == team)
				{
					const int medical = u.value("medical", 0);
					hungry += medical == 1;
					injured += medical == 2;
				}
				// Unit records name their type only in extended queries; map the base kinds.
				static const char *const kinds[] = {"worker", "explorer", "warrior"};
				const int typeNumber = u.value("type", -1);
				const std::string kind = u.value(
					"typeKey", std::string(typeNumber >= 0 && typeNumber < 3 ? kinds[typeNumber] : "unit"));
				Json record = {{"type", kind}, {"x", u.value("x", 0)}, {"y", u.value("y", 0)},
							   {"hp", u.value("hp", 0)}, {"max_hp", u.value("maxHp", 0)}};
				if (owner == team)
				{
					++ownUnits[kind];
					if (full && int(ownList.size()) < limit)
					{
						record["id"] = u.value("id", 0);
						record["generation"] = u.value("generation", 0);
						ownList.push_back(std::move(record));
					}
				}
				else
				{
					++otherUnits[std::to_string(owner) + ":" + kind];
					record.erase("hp");
					record.erase("max_hp");
					record["team"] = owner;
					counts["enemy_units"] = counts["enemy_units"].get<int>() + 1;
					if (full && int(seen.size()) < limit)
						seen.push_back(std::move(record));
				}
			}
			Json placeable = Json::array();
			for (const auto &[name, enabled] : gui.placeableChoices())
				if (enabled)
				{
					const auto *type = types.get(types.getPlaceableTypeNum(name));
					placeable.push_back({{"name", name.substr(0, name.find('.'))},
										 {"width", type ? type->width : 1},
										 {"height", type ? type->height : 1}});
				}
			// Other teams' buildings seen earlier and now under fog, as last seen. The
			// refresh also notes which of the colony's own buildings are gone.
			memoryTracked = true;
			refreshMemory();
			std::set<int> listed;
			for (const auto &b : others)
				listed.insert(b.value("id", -1));
			Json remembered = Json::array();
			for (const auto &[gid, record] : seenBuildings)
			{
				if (listed.count(gid) || !inside(record) || int(remembered.size()) >= limit)
					continue;
				Json entry = record;
				entry["remembered"] = true;
				entry["enemy"] = (enemies & Team::teamNumberToMask(record.value("team", 0))) != 0;
				remembered.push_back(std::move(entry));
			}
			// Resources the colony knows of near its buildings, by type: tile count and
			// the nearest tile to the colony's centre (its Swarm when it has one).
			int cx = -1, cy = -1, minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
			for (const auto &b : own)
			{
				const int bx = b.at("x"), by = b.at("y");
				if (cx < 0 || b.at("type") == "swarm")
					cx = bx + b.value("width", 1) / 2, cy = by + b.value("height", 1) / 2;
				minX = std::min(minX, bx), minY = std::min(minY, by);
				maxX = std::max(maxX, bx), maxY = std::max(maxY, by);
			}
			Json resources = Json::object();
			if (cx >= 0 && !brief)
			{
				const int radius = args.value("resource_radius", 24);
				const int w = gui.game.map.getW(), h = gui.game.map.getH();
				const auto types = json(observations.query("resourceTypes", {}));
				struct Seen { int tiles = 0, amount = 0, x = 0, y = 0, distance = 1 << 30; };
				std::map<std::string, Seen> seen;
				const int spanX = std::min(w, maxX - minX + 2 * radius), spanY = std::min(h, maxY - minY + 2 * radius);
				for (int oy = 0; oy < spanY; ++oy)
					for (int ox = 0; ox < spanX; ++ox)
					{
						const int x = ((minX - radius + ox) % w + w) % w, y = ((minY - radius + oy) % h + h) % h;
						// Tiles as this colony last saw them (fog of war), never their live state.
						const auto cell = observations.cell(x, y);
						if (!cell.known || cell.resource == 65535 || !cell.amount || cell.resource >= types.size())
							continue;
						auto &r = seen[types[cell.resource].value("key", std::string("resource"))];
						++r.tiles;
						r.amount += cell.amount;
						const int dx = std::min(std::abs(x - cx), w - std::abs(x - cx));
						const int dy = std::min(std::abs(y - cy), h - std::abs(y - cy));
						if (dx + dy < r.distance)
							r.distance = dx + dy, r.x = x, r.y = y;
					}
				for (const auto &[key, r] : seen)
					resources[key] = {{"tiles", r.tiles}, {"amount", r.amount},
									  {"nearest", {{"x", r.x}, {"y", r.y}, {"distance", r.distance}}}};
			}
			// Losses first: buildings gone since recent refreshes, then the HUD's event
			// rows (attacks, conversions, finished buildings) with their tiles.
			const auto now = gui.game.stepCounter;
			// An alert older than AlertTicks, or not after since_tick, is not reported.
			const auto stale = [&](unsigned age) { return age > AlertTicks || (since && now - age <= since); };
			Json alerts = Json::array(), lostBuildings = Json::array();
			// Standing orders that failed: paused ones need hive_program resume after a fix.
			for (auto it = failures.rbegin(); it != failures.rend(); ++it)
			{
				const unsigned age = now > it->tick ? now - it->tick : 0;
				if (stale(age))
					continue;
				alerts.push_back({{"event", "Hive program " + it->program + (it->paused ? " paused: " : " retrying: ") + it->error},
								  {"kind", "hive_program_failed"}, {"program", it->program}, {"paused", it->paused},
								  {"count", 1}, {"ticks_ago", age}});
			}
			for (auto it = losses.rbegin(); it != losses.rend(); ++it)
			{
				const unsigned age = now > it->tick ? now - it->tick : 0;
				if (stale(age))
					continue;
				Json loss = {{"event", (it->demolished ? "Building demolished: " : "Building destroyed: ") + it->type},
							 {"kind", it->demolished ? "building_demolished" : "building_destroyed"},
							 {"x", it->x}, {"y", it->y}, {"count", 1}, {"ticks_ago", age}};
				if (!it->demolished)
					lostBuildings.push_back({{"type", it->type}, {"x", it->x}, {"y", it->y}, {"ticks_ago", age}});
				alerts.push_back(std::move(loss));
			}
			Json events = Json::array();
			// Other teams' buildings first seen recently (only ever what the colony saw).
			for (auto it = sightings.rbegin(); it != sightings.rend(); ++it)
			{
				const unsigned age = now > it->tick ? now - it->tick : 0;
				if (stale(age))
					continue;
				events.push_back({{"event", "Building sighted: " + it->type + " of team " + std::to_string(it->team)},
								  {"kind", "enemy_building_sighted"}, {"x", it->x}, {"y", it->y},
								  {"team", it->team}, {"count", 1}, {"ticks_ago", age}});
			}
			for (const auto &row : gui.gameEvents().rows())
			{
				const unsigned age = now > row.lastTick ? now - row.lastTick : 0;
				if (stale(age))
					continue;
				Json alert = {{"event", row.label}, {"kind", row.type < GESize ? eventKinds[row.type] : "event"},
							  {"x", row.x}, {"y", row.y}, {"count", row.count}, {"ticks_ago", age}};
				(row.type == GEUnitLostConversion ? alerts : events).push_back(std::move(alert));
			}
			for (auto &e : events)
				alerts.push_back(std::move(e));
			// Conversions are rate-limited as events; the colony's counter is exact.
			const int convertedAway = gui.game.teams[team]->unitConversionLost;
			Json lossSummary = {{"buildings_destroyed", lostBuildings},
								{"units_converted_away_total", convertedAway},
								{"units_converted_to_you_total", gui.game.teams[team]->unitConversionGained},
								{"units_converted_away_since_last_read",
								 convertedAwaySeen < 0 ? 0 : convertedAway - convertedAwaySeen}};
			convertedAwaySeen = convertedAway;
			// Follow up recent game_place requests: a queued order can still be refused
			// by the game, or its site destroyed before it was built.
			Json followUp = Json::array();
			for (auto it = placements.begin(); it != placements.end();)
			{
				std::string status = "pending";
				Json entry = {{"building", it->building}, {"x", it->x}, {"y", it->y}};
				for (const auto &b : ownIndex)
					if (b.value("x", -1) == it->x && b.value("y", -1) == it->y &&
						b.value("type", std::string()) == it->building)
					{
						// Flags are placed directly; buildings start as sites.
						status = b.value("site", false) ? "site" : b.value("flag", false) ? "placed" : "built";
						entry["id"] = b.at("id");
						entry["generation"] = b.at("generation");
					}
				if (status == "pending" && gui.game.stepCounter > it->tick + 40)
					status = "missing: refused by the game or destroyed before it was seen";
				entry["status"] = status;
				followUp.push_back(std::move(entry));
				it = status == "pending" ? std::next(it) : placements.erase(it);
			}
			const auto bar = gui.topBarValues();
			Json status = {{"workers_free_total", bar[0]},	 {"explorers_free_total", bar[1]},
						   {"warriors_free_total", bar[2]}, {"prestige", bar[3]},
						   {"win_chance_percent", bar[4]}};
			Json visibleUnits = Json::object();
			for (const auto &[k, n] : otherUnits)
				visibleUnits[k] = n;
			const Json forces = {{"your_warriors_by_attack_level", ownWarriors},
								 {"your_hungry_units", hungry},
								 {"your_injured_units", injured}};
			if (brief)
			{
				result = {{"tick", gui.game.stepCounter}, {"team", team},	   {"status", status},
						  {"units", ownUnits},			   {"forces", forces}, {"alerts", alerts},
						  {"losses", lossSummary},		   {"buildings", Json::array()}};
				// detail alerts: what changed, without the building list.
				if (alertsOnly)
					result.erase("buildings");
				else
					for (const auto &b : own)
						result["buildings"].push_back(briefLine(b));
				if (!followUp.empty())
					result["placements"] = followUp;
				return;
			}
			result = {{"tick", gui.game.stepCounter},
					  {"team", team},
					  {"map", {{"width", gui.game.map.getW()}, {"height", gui.game.map.getH()}}},
					  {"colony", {{"buildings", own}, {"units", ownUnits}}},
					  {"others", {{"buildings", others}, {"units_by_team_and_type", visibleUnits}}},
					  {"counts", counts},
					  {"status", status},
					  {"forces", forces},
					  {"resources", resources},
					  {"placeable", placeable}};
			result["others"]["remembered_buildings"] = remembered;
			result["alerts"] = alerts;
			result["losses"] = lossSummary;
			if (!followUp.empty())
				result["placements"] = followUp;
			if (full)
			{
				result["colony"]["unit_list"] = ownList;
				result["others"]["unit_list"] = seen;
			}
		});
	if (brief)
		return result;
	const auto &camera = gui.camera;
	result["view"] = {{"x", int(std::floor(camera.originX / 32))},
					  {"y", int(std::floor(camera.originY / 32))},
					  {"width", int(std::ceil(camera.visibleW() / 32))},
					  {"height", int(std::ceil(camera.visibleH() / 32))}};
	result["note"] = "Tiles are map coordinates; x/y of a building is its top-left tile. Only what "
					 "your colony can see is listed. Place with game_place, look with game_view.";
	return result;
}
Json LocalCommander::place(const Json &args)
{
	const auto name = args.at("building").get<std::string>();
	const int x = args.at("x"), y = args.at("y");
	const int radius = args.value("near", false) ? args.value("search_radius", 12) : 0;
	std::pair<int, int> at{x, y}, size{1, 1};
	std::vector<std::array<int, 4>> reserved;
	// Sites appear within a few ticks; older entries no longer need reserving.
	while (!placements.empty() && gui.game.stepCounter > placements.front().tick + 200)
		placements.pop_front();
	for (const auto &p : placements)
		reserved.push_back({p.x, p.y, p.width, p.height});
	if (const auto error = gui.placeFromAutomation(name, x, y, radius, &at, reserved, &size); !error.empty())
		throw std::runtime_error(error);
	placements.push_back({name.substr(0, name.find('.')), at.first, at.second, size.first, size.second,
						  gui.game.stepCounter});
	while (placements.size() > 16)
		placements.pop_front();
	return {{"queued", true},
			{"building", name},
			{"x", at.first},
			{"y", at.second},
			{"note", "Queued like a click. The site or flag appears after the next simulation "
					 "step; game_world lists it under placements until it does (or says it never "
					 "did). Change its workers with game_building."}};
}
Json LocalCommander::view(const Json &args)
{
	const int x = args.at("x"), y = args.at("y");
	if (x < 0 || y < 0 || x >= int(gui.game.map.getW()) || y >= int(gui.game.map.getH()))
		throw std::runtime_error("Tile (" + std::to_string(x) + ", " + std::to_string(y) +
								 ") is outside the map; read its size from game_world");
	// width/height: frame that tile rectangle (top-left x/y), as a player zooms out to
	// see a battle or a whole base; zoom is then chosen to fit it.
	double cx = x + 0.5, cy = y + 0.5;
	std::optional<double> zoom;
	if (args.contains("zoom"))
		zoom = args.at("zoom").get<double>();
	if (args.contains("width") || args.contains("height"))
	{
		const int w = args.value("width", 1), h = args.value("height", 1);
		cx = x + w / 2.0, cy = y + h / 2.0;
		const auto &c = gui.camera;
		// Screen size in logical pixels is the visible world span times the zoom.
		const double fit = std::min(c.visibleW() * c.zoom / (w * 32.0), c.visibleH() * c.zoom / (h * 32.0));
		zoom = std::clamp(fit, c.minimumZoom(), double(MapCamera::MAX_ZOOM));
	}
	// Zoom first, about the view's centre, so the tile ends up centred at the new scale.
	if (zoom && !gui.setMapZoom(*zoom))
		throw std::runtime_error("Zoom is unavailable while the torus view reveals itself; "
								 "try again in a moment");
	gui.centreViewOnTile(cx, cy);
	const auto &camera = gui.camera;
	const auto point = camera.worldToScreen((x + 0.5) * 32, (y + 0.5) * 32);
	return {{"view",
			 {{"x", int(std::floor(camera.originX / 32))},
			  {"y", int(std::floor(camera.originY / 32))},
			  {"width", int(std::ceil(camera.visibleW() / 32))},
			  {"height", int(std::ceil(camera.visibleH() / 32))}}},
			{"tile_screen", {{"x", int(point.first)}, {"y", int(point.second)}}},
			{"tile_pixels", 32 * camera.zoom},
			{"zoom", camera.zoom},
			{"zoom_range", {camera.minimumZoom(), MapCamera::MAX_ZOOM}},
			{"note", "tile_screen is the tile's centre in the logical pixels ui_inspect bounds use; "
					 "with a screenshot, prefer its capture_id coordinates."}};
}
Json LocalCommander::chat(const Json &args)
{
	const std::string text = args.at("text");
	// The chat order carries at most this many bytes of UTF-8; refuse rather
	// than send a message the players would see cut short.
	if (text.size() > static_cast<std::size_t>(ORDER_TEXT_MESSAGE_MAX_TEXT_BYTES))
		throw std::runtime_error("Chat text is " + std::to_string(text.size()) +
								 " bytes of UTF-8; a chat message holds at most " +
								 std::to_string(ORDER_TEXT_MESSAGE_MAX_TEXT_BYTES) +
								 " (accented letters and emoji take 2 to 4 bytes each). "
								 "Shorten it or send it in parts.");
	if (!gui.sendChatFromAutomation(text))
		throw std::runtime_error("Chat text is empty");
	return {{"sent", true},
			{"text", text},
			{"note", "Queued like the chat box's Send: the message shows in the game's "
					 "message area, from your player name, after the next simulation step."}};
}
Json LocalCommander::time(const Json &args)
{
	// call() has checked this is the local human player's own match: no network
	// peers, replay or spectating, where the player could hard-pause too.
	if (!gui.canChangeGameSpeed())
		throw std::runtime_error("Time control is only for local matches without network players");
	const auto action = args.value("action", std::string("status"));
	// speed: the top bar's stepper, with any action (status, pause, resume, step).
	if (args.contains("speed"))
		if (const auto error = gui.setGameSpeedFromAutomation(args.at("speed").get<int>()); !error.empty())
			throw std::runtime_error(error);
	unsigned steps = 0;
	if (action == "pause")
	{
		gui.heldSteps = 0;
		gui.agentPaused = true;
		gui.hardPause = true;
	}
	else if (action == "resume")
	{
		gui.heldSteps = 0;
		gui.agentPaused = false;
		gui.hardPause = false;
		// A pause from the menu or the Pause key is the ordinary, recorded pause.
		if (gui.gamePaused)
			gui.requestPause(false);
	}
	else if (action == "step")
	{
		if (gui.gamePaused)
			throw std::runtime_error("The match is paused from its menu or the Pause key; call "
									 "game_time resume first, then pause and step");
		steps = args.value("ticks", 30u);
		gui.agentPaused = true;
		gui.heldSteps = steps;
		gui.hardPause = true;
	}
	const unsigned left = gui.heldSteps.load();
	Json result = {{"tick", tick},
				   {"paused", bool(gui.gamePaused) || gui.simulationHeld()},
				   {"paused_by", gui.gamePaused	   ? "menu"
								 : !gui.hardPause ? "none"
								 : gui.agentPaused ? "agent"
												   : "player"},
				   {"steps_remaining", left},
				   {"speed", globalContainer->settings.getGameSpeedText()},
				   {"ticks_per_second", gui.targetTickRate()}};
	if (action == "step")
	{
		result["target_tick"] = tick + steps;
		result["note"] = "Stepping runs the ticks at the current speed, then holds again; "
						 "check game_time status until steps_remaining is 0. Orders sent while "
						 "paused (game_place, clicks) apply as the next ticks run.";
	}
	else if (result["paused"] == true)
		result["note"] = "Paused: no ticks run. You can still read, place, click and type; "
						 "orders apply once the match runs again (resume or step).";
	return result;
}
void LocalCommander::refreshMemory()
{
	// Callers hold the simulation boundary. Only what the colony currently sees
	// updates the memory: a building is forgotten when its tile is in view and
	// it is no longer there, never because it changed out of sight.
	auto scope = observations.captureObservation();
	observations.observe();
	std::set<int> visible;
	std::map<int, Owned> owned;
	for (const auto &b : json(observations.query("buildings", {})))
	{
		const int owner = b.value("team", -1);
		const int gid = b.value("id", -1);
		const auto *type = gui.game.buildingsTypes.get(b.value("type", 0));
		if (!type || type->isVirtual)
			continue;
		const std::string key = b.value("key", std::string());
		if (owner == team)
		{
			const auto *building = gui.game.teams[team]->myBuildings[Building::GIDtoID(gid)];
			owned[gid] = {key.substr(0, key.find('.')), b.value("x", 0), b.value("y", 0),
						  b.value("generation", 0u),
						  building && building->buildingState == Building::WAITING_FOR_DESTRUCTION};
			continue;
		}
		// Only once memory exists: the first refresh notes what is already in view.
		if (!seenBuildings.count(gid) && memoryStarted)
		{
			sightings.push_back({key.substr(0, key.find('.')), b.value("x", 0), b.value("y", 0), owner,
								 gui.game.stepCounter});
			while (sightings.size() > 32)
				sightings.pop_front();
		}
		seenBuildings[gid] = {{"type", key.substr(0, key.find('.'))}, {"x", b.value("x", 0)},
							  {"y", b.value("y", 0)}, {"width", type->width}, {"height", type->height},
							  {"team", owner}, {"last_seen_tick", gui.game.stepCounter}};
		visible.insert(gid);
	}
	for (auto it = seenBuildings.begin(); it != seenBuildings.end();)
	{
		const auto cell = observations.cell(it->second.value("x", 0), it->second.value("y", 0));
		it = cell.visible && !visible.count(it->first) ? seenBuildings.erase(it) : std::next(it);
	}
	// Own buildings that are gone since the last refresh. An identity reused by a new
	// building has another generation, so the earlier one is gone too.
	for (const auto &[gid, before] : ownBuildings)
	{
		const auto now = owned.find(gid);
		if (now != owned.end() && now->second.generation == before.generation)
			continue;
		losses.push_back({before.type, before.x, before.y, gui.game.stepCounter,
						  before.demolishing || demolitionOrders.count(gid) > 0});
		demolitionOrders.erase(gid);
	}
	while (losses.size() > 32)
		losses.pop_front();
	ownBuildings = std::move(owned);
	memoryStarted = true;
}
void LocalCommander::submit(const std::vector<Json> &descriptors)
{
	std::vector<std::shared_ptr<Order>> orders;
	std::string error;
	boundary(
		[&]
		{
			try
			{
				for (const auto &d : descriptors)
					orders.push_back(Script::order(gui.game, team, value(d)));
			}
			catch (const std::exception &e)
			{
				error = e.what();
			}
		});
	if (!error.empty())
		throw std::runtime_error(error);
	if (!gui.enqueueCommanderOrders(orders, [] { return true; }))
		throw std::runtime_error("Order queue full; nothing was changed. Try again shortly");
}
Json LocalCommander::building(const Json &args)
{
	const Json ref = {{"id", args.at("id")}, {"generation", args.at("generation")}};
	std::vector<Json> orders;
	Json changed = Json::array();
	// A request above the building's unit limit, or a flag range above its type's
	// maximum, is capped, and the reply says so.
	int limit = MAX_BUILDING_WORKER_REQUEST;
	int maximumRange = -1;
	boundary(
		[&]
		{
			const int id = args.at("id");
			if (Building::GIDtoTeam(id) != team)
				return;
			if (const auto *b = gui.game.teams[team]->myBuildings[Building::GIDtoID(id)])
			{
				limit = std::min(limit, int(b->type->semantics.assignmentLimit));
				maximumRange = b->type->maxUnitStayRange;
			}
		});
	int requested = 0, rangeRequested = 0;
	if (args.contains("workers"))
	{
		requested = args.at("workers");
		orders.push_back({{"type", "workers"}, {"building", ref}, {"workers", std::min(requested, limit)}});
		changed.push_back("workers");
	}
	if (args.contains("priority"))
	{
		const auto p = args.at("priority").get<std::string>();
		orders.push_back({{"type", "priority"}, {"building", ref},
						  {"priority", p == "low" ? -1 : p == "high" ? 1 : 0}});
		changed.push_back("priority");
	}
	if (args.contains("production"))
	{
		const auto &r = args.at("production");
		orders.push_back({{"type", "production"}, {"building", ref},
						  {"ratios", {r.value("workers", 0), r.value("explorers", 0), r.value("warriors", 0)}}});
		changed.push_back("production");
	}
	if (args.contains("range"))
	{
		rangeRequested = args.at("range");
		const int range = maximumRange >= 0 ? std::min(rangeRequested, maximumRange) : rangeRequested;
		orders.push_back({{"type", "range"}, {"building", ref}, {"range", range}});
		changed.push_back("range");
	}
	if (args.contains("action"))
	{
		const auto a = args.at("action").get<std::string>();
		const int builders = args.value("builders", 3);
		if (a == "upgrade" || a == "repair")
			orders.push_back({{"type", "construction"}, {"building", ref}, {"workers", builders},
							  {"futureWorkers", args.value("workers", builders)}});
		else if (a == "cancel_upgrade")
			orders.push_back({{"type", "cancelConstruction"}, {"building", ref}, {"workers", args.value("workers", 1)}});
		else if (a == "destroy")
		{
			orders.push_back({{"type", "delete"}, {"building", ref}});
			demolitionOrders.insert(args.at("id").get<int>());
		}
		else if (a == "cancel_destroy")
		{
			orders.push_back({{"type", "cancelDelete"}, {"building", ref}});
			demolitionOrders.erase(args.at("id").get<int>());
		}
		else if (a == "move")
		{
			if (!args.contains("x") || !args.contains("y"))
				throw std::runtime_error("move needs the flag's new x and y tile");
			orders.push_back({{"type", "moveFlag"}, {"building", ref}, {"x", args.at("x")}, {"y", args.at("y")}});
		}
		changed.push_back(a);
	}
	Json result = {{"queued", changed},
				   {"note", "Applied like the building panel's controls after the next simulation "
							"step; read game_world to confirm."}};
	if (requested > limit)
		result["workers_capped_at"] = limit;
	if (maximumRange >= 0 && rangeRequested > maximumRange)
		result["range_capped_at"] = maximumRange;
	if (orders.empty())
		throw std::runtime_error("Nothing to change: pass workers, priority, production, range or action");
	submit(orders);
	return result;
}
Json LocalCommander::zone(const Json &args)
{
	const auto kind = args.at("zone").get<std::string>();
	const int width = args.at("width"), height = args.at("height");
	Json mask = Json::array();
	for (int i = 0; i < width * height; ++i)
		mask.push_back(true);
	submit({{{"type", kind == "forbidden" ? "forbidden" : kind == "guard" ? "guardArea" : "clearArea"},
			 {"mode", args.value("mode", std::string("paint")) == "erase" ? 2 : 1},
			 {"x", args.at("x")},
			 {"y", args.at("y")},
			 {"width", width},
			 {"height", height},
			 {"mask", mask}}});
	return {{"queued", true},
			{"note", "The zone is painted after the next simulation step; it appears on the map "
					 "and in the Flags and areas panel."}};
}
Json LocalCommander::stats(const Json &args)
{
	std::shared_ptr<const TeamStats> history;
	std::vector<Stats::Metric> metrics;
	Json details;
	Uint32 now = 0;
	boundary(
		[&]
		{
			// A frozen copy: the report is built after the simulation resumes.
			history = gui.game.teams[team]->stats.frozenHistory();
			metrics = Stats::catalogForBuildings(gui.game.buildingsTypes);
			const auto &color = gui.game.teams[team]->color;
			details = {{"team", team}, {"color", colorText(color.r, color.g, color.b)}};
			for (int i = 0; i < gui.game.gameHeader.getNumberOfPlayers(); ++i)
				if (gui.game.gameHeader.getBasePlayer(i).teamNumber == team)
				{
					details["name"] = gui.game.gameHeader.getBasePlayer(i).name;
					break;
				}
			now = gui.game.stepCounter;
		});
	// Like the in-game Statistics panel: the win-probability history reads hidden
	// opponent state, so it is offered only on the results screen.
	for (const auto &id : args.value("metrics", Json::array()))
		if (const auto metric = std::find_if(metrics.begin(), metrics.end(), [&](const auto &m) { return id == m.id; });
			metric != metrics.end() && metric->winProbability)
			throw std::runtime_error("The win probability history is shown only after the match, on the "
									 "end_game results screen; status.win_chance_percent here is the current "
									 "chance the HUD shows");
	std::erase_if(metrics, [](const auto &m) { return m.winProbability; });
	static const std::vector<std::string> defaults = {"population", "net growth", "hunger",	"health",
													  "idle workers", "blocked buildings", "warriors", "intruders"};
	Json result = statsReport(metrics, {{team, history.get(), details}}, args, defaults);
	if (args.value("list", false))
		return result;
	const auto bar = gui.topBarValues();
	result["mode"] = "match";
	result["tick"] = now;
	result["seconds"] = now / GAME_TICKS_PER_SECOND;
	result["team"] = details;
	result["status"] = {{"workers_free_total", bar[0]}, {"explorers_free_total", bar[1]},
						{"warriors_free_total", bar[2]}, {"prestige", bar[3]},
						{"win_chance_percent", bar[4]}, {"converted_in", bar[5]},
						{"converted_out", bar[6]}};
	result["note"] = "Your colony only, as the in-game Statistics panel shows it. Every team's "
					 "statistics and the win probability history follow on the end_game results "
					 "screen, while it is shown.";
	return result;
}
namespace
{
// game_wait's until categories, and the alert kinds each one stands for.
const std::map<std::string, std::set<std::string>> &waitCategories()
{
	static const std::map<std::string, std::set<std::string>> categories = {
		{"attack", {"unit_attacked", "building_attacked"}},
		{"building_completed", {"building_completed"}},
		{"building_lost", {"building_destroyed"}},
		{"conversion", {"unit_converted_away", "unit_converted_to_you"}},
		{"enemy_sighted", {"enemy_building_sighted"}},
		{"hive_failure", {"hive_program_failed"}}};
	return categories;
}
} // namespace
Json LocalCommander::wait(const Json &args)
{
	// Everything read here is what the colony itself is told: the HUD's event rows, its
	// own losses, buildings it has seen and its scripts. Nothing about hidden teams.
	const unsigned now = gui.simulationThreaded ? gui.drawnScene().tick : gui.game.stepCounter;
	std::set<std::string> wanted;
	for (const auto &category : args.value("until", Json{"attack", "building_lost", "conversion",
														  "enemy_sighted", "hive_failure"}))
	{
		const auto found = waitCategories().find(category.get<std::string>());
		if (found == waitCategories().end())
			throw std::runtime_error("Unknown until category " + category.dump() +
									 "; use attack, building_completed, building_lost, conversion, "
									 "enemy_sighted or hive_failure");
		wanted.insert(found->second.begin(), found->second.end());
	}
	Json poll;
	if (args.contains("_poll"))
		poll = args.at("_poll");
	else
	{
		// Losses and sightings come from the game_world memory: start it if needed,
		// so the wait sees buildings lost or first seen from now on.
		boundary(
			[&]
			{
				memoryTracked = true;
				if (!memoryStarted)
					refreshMemory();
			});
		poll = {{"start", now}, {"target", now + args.value("ticks", 300u)}, {"rows", Json::object()},
				{"events", Json::array()}, {"ongoing", Json::object()}};
		// Rows active as the wait starts (a skirmish in progress) are reported but do not
		// end it on every update: only a new row, or a quiet one flaring up, does.
		for (const auto &row : gui.gameEvents().rows())
			if (row.lastTick + 150 >= now)
				poll["ongoing"][std::to_string(row.id)] = true;
	}
	const unsigned start = poll.at("start"), target = poll.at("target");
	std::string reason;
	if (const auto outcome = gui.announcedOutcome(); !outcome.empty())
		reason = "match_end";
	// HUD rows are coalesced and fade, so note each update while the wait lasts.
	for (const auto &row : gui.gameEvents().rows())
	{
		const auto id = std::to_string(row.id);
		if (row.lastTick <= start || poll["rows"].value(id, std::uint64_t(0)) == row.updateSequence)
			continue;
		poll["rows"][id] = row.updateSequence;
		const std::string kind = row.type < GESize ? eventKinds[row.type] : "event";
		if (poll["events"].size() < 40)
			poll["events"].push_back({{"event", row.label}, {"kind", kind}, {"x", row.x}, {"y", row.y},
									  {"count", row.count}, {"tick", row.lastTick}});
		if (reason.empty() && wanted.count(kind) && !poll["ongoing"].contains(id))
			reason = kind;
	}
	const auto after = [&](const auto &list, const char *kind)
	{
		for (const auto &entry : list)
			if (entry.tick > start && reason.empty() && wanted.count(kind))
				reason = kind;
	};
	after(failures, "hive_program_failed");
	after(sightings, "enemy_building_sighted");
	for (const auto &loss : losses)
		if (loss.tick > start && !loss.demolished && reason.empty() && wanted.count("building_destroyed"))
			reason = "building_destroyed";
	if (reason.empty() && now >= target)
		reason = "ticks";
	if (reason.empty() && (gui.gamePaused || gui.simulationHeld()))
		reason = "paused";
	if (reason.empty() && args.value("_final", false))
		reason = "timeout";
	if (reason.empty())
		return {{"pending", true}, {"poll", poll}};
	// Done: what game_world detail alerts would show for the wait, plus rows that have
	// already faded from the HUD.
	Json result = world({{"detail", "alerts"}, {"since_tick", start}});
	std::set<std::string> listed;
	for (const auto &a : result["alerts"])
		listed.insert(a.value("kind", std::string()) + "@" + std::to_string(a.value("x", -1)) + "," +
					  std::to_string(a.value("y", -1)));
	for (auto event : poll["events"])
		if (!listed.count(event.value("kind", std::string()) + "@" + std::to_string(event.value("x", -1)) +
						  "," + std::to_string(event.value("y", -1))))
		{
			const unsigned at = event.at("tick");
			event.erase("tick");
			event["ticks_ago"] = result.value("tick", at) - at;
			result["alerts"].push_back(std::move(event));
		}
	result["reason"] = reason;
	result["from_tick"] = start;
	result["waited_ticks"] = result.value("tick", start) - start;
	if (const auto outcome = gui.announcedOutcome(); !outcome.empty())
		result["outcome"] = outcome;
	result["note"] = reason == "paused"	 ? "The match is paused, so no time passes: game_time resume first."
					 : reason == "ticks"	 ? "The requested ticks passed without an until event."
					 : reason == "timeout" ? "The wall-clock timeout ended the wait before the ticks passed."
					 : reason == "match_end"
						 ? "The game has announced the result; ok/quit the dialog to reach end_game."
						 : "Stopped early for the first until event; alerts lists everything since from_tick.";
	return result;
}
Json LocalCommander::alliance(const Json &args)
{
	const auto flag = [&](const char *name) -> std::optional<bool>
	{
		if (!args.contains(name))
			return std::nullopt;
		return args.at(name).get<bool>();
	};
	const auto allied = flag("allied"), map = flag("share_map"), food = flag("share_food_vision"),
			   market = flag("share_market");
	if (!allied && !map && !food && !market)
		throw std::runtime_error("Nothing to change: pass allied, share_map, share_food_vision or share_market");
	if (const auto error = gui.setAllianceFromAutomation(args.at("team").get<int>(), allied, map, food, market);
		!error.empty())
		throw std::runtime_error(error);
	return {{"queued", true},
			{"note", "Sent as the Alliance dialog's OK; it applies after the next simulation step. "
					 "share_food_vision lets that team's hungry units see your Inns (and so join "
					 "you when yours are happier)."}};
}
Json matchOver(const std::string &tool)
{
	if (tool != "game_wait")
		throw std::runtime_error("The match ended before the operation finished");
	return {{"reason", "match_over"},
			{"note", "The match is no longer running (it ended or was left); ui_inspect shows the "
					 "current screen, usually end_game."}};
}
Json localCall(const std::string &tool, const Json &args)
{
	if (args.contains("_poll") && (!active || !active->available()))
		return matchOver(tool);
	if (tool == "hive_api")
		return apiDocs(args);
	if (!active || !active->available())
	{
		if (tool == "hive_status")
			return {{"available", false},
					{"reason", "No active local human match"},
					{"limits", playerAPI().at("limits")}};
		if (tool == "game_stats")
			throw std::runtime_error("game_stats needs a local match you are playing (your colony) or "
									 "the end_game results screen after a match (every team); this "
									 "screen has neither");
		throw std::runtime_error("Local Hive unavailable: requires a local human match; online, "
								 "replay, spectator and AI takeover modes are unsupported");
	}
	return active->call(tool, args);
}
} // namespace Hive
