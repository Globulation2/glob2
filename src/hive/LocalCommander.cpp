// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocalCommander.h"
#include "AgeGate.h"
#include "UnitConsts.h"
#include <deque>
#include <set>
#include <optional>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include "Building.h"
#include "Unit.h"
#include "GameGUIToolManager.h"
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
#include "render/scene/BuildingCatalogView.h"
#include "GameStats.h"
#include "EngineTiming.h"
#include "stats/MetricCatalog.h"
namespace Hive
{
namespace
{
LocalCommander *active = nullptr; // main thread only
std::atomic<std::uint64_t> sessions{0};
// Every match session this process has had (main thread only): a stale session is
// told apart from one that never existed.
std::set<std::string> &knownSessions()
{
	static std::set<std::string> known;
	return known;
}
std::function<std::string()> &screenSource()
{
	static std::function<std::string()> source;
	return source;
}
const char *const unitNames[] = {"worker", "explorer", "warrior"};
constexpr std::size_t JournalCapacity = 256;
constexpr unsigned AlertTicks = 9000;	// game_world's default alert window: 5 minutes at 1x
constexpr unsigned LogTicks = 54000;	// the journal keeps half an hour of game time
constexpr std::size_t LogEntries = 4096;
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
		 "(default/minimum 25). replace requires expected_revision from inspect. A missing argument is "
		 "reported with every other missing one. Programs use "
		 "names, with server-assigned monotonic revisions; there are no UUIDs, caller-assigned "
		 "revisions or migration source. Install/replacement preflight validates without effects. "
		 "Replacement resets globals and retains pause state; failure preserves the old program. "
		 "Use a unique operation_key per mutation; repeating identical arguments returns the same "
		 "operation result without re-execution. Queued means admission, not gameplay success. "
		 "Standing-order globals persist across successful invocations only. Programs continue "
		 "after MCP disconnect or lease expiry, but programs/globals are discarded on load/exit "
		 "and are never saved. The same session ties game_* building ids to this match: "
		 "game_world and game_place replies carry it. hive_program list gives last_run_tick (the "
		 "latest run's observation tick, null before the first), period_ticks and order_lag_ticks. "
		 "wakeAgent records local_events only: no authentication, billing, "
		 "supervision, model access or paid work. Reads are fixed to the current human player's "
		 "team. tools/list supplies the authoritative argument schemas."},
		{"declarations", "hive_api section \"types\": TypeScript declarations of every record and order"},
		{"limits",
		 {{"work_units_per_run", 250000000},
		  {"data_bytes_per_run", 32 * 1024 * 1024},
		  {"script_data_bytes_per_run", 1024 * 1024},
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

A script declares step(ctx) (main(ctx) is accepted as another name for it).

Queries
- ctx.game.units(filter) / ctx.game.buildings(filter): filter keys are team, type, offset,
  limit (buildings also remembered); any other key is refused. team is a team number or
  "mine", "enemies" or "allies"; type is a number or a name: "worker", "explorer",
  "warrior" for units, a building family name ("inn", "swarm", "warflag") for buildings.
  Records carry typeName (units: "worker"...; buildings: the variant key, "inn.0.site")
  and buildings familyName ("inn"). Your own buildings and flags also report assigned:
  units that have taken the job (workers is what it requests).
- ctx.game.buildings({team: "enemies", remembered: true}): other teams' buildings your
  colony has seen and believes still stand, as last seen (fog of war), each with
  last_seen_tick, familyName, x, y, width, height, team and remembered: true. The same
  memory as game_world's remembered buildings; type filters by family name.
- ctx.game.buildingType(name, {level, site}): the type record a player places for a name
  (level 0's construction site, or the flag), or null when that name has no such level or
  site. An unknown name throws, listing the names, as findPlacement and canPlace do.
- ctx.game.events(sinceTick): your colony's events after sinceTick, oldest first:
  {kind, event, x, y, count, tick}. Kinds: unit_attacked (with unit: the type attacked),
  building_attacked, building_completed, unit_converted_away, unit_converted_to_you,
  building_destroyed, building_demolished, enemy_building_sighted, enemy_building_gone,
  hive_program_failed (with program and paused, its current state) and hive_wake. x/y is
  a tile: the attacked unit's (the attacker's position is not reported), a building's
  top-left, except building_completed, which gives its centre. Keep the last tick in a
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
  corner, no resource or known building), or a flag on that tile. reason names the first
  tile in the way and why: unexplored, one of your buildings, another team's building, a
  pending placement, a resource, mixed terrain or unbuildable terrain.
- ctx.game.map.findPlacement(name, {x, y, radius}) -> {x, y} or null: the nearest such
  top-left, searching rings out to radius (default 12, at most 64), as game_place near does.
  The game still decides when the order arrives.
- Tile records' resource also has key ("wheat", "rice", "fish") from resourceTypes().
- map.delta and map.distance read only x and y of record arguments here, so passing whole
  unit or building records is cheap.

Orders
- {type: "create", building: "inn", x, y}: what a click with that palette entry creates,
  with its default requested units (workers, futureWorkers) and a flag's default range;
  pass any of them to override. buildingType ids still work.
- Order x/y must lie on the map (0..width-1, 0..height-1): an order outside it is listed in
  the result's rejected, with the map size, and not sent. Wrap coordinates yourself
  (((x % w) + w) % w) when you mean the tile across the edge.

Cost
Each run has 250 million work units, a 128 MiB heap and a 5 s deadline. Reading an
explored tile record costs about 1,500 units; scan() returns compact numbers instead. A
for...of loop costs about three times an index loop (for (let i = 0; i < a.length; i++)).
A run converts at most 1 MiB of strings and record keys from JavaScript to the game
(script_data_bytes_per_run: helper arguments, including records you pass, and the returned
output and globals) and 32 MiB of native data (data_bytes_per_run). Crossing either throws
a TypeError that names the call; you can catch it, but the bytes are not given back, so
the run still fails when it returns: treat it as fatal.

Cadence
A program runs at most once per interval_ticks, one run at a time across all programs.
Each run snapshots the colony on a worker thread (20 to 130 ms) and runs the script in a
separate process, so at high game speed many ticks pass between its observation and its
orders, and two busy programs take turns. hive_program list reports each program's
period_ticks (between its last two runs) and order_lag_ticks (observation to orders).)";
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
		{
			// Prose first, so the table of contents summarises it; then one limit per line.
			std::string limits = "Per-run and per-match limits of local Hive scripts (hive_status lists them too).\n";
			const Json api = playerAPI();
			for (const auto &[name, value] : api.at("limits").items())
				limits += "- " + name + ": " + value.dump() + "\n";
			out.push_back({"limits", "Limits", limits});
		}
		// The TypeScript declarations, compiled in from hive.d.ts. Each export becomes its
		// own paragraph, so a search returns the one declaration that matches.
		std::string types = PlayerAPIDeclarations;
		for (std::size_t at = 0; (at = types.find("\nexport ", at)) != std::string::npos; at += 2)
			types.insert(at, "\n");
		out.push_back({"types", "TypeScript declarations (records, queries, orders)", types});
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
		// The first sentence of prose: skip code, tables, lists and blank lines.
		std::string first;
		std::size_t at = 0;
		bool code = false;
		while (at < section.text.size() && first.empty())
		{
			const auto end = section.text.find('\n', at);
			auto line = section.text.substr(at, end == std::string::npos ? std::string::npos : end - at);
			at = end == std::string::npos ? section.text.size() : end + 1;
			if (line.rfind("```", 0) == 0)
				code = !code;
			else if (!code && !line.empty() && line[0] != '|' && line[0] != '{' && line[0] != '-' &&
					 line[0] != ' ' && line.rfind("ctx.", 0) != 0 && line.rfind("export", 0) != 0)
				first = line.substr(0, line.find(". ") == std::string::npos ? line.size() : line.find(". ") + 1);
		}
		if (first.empty())
			first = section.title;
		contents.push_back({{"section", section.id}, {"title", section.title},
							{"summary", first.size() > 140 ? first.substr(0, 140) + "..." : first},
							{"bytes", section.text.size()}});
	}
	return {{"sections", contents},
			{"note", "Browse with hive_api section (one section, with previous/next) or search (words "
					 "that must all appear). Start with contract, helpers and local-control; types has "
					 "the TypeScript declarations of every record and order. Every script declares "
					 "step(ctx) and returns {output, orders}."}};
}
LocalCommander::LocalCommander(GameGUI &g, std::function<std::future<Json>(Json)> start)
	: gui(g), observations(g.game, g.localTeamNo), player(g.localPlayer), team(g.localTeamNo),
	  session(std::to_string(SDL_GetTicksNS()) + "-" + std::to_string(++sessions)),
	  launch(std::move(start))
{
	active = this;
	knownSessions().insert(session);
}
void LocalCommander::checkSession(const Json &args) const
{
	if (!args.contains("session") || args.at("session") == session)
		return;
	const auto given = args.at("session").is_string() ? args.at("session").get<std::string>() : args.at("session").dump();
	if (knownSessions().count(given))
		throw std::runtime_error("Match session expired; inspect hive_status");
	throw std::runtime_error("Unknown session \"" + given.substr(0, 128) +
							 "\": no match had it; hive_status or game_world gives this match's session");
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
		if (p.paused && !p.pausedReason.empty())
			record["paused_reason"] = p.pausedReason + " (fix the script, then hive_program replace or resume)";
		if (!p.lastError.empty())
			record["last_error"] = p.lastError;
		// Due now (or as soon as the worker is free) when the deadline has passed.
		record["nextTick"] = std::max(p.nextTick, tick);
		record["intervalTicks"] = p.definition.at("intervalTicks");
		// The latest run's observation tick (null before its first run), how long its
		// orders took to follow, and the ticks between its last two runs: a run needs real
		// time, so at high speed the period exceeds intervalTicks.
		record["last_run_tick"] = p.runs ? Json(p.lastRunTick) : Json(nullptr);
		if (p.runs)
			record["order_lag_ticks"] = p.lastSubmissionTick - p.lastRunTick;
		if (p.periodTicks)
			record["period_ticks"] = p.periodTicks;
		record["runs"] = p.runs;
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
	{
		programs.erase(it);
		return;
	}
	it->second.paused = action == "pause";
	if (action == "resume")
		it->second.pausedReason.clear();
	it->second.nextTick = tick + 25;
	it->second.generation = ++generation;
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
		{
			auto over = matchOver(tool);
			if (args.at("_poll").contains("began"))
				over["from_tick"] = args.at("_poll").at("start");
			return over;
		}
		// game_time step with wait is a game_wait for the stepped ticks.
		return tool == "game_wait" || tool == "game_time" ? wait(args) : awaitResult(args);
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
	// Every missing argument at once, rather than the first one a lookup trips on.
	if (tool == "hive_result" || tool == "hive_run" || tool == "hive_program")
	{
		const auto action = tool == "hive_program" && args.contains("action") && args.at("action").is_string()
								? args.at("action").get<std::string>()
								: std::string();
		std::vector<const char *> needed;
		if (tool == "hive_result")
			needed = {"session", "operation_key"};
		else if (tool == "hive_run")
			needed = {"session", "operation_key", "source"};
		else if (action == "inspect")
			needed = {"name"};
		else if (action == "install" || action == "replace")
			needed = {"session", "operation_key", "name", "source"};
		else if (action == "pause" || action == "resume" || action == "cancel")
			needed = {"session", "operation_key", "name"};
		else if (action != "list")
			throw std::runtime_error("hive_program action must be list, inspect, install, replace, pause, resume "
									 "or cancel");
		std::string missing;
		for (const char *field : needed)
			if (!args.contains(field) || args.at(field).is_null())
				missing += (missing.empty() ? "" : ", ") + std::string(field);
		if (!missing.empty())
			throw std::runtime_error(tool + (action.empty() ? std::string() : " " + action) + " is missing " + missing +
									 (missing.find("session") != std::string::npos ? " (hive_status gives the session)"
																				   : std::string()));
	}
	// The installed programs' names, for errors about one that is not installed.
	const auto installed = [&]
	{
		std::string names;
		for (const auto &[name, p] : programs)
			names += (names.empty() ? "" : ", ") + name;
		return names.empty() ? std::string("none is installed") : "installed: " + names;
	};
	if (tool == "hive_result")
	{
		checkSession(args);
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
	checkSession(args);
	if (kind == "list")
	{
		// The list says how each program is doing; its last output, events and description
		// come with inspect, so a colony with many programs stays a short read.
		auto list = standingOrders();
		for (auto &program : list)
		{
			program.erase("description");
			auto &recent = program["recent"];
			if (recent.is_object())
				for (const char *heavy : {"output", "local_events", "orders", "rejected"})
					recent.erase(heavy);
		}
		return {{"session", session}, {"programs", list}, {"note", "hive_program inspect gives a program's source, last output and events"}};
	}
	if (kind == "inspect")
	{
		auto it = programs.find(args.at("name").get<std::string>());
		if (it == programs.end())
			throw std::runtime_error("No program named \"" + args.at("name").get<std::string>() + "\" (" + installed() + ")");
		return {{"session", session},
				{"program", it->second.definition},
				{"paused", it->second.paused},
				{"recent", it->second.recent}};
	}
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
			const auto &interval = args.contains("interval_ticks") ? args.at("interval_ticks") : Json(25);
			if (!interval.is_number_integer() || interval.get<std::int64_t>() < 25 || interval.get<std::int64_t>() > 1000000)
				throw std::runtime_error("interval_ticks must be a whole number 25..1000000 (got " + interval.dump() + ")");
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
			remember(key, {{"status", "failed"}, {"error", "No program named \"" + name + "\" (" + installed() + ")"}});
		else
		{
			change(name, kind);
			remember(key, {{"status", "completed"}, {"submitted", false}});
		}
	}
	// An install over an existing name, or a stale replace, fails now: its preflight
	// would run the whole script only to be refused. (finish checks again, since an
	// earlier queued operation can still change the program.)
	else if (const auto conflict = kind == "install" || kind == "replace" ? programConflict(kind, args) : std::string();
			 !conflict.empty())
		remember(key, {{"status", "failed"}, {"submitted", false}, {"error", conflict}});
	else
		queued.push_back({key, args});
	if (args.value("wait", false))
		return awaitResult({{"session", session}, {"operation_key", key}});
	return journal.at(key);
}
std::string LocalCommander::programConflict(const std::string &kind, const Json &request) const
{
	const auto name = request.value("name", std::string());
	const auto old = programs.find(name);
	const auto revision = [&] { return old->second.definition.at("revision").dump(); };
	if (kind == "install")
	{
		if (old != programs.end())
			return "A program named \"" + name + "\" is already installed (revision " + revision() +
				   "): use action replace with expected_revision " + revision() + ", or cancel it first";
		if (programs.size() >= 8)
			return "Eight programs are already installed, the most a colony can run: cancel one first "
				   "(hive_program list names them)";
		return {};
	}
	if (old == programs.end())
		return "No program named \"" + name + "\" is installed, so there is nothing to replace: use action install "
			   "(hive_program list names the installed ones)";
	if (!request.contains("expected_revision"))
		return "replace needs expected_revision: \"" + name + "\" is at revision " + revision();
	if (old->second.definition.at("revision") != request.at("expected_revision"))
		return "expected_revision " + request.at("expected_revision").dump() + " is stale: \"" + name +
			   "\" is now at revision " + revision() + "; replace with expected_revision " + revision() +
			   " if you still mean to replace that version";
	return {};
}
void LocalCommander::record(Logged entry)
{
	if (entry.kind == "building_destroyed")
		++destroyedTotal;
	if (entry.kind == "building_demolished")
		++demolishedTotal;
	eventLog.push_back(std::move(entry));
	const unsigned newest = eventLog.back().tick;
	while (eventLog.size() > LogEntries || (!eventLog.empty() && eventLog.front().tick + LogTicks < newest))
		eventLog.pop_front();
}
void LocalCommander::recordFeed()
{
	// Each new report the HUD coalesced into a row (attacks, completions, conversions).
	for (const auto &row : gui.gameEvents().rows())
	{
		auto &seen = rowSeen[row.id];
		if (seen == row.updateSequence)
			continue;
		seen = row.updateSequence;
		Logged entry{row.type < GESize ? eventKinds[row.type] : "event", row.label, row.x, row.y, -1, row.lastTick};
		// What the report is about: the unit type attacked, the other team of a
		// conversion, the building's family.
		if (row.type == GEUnitUnderAttack)
			entry.subject = int(row.subject);
		else if (row.type == GEUnitLostConversion || row.type == GEUnitGainedConversion)
			entry.team = int(row.subject);
		else if (row.type == GEBuildingUnderAttack || row.type == GEBuildingCompleted)
			if (const auto &types = gui.drawnScene().buildingTypes)
				if (const auto *type = BuildingCatalogView(*types).get(int(row.subject)))
					entry.what = type->type;
		record(std::move(entry));
	}
	if (rowSeen.size() > 2048)
		rowSeen.clear();
}
Json LocalCommander::groupedAlerts(unsigned since, unsigned now, std::size_t cap) const
{
	// One group per kind, team and program over the whole window: a long fight is one
	// line with n reports and the box they came from, not one group per area.
	struct Group
	{
		Json alert;
		unsigned first = 0, last = 0, n = 0;
		int minX = 0, minY = 0, maxX = -1, maxY = -1;
		std::map<std::string, int> what, units;
		std::set<std::string> messages;
	};
	std::map<std::string, Group> groups;
	for (const auto &e : eventLog)
	{
		if (e.tick <= since && since)
			continue;
		const auto key = e.kind + "|" + std::to_string(e.team) + "|" + e.program;
		auto &g = groups[key];
		if (!g.n)
		{
			g.first = e.tick;
			g.alert = {{"kind", e.kind}};
			if (e.team >= 0)
				g.alert["team"] = e.team;
			if (!e.program.empty())
			{
				g.alert["program"] = e.program;
				// The program's state now, not when it failed: replace or resume clears it.
				const auto program = programs.find(e.program);
				if (program == programs.end())
					g.alert["program_state"] = "removed";
				else
					g.alert["paused"] = program->second.paused;
			}
		}
		++g.n;
		if (e.tick >= g.last)
		{
			g.last = e.tick;
			g.alert["event"] = e.event; // the latest report's message
			if (e.x >= 0)
				g.alert["x"] = e.x, g.alert["y"] = e.y;
		}
		g.messages.insert(e.event);
		if (e.x >= 0)
		{
			if (g.maxX < 0)
				g.minX = g.maxX = e.x, g.minY = g.maxY = e.y;
			g.minX = std::min(g.minX, e.x), g.maxX = std::max(g.maxX, e.x);
			g.minY = std::min(g.minY, e.y), g.maxY = std::max(g.maxY, e.y);
		}
		if (!e.what.empty())
			++g.what[e.what];
		if (e.subject >= 0 && e.subject < 3)
			++g.units[unitNames[e.subject]];
	}
	std::vector<Group *> ordered;
	for (auto &[key, g] : groups)
		ordered.push_back(&g);
	std::sort(ordered.begin(), ordered.end(), [](const Group *a, const Group *b) { return a->last > b->last; });
	Json alerts = Json::array();
	for (std::size_t i = 0; i < ordered.size() && i < cap; ++i)
	{
		auto &g = *ordered[i];
		g.alert["n"] = g.n;
		if (g.maxX >= 0 && (g.minX != g.maxX || g.minY != g.maxY))
			g.alert["area"] = {{"x", g.minX}, {"y", g.minY}, {"width", g.maxX - g.minX + 1}, {"height", g.maxY - g.minY + 1}};
		if (!g.what.empty() && g.messages.size() > 1)
			g.alert["buildings"] = g.what;
		if (!g.units.empty() && g.messages.size() > 1)
			g.alert["units"] = g.units;
		g.alert["first_tick"] = g.first;
		g.alert["last_tick"] = g.last;
		g.alert["ticks_ago"] = now > g.last ? now - g.last : 0;
		alerts.push_back(std::move(g.alert));
	}
	if (ordered.size() > cap)
		alerts.push_back({{"kind", "more"}, {"event", std::to_string(ordered.size() - cap) + " older alert groups; pass since_tick to narrow"}});
	return alerts;
}
Json LocalCommander::placeByName(Json order)
{
	// A building reference copied from another team's record must never reach one of
	// yours with the same id: references carry their team, and only yours are accepted.
	if (order.is_object() && order.contains("building") && order.at("building").is_object())
	{
		const auto &ref = order.at("building");
		if (ref.contains("team") && ref.at("team").is_number_integer() && ref.at("team").get<int>() != team)
			throw std::runtime_error("building is team " + std::to_string(ref.at("team").get<int>()) +
									 "'s, not yours: orders can only change your own buildings");
		if (order.value("type", std::string()) == "delete" && ref.contains("id") && ref.at("id").is_number_integer())
			demolitionOrders.insert(ref.at("id").get<int>()); // reported as demolished, not destroyed
	}
	// Order coordinates are canonical map tiles, as the contract says and game_place
	// requires: one outside the map is refused (and listed in rejected), never moved.
	if (order.is_object())
	{
		const int width = gui.game.map.getW(), height = gui.game.map.getH();
		for (const auto &[field, size] : {std::pair{"x", width}, std::pair{"y", height}})
			if (order.contains(field) && order.at(field).is_number() && size > 0)
			{
				const auto v = order.at(field).get<double>();
				if (v < 0 || v >= size)
					throw std::runtime_error(
						std::string(field) + " " + order.at(field).dump() + " is outside the " + std::to_string(width) + "x" +
						std::to_string(height) + " map (x 0.." + std::to_string(width - 1) + ", y 0.." +
						std::to_string(height - 1) + "); wrap it yourself (((v % size) + size) % size) to mean "
						"the tile across the edge");
			}
	}
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
LocalCommander::Annotation LocalCommander::annotation()
{
	// Callers hold the simulation boundary. Only the colony's own information is added,
	// plus what any building's type says (its name and footprint).
	if (!buildingTypeShapes)
	{
		Json shapes = Json::array();
		for (std::size_t i = 0; i < gui.game.buildingsTypes.size(); ++i)
			if (const auto *type = gui.game.buildingsTypes.get(i))
				shapes.push_back({{"name", type->type}, {"width", type->width}, {"height", type->height}});
		buildingTypeShapes = std::make_shared<const Json>(std::move(shapes));
	}
	Annotation out{team, buildingTypeShapes, Json::object(), Json::array(), {}, Json::array()};
	// Units that took the job, beside the units it requests; an Inn's units inside
	// and the happiness it offers, as the building panel shows.
	for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
	{
		auto *building = gui.game.teams[team]->myBuildings[slot];
		if (!building)
			continue;
		Json own = {{"assigned", int(building->unitsWorking.size())}};
		if (building->type->canFeedUnit && !building->type->isBuildingSite)
			own["inside"] = int(building->unitsInside.size()), own["happiness"] = building->availableHappynessLevel();
		out.own[std::to_string(building->gid)] = std::move(own);
	}
	// Footprints already claimed but not yet sites: game_place requests and script creates.
	for (const auto &p : placements)
		out.pending.push_back({{"x", p.x}, {"y", p.y}, {"width", p.width}, {"height", p.height}});
	for (const auto &c : scriptCreates)
		if (const auto *type = gui.game.buildingsTypes.get(c.type); type && !type->isVirtual)
			out.pending.push_back({{"x", c.x}, {"y", c.y}, {"width", type->width}, {"height", type->height}});
	// ctx.game.events(): the colony's journal (what game_wait reports), with ticks.
	recordFeed();
	out.events.assign(eventLog.begin(), eventLog.end());
	// A failed program's paused flag says what it is now: replace or resume clears it.
	for (auto &e : out.events)
		if (!e.program.empty() && e.kind == "hive_program_failed")
		{
			const auto program = programs.find(e.program);
			e.paused = program != programs.end() && program->second.paused;
		}
	// Other teams' buildings the colony has seen, as last seen (the game_world memory).
	const Uint32 enemies = gui.game.teams[team]->enemies;
	for (const auto &[gid, record] : seenBuildings)
	{
		Json entry = record;
		entry["familyName"] = record.value("type", std::string());
		entry.erase("type");
		entry["remembered"] = true;
		entry["enemy"] = (enemies & Team::teamNumberToMask(record.value("team", 0))) != 0;
		out.remembered.push_back(std::move(entry));
	}
	// Losses and sightings need the game_world memory; scripts read events, so start it.
	memoryTracked = true;
	return out;
}
void LocalCommander::annotate(Json &snapshot, const Annotation &annotation)
{
	const auto &shapes = *annotation.shapes;
	for (auto &u : snapshot.at("units"))
		if (const int type = u.value("type", -1); type >= 0 && type < 3)
			u["typeName"] = unitNames[type];
	for (auto &b : snapshot.at("buildings"))
	{
		const auto type = b.value("type", -1);
		if (type >= 0 && std::size_t(type) < shapes.size())
			for (const auto &[key, value] : shapes[std::size_t(type)].items())
				b[key] = value;
		// Names beside the numbers: the family ("inn") and the variant key ("inn.0.site").
		if (b.contains("name"))
			b["familyName"] = b.at("name");
		if (b.contains("key"))
			b["typeName"] = b.at("key");
		else if (b.contains("name"))
			b["typeName"] = b.at("name");
		if (b.value("team", -1) != annotation.team)
			continue;
		const auto own = annotation.own.find(std::to_string(b.value("id", 0)));
		if (own != annotation.own.end())
			for (const auto &[key, value] : own->items())
				b[key] = value;
	}
	snapshot["pending"] = annotation.pending;
	snapshot["remembered"] = annotation.remembered;
	Json events = Json::array();
	for (const auto &e : annotation.events)
	{
		Json event = {{"kind", e.kind}, {"event", e.event}, {"count", 1}, {"tick", e.tick}};
		if (e.x >= 0)
			event["x"] = e.x, event["y"] = e.y;
		if (e.team >= 0)
			event["team"] = e.team;
		if (e.subject >= 0 && e.subject < 3)
			event["unit"] = unitNames[e.subject];
		if (!e.what.empty())
			event["building"] = e.what;
		if (!e.program.empty())
			event["program"] = e.program, event["paused"] = e.paused;
		events.push_back(std::move(event));
	}
	std::stable_sort(events.begin(), events.end(),
					 [](const Json &a, const Json &b) { return a.value("tick", 0u) < b.value("tick", 0u); });
	snapshot["events"] = std::move(events);
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
	Json rejected = Json::array();
	std::size_t sent = 0;
	std::string stillPaused; // a replaced program keeps its pause
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
		// The budget is the sum of every host read in one run; say what fits. Inside a call
		// the error reaches the script as a TypeError, so look past that prefix, and put the
		// explanation at the end of the first line, before any stack trace.
		const auto explain = [&](const char *limit, const char *hint)
		{
			const auto at = error.find(limit);
			if (at == std::string::npos || error.find('\n') < at)
				return;
			const auto lineEnd = error.find('\n');
			error.insert(lineEnd == std::string::npos ? error.size() : lineEnd, hint);
		};
		explain("Native script data exceeds limit",
				" (data_bytes_per_run: a run may convert at most 32 MiB of data between the game and "
				"JavaScript: call large queries such as buildingTypes() once per run, not inside a loop)");
		if (error.find("Native script data exceeds limit") == std::string::npos)
			explain("Script data exceeds limit",
					" (script_data_bytes_per_run: 1 MiB of strings and record keys per run, counted "
					"cumulatively over every helper call's arguments, records passed to helpers included, "
					"and the returned output and globals; pass numbers or small {x, y} records to helpers in "
					"loops. Catching the error does not give the bytes back)");
		if (error.rfind("JavaScript native resource limit exhausted", 0) == 0)
			error += " (the script ran out of its 128 MiB JavaScript heap or its call stack: read "
					 "smaller areas or scan() fields instead of region() records, keep globals small, "
					 "and avoid deep recursion)";
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
		{
			// Each order stands alone: one the game no longer accepts (its building was
			// destroyed since the script looked, a bad descriptor) is listed in rejected and
			// the others are still sent. A script error is what fails a run, not stale state.
			const auto descriptors = result.value("orders", Json::array());
			for (std::size_t i = 0; i < descriptors.size(); ++i)
				try
				{
					const auto prepared = placeByName(descriptors[i]);
					orders.push_back(Script::order(gui.game, team, value(prepared)));
					if (prepared.value("type", std::string()) == "create")
						scriptCreates.push_back({prepared.value("x", 0), prepared.value("y", 0),
												 prepared.value("buildingType", -1), tick});
				}
				catch (const std::exception &e)
				{
					const auto &d = descriptors[i];
					rejected.push_back({{"order", i},
										{"type", d.is_object() ? d.value("type", std::string("?")) : std::string("?")},
										{"error", e.what()}});
				}
			while (!scriptCreates.empty() && scriptCreates.front().tick + 300 < tick)
				scriptCreates.pop_front();
		}
		if (orders.size() > 32)
			throw std::runtime_error("Too many orders");
		if (kind == "install" || kind == "replace")
		{
			const auto id = current.request.at("name").get<std::string>();
			auto old = programs.find(id);
			if (const auto conflict = programConflict(kind, current.request); !conflict.empty())
				throw std::runtime_error(conflict);
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
			if (old != programs.end())
			{
				// The same program by name: its run history carries over.
				p.runs = old->second.runs;
				p.lastRunTick = old->second.lastRunTick;
				p.lastSubmissionTick = old->second.lastSubmissionTick;
				p.periodTicks = old->second.periodTicks;
			}
			if (p.paused)
			{
				p.pausedReason = old->second.pausedReason;
				stillPaused = p.pausedReason.empty() ? "paused" : "paused (" + p.pausedReason + ")";
			}
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
			sent = orders.size();
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
				   {"order_count", sent},
				   {"output", result.value("output", Json(nullptr))}};
	if (!error.empty())
		record["error"] = error; // absent on success
	if (!rejected.empty())
		record["rejected"] = rejected;
	if (!stillPaused.empty())
		record["note"] = "Replaced, but the program is still " + stillPaused + ": hive_program resume runs it";
	if (kind != "install" && kind != "replace")
		record["local_events"] = result.value("wakes", Json::array());
	// ctx.wakeAgent({key, reason}): reaches the agent as a hive_wake alert (game_wait until).
	for (const auto &w : result.value("wakes", Json::array()))
		if (w.is_object())
		{
			Logged wake{"hive_wake", w.value("key", std::string()) + ": " + w.value("reason", std::string()), -1, -1, -1, tick};
			wake.program = name.empty() ? current.key : name;
			this->record(std::move(wake));
		}
	if (!name.empty() && program != programs.end() &&
		program->second.generation == currentGeneration)
	{
		program->second.recent = record;
		auto &p = program->second;
		if (p.runs && observationTick > p.lastRunTick)
			p.periodTicks = observationTick - p.lastRunTick;
		p.lastRunTick = observationTick;
		p.lastSubmissionTick = tick;
		++p.runs;
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
			{
				p.paused = true;
				p.pausedReason = error.substr(0, error.find('\n'));
			}
			p.lastError = error.substr(0, error.find('\n'));
			failures.push_back({name, error.substr(0, error.find('\n')), tick, p.paused});
			{
				Logged failed{"hive_program_failed", "Hive program " + name + (p.paused ? " paused: " : " retrying: ") +
															 error.substr(0, error.find('\n')),
							  -1, -1, -1, tick, name, p.paused};
				this->record(std::move(failed));
			}
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
	// The HUD's rows coalesce and fade: note each new report as it arrives.
	recordFeed();
	applyPendingPriorities();
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
				const auto t0 = SDL_GetTicksNS();
				// The simulation boundary: bring the colony's memory up to date and copy what
				// the snapshot needs. The real worker builds the snapshot on its own thread;
				// an injected one (tests) gets a complete snapshot built here.
				auto input = std::make_shared<CaptureInput>(prepareCapture(observations, gui.game, team));
				auto extra = std::make_shared<Annotation>(annotation());
				Json request = {{"source", current.request.at("source")}};
				observationTick = tick;
				if (!currentProgram.empty())
				{
					auto &p = programs.at(currentProgram);
					request["state"] = p.state;
					request["initialized"] = p.initialized;
					request["random"] = p.random;
				}
				if (std::getenv("GLOB2_HIVE_TIMING"))
					fprintf(stderr, "HIVE_TIMING main_thread_capture_ms=%.2f\n", (SDL_GetTicksNS() - t0) / 1e6);
				if (!current.key.empty())
					remember(
						current.key,
						{{"status", "running"}, {"submitted", false}, {"observation_tick", tick}});
				auto build = [input, extra, cache = launch ? nullptr : tileCache](Json request) mutable
				{
					const auto t1 = SDL_GetTicksNS();
					Json snapshot = buildSnapshot(*input, cache.get());
					annotate(snapshot, *extra);
					request["snapshot"] = std::move(snapshot);
					if (std::getenv("GLOB2_HIVE_TIMING"))
						fprintf(stderr, "HIVE_TIMING worker_snapshot_ms=%.2f\n", (SDL_GetTicksNS() - t1) / 1e6);
					return request;
				};
				worker = launch ? launch(build(std::move(request)))
								: startWorker([build, request = std::move(request)]() mutable { return build(std::move(request)); });
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
	auto result = worldReply(args);
	if (!args.contains("include"))
	{
		// The summary leaves out what rarely changes: the placeable names (game_place lists
		// them when a name is wrong; detail full or include placeable has them) and the note
		// on coordinates (the tool description says it).
		if (args.value("detail", std::string("summary")) == "summary")
		{
			result.erase("placeable");
			result.erase("note");
		}
		return result;
	}
	// Only the sections asked for, plus what identifies the reply. The note is left out:
	// a caller that filters knows what it asked for.
	Json kept = Json::object();
	for (const char *always : {"tick", "session", "team"})
		if (result.contains(always))
			kept[always] = std::move(result[always]);
	static const std::set<std::string> sections = {
		"status", "alerts", "losses", "units", "forces", "buildings", "placements", "colony", "others",
		"counts", "resources", "resources_scope", "resources_note", "placeable", "map", "view", "note"};
	for (const auto &section : args.at("include"))
	{
		const auto name = section.is_string() ? section.get<std::string>() : section.dump();
		if (!sections.contains(name))
		{
			std::string valid;
			for (const auto &known : sections)
				valid += (valid.empty() ? "" : ", ") + known;
			throw std::runtime_error("Invalid argument: include has no section \"" + name + "\" (sections: " + valid +
									 "; brief has status, alerts, losses, units, forces, buildings, placements)");
		}
		if (result.contains(name))
			kept[name] = std::move(result[name]);
	}
	return kept;
}
Json LocalCommander::worldReply(const Json &args)
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
			// The colony's extent and centre (its Swarm's when it has one), from every own
			// building whatever the filters: the resources section searches around it.
			int cx = -1, cy = -1, minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
			bool centredOnSwarm = false;
			for (const auto &b : buildings)
			{
				if (b.value("team", -1) == team)
				{
					const auto *type = types.get(b.value("type", 0));
					const std::string key = b.value("key", std::string());
					if (type && !type->isVirtual)
					{
						const int bx = b.value("x", 0), by = b.value("y", 0);
						const bool swarm = key.substr(0, key.find('.')) == "swarm";
						if (cx < 0 || (swarm && !centredOnSwarm))
							cx = bx + type->width / 2, cy = by + type->height / 2, centredOnSwarm = swarm;
						minX = std::min(minX, bx), minY = std::min(minY, by);
						maxX = std::max(maxX, bx + type->width - 1), maxY = std::max(maxY, by + type->height - 1);
					}
					// Every own building is this match's: game_building accepts its reference.
					reportedRefs.insert({b.value("id", 0), b.value("generation", 0u)});
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
			// Resources the colony knows of, by type: within resource_radius of its buildings
			// (flags excluded), or inside area when one is given. Each type has its tile count
			// and amount, the nearest tile to the colony's centre, and its largest fields:
			// tiles of that resource touching each other (diagonals count), as the colony
			// last saw them (fog of war), never their live state.
			Json resources = Json::object(), resourceScope = Json::object();
			std::string resourceNote;
			if (!brief)
			{
				const int w = gui.game.map.getW(), h = gui.game.map.getH();
				int sx = 0, sy = 0, spanX = 0, spanY = 0;
				if (args.contains("area"))
				{
					sx = ((ax % w) + w) % w, sy = ((ay % h) + h) % h;
					spanX = std::clamp(aw, 0, w), spanY = std::clamp(ah, 0, h);
					if (cx < 0)
						cx = (sx + spanX / 2) % w, cy = (sy + spanY / 2) % h;
					resourceScope = {{"area", {{"x", ax}, {"y", ay}, {"width", aw}, {"height", ah}}}};
				}
				else if (cx >= 0)
				{
					const int radius = std::clamp(args.value("resource_radius", 24), 0, std::max(w, h));
					sx = (((minX - radius) % w) + w) % w, sy = (((minY - radius) % h) + h) % h;
					spanX = std::min(w, maxX - minX + 1 + 2 * radius), spanY = std::min(h, maxY - minY + 1 + 2 * radius);
					resourceScope = {{"resource_radius", radius},
									 {"around", "your buildings (flags excluded)"},
									 {"from", {{"x", sx}, {"y", sy}, {"width", spanX}, {"height", spanY}}}};
				}
				if (cx >= 0)
					resourceScope["distances_from"] = {{"x", cx}, {"y", cy}, {"what", centredOnSwarm ? "your swarm" : args.contains("area") && minX > maxX ? "the area's centre" : "your first building"}};
				const auto resourceTypes = json(observations.query("resourceTypes", {}));
				// Each scanned tile's resource type, or -1, and its amount.
				std::vector<int> kind(std::size_t(spanX) * spanY, -1), amounts(kind.size(), 0);
				for (int oy = 0; oy < spanY; ++oy)
					for (int ox = 0; ox < spanX; ++ox)
					{
						const auto cell = observations.cell((sx + ox) % w, (sy + oy) % h);
						if (!cell.known || cell.resource == 65535 || !cell.amount || cell.resource >= resourceTypes.size())
							continue;
						kind[std::size_t(oy) * spanX + ox] = cell.resource;
						amounts[std::size_t(oy) * spanX + ox] = int(std::min<unsigned>(cell.amount, 65535u));
					}
				const auto distance = [&](int x, int y)
				{
					const int dx = std::min(std::abs(x - cx), w - std::abs(x - cx));
					const int dy = std::min(std::abs(y - cy), h - std::abs(y - cy));
					return dx + dy;
				};
				struct Field
				{
					int tiles = 0, amount = 0, x = 0, y = 0, distance = 1 << 30;
				};
				std::map<int, std::vector<Field>> fields; // by resource type
				std::vector<char> visited(kind.size(), 0);
				std::vector<std::pair<int, int>> stack;
				for (int oy = 0; oy < spanY; ++oy)
					for (int ox = 0; ox < spanX; ++ox)
					{
						const std::size_t start = std::size_t(oy) * spanX + ox;
						if (kind[start] < 0 || visited[start])
							continue;
						const int resource = kind[start];
						Field field;
						visited[start] = 1;
						stack.assign(1, {ox, oy});
						while (!stack.empty())
						{
							const auto [px, py] = stack.back();
							stack.pop_back();
							const std::size_t i = std::size_t(py) * spanX + px;
							const int x = (sx + px) % w, y = (sy + py) % h;
							++field.tiles;
							field.amount += amounts[i];
							if (const int d = distance(x, y); d < field.distance)
								field.distance = d, field.x = x, field.y = y;
							for (int dy = -1; dy <= 1; ++dy)
								for (int dx = -1; dx <= 1; ++dx)
								{
									int nx = px + dx, ny = py + dy;
									// Across the map's edge only when the scan covers the whole torus.
									if (spanX == w)
										nx = (nx + w) % w;
									if (spanY == h)
										ny = (ny + h) % h;
									if (nx < 0 || ny < 0 || nx >= spanX || ny >= spanY)
										continue;
									const std::size_t n = std::size_t(ny) * spanX + nx;
									if (kind[n] == resource && !visited[n])
									{
										visited[n] = 1;
										stack.emplace_back(nx, ny);
									}
								}
						}
						fields[resource].push_back(field);
					}
				const std::size_t fieldCount = std::clamp(args.value("resource_fields", 3), 1, 10);
				for (auto &[resource, list] : fields)
				{
					int tiles = 0, amount = 0;
					const Field *nearest = &list.front();
					for (const auto &f : list)
					{
						tiles += f.tiles, amount += f.amount;
						if (f.distance < nearest->distance)
							nearest = &f;
					}
					Json entry = {{"tiles", tiles},
								  {"amount", amount},
								  {"nearest", {{"x", nearest->x}, {"y", nearest->y}, {"distance", nearest->distance}}},
								  {"field_count", list.size()}};
					// Largest fields first; of equal size, the nearest.
					std::sort(list.begin(), list.end(), [](const Field &a, const Field &b)
							  { return a.tiles != b.tiles ? a.tiles > b.tiles : a.distance < b.distance; });
					Json best = Json::array();
					for (std::size_t i = 0; i < list.size() && i < fieldCount; ++i)
						best.push_back({{"x", list[i].x},
										{"y", list[i].y},
										{"tiles", list[i].tiles},
										{"amount", list[i].amount},
										{"distance", list[i].distance}});
					entry["fields"] = std::move(best);
					resources[resourceTypes[std::size_t(resource)].value("key", std::string("resource"))] = std::move(entry);
				}
				if (!args.contains("area") && cx < 0)
					resourceNote = "You have no buildings to search around: pass area to look somewhere";
				else if (resources.empty())
					resourceNote = "None seen yet " + std::string(args.contains("area") ? "in this area" : "within resource_radius of your buildings") +
								   ": explore (explorers, or an exploration flag) or pass a larger " +
								   (args.contains("area") ? "area" : "resource_radius");
				else
					resourceNote = "fields: each a group of touching tiles of one resource, largest first; its x/y is "
								   "the field's tile nearest your colony. Only tiles your colony has seen count.";
			}
			// Alerts from the colony's journal, grouped (a skirmish is one alert with n
			// reports), newest first: the last AlertTicks by default, or after since_tick.
			const auto now = gui.game.stepCounter;
			recordFeed();
			const unsigned window = since ? since : (now > AlertTicks ? now - AlertTicks : 0);
			Json alerts = groupedAlerts(window, now, 25), lostBuildings = Json::array();
			for (const auto &e : eventLog)
				if ((e.tick > window || !window) && e.kind == "building_destroyed")
					lostBuildings.push_back({{"type", e.event.substr(e.event.find(": ") + 2)}, {"x", e.x}, {"y", e.y},
											 {"ticks_ago", now > e.tick ? now - e.tick : 0}});
			// Conversions are rate-limited as events; the colony's counter is exact.
			const int convertedAway = gui.game.teams[team]->unitConversionLost;
			Json lossSummary = {{"buildings_destroyed", lostBuildings},
								{"buildings_destroyed_total", destroyedTotal},
								{"buildings_demolished_total", demolishedTotal},
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
			const auto chance = gui.localWinChancePercent();
			// units_free is counted now; hud_sampled is the top bar's text, from statistics
			// sampled every few seconds, so it can lag.
			Json status = {{"units_free", unitStatus()},
						   {"win_chance", chance ? Json(*chance) : Json(nullptr)},
						   {"prestige", bar[3]},
						   {"hud_sampled", {{"workers", bar[0]}, {"explorers", bar[1]}, {"warriors", bar[2]},
											{"win_chance", bar[4]}}}};
			Json visibleUnits = Json::object();
			for (const auto &[k, n] : otherUnits)
				visibleUnits[k] = n;
			const Json forces = {{"your_warriors_by_attack_level", ownWarriors},
								 {"your_hungry_units", hungry},
								 {"your_injured_units", injured}};
			if (brief)
			{
				result = {{"tick", gui.game.stepCounter}, {"session", session}, {"team", team}, {"status", status},
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
					  {"session", session},
					  {"team", team},
					  {"map", {{"width", gui.game.map.getW()}, {"height", gui.game.map.getH()}}},
					  {"colony", {{"buildings", own}, {"units", ownUnits}}},
					  {"others", {{"buildings", others}, {"units_by_team_and_type", visibleUnits}}},
					  {"counts", counts},
					  {"status", status},
					  {"forces", forces},
					  {"resources", resources},
					  {"resources_scope", resourceScope},
					  {"resources_note", resourceNote},
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
	result["note"] = "Tiles are map coordinates; x/y of a building is its top-left tile (an alert's x/y is "
					 "its latest report's tile; area boxes them all). Only what your colony can see is listed. "
					 "Place with game_place, look with game_view; ids are this match's (session).";
	return result;
}
Json LocalCommander::place(const Json &args)
{
	checkSession(args);
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
	// Initial settings, sent in the create order itself in place of a click's defaults.
	BuildingCreateSettings settings;
	const auto whole = [&](const char *field, int minimum, int maximum) -> std::optional<int>
	{
		if (!args.contains(field))
			return std::nullopt;
		const auto &v = args.at(field);
		if (!v.is_number_integer() || v.get<std::int64_t>() < minimum || v.get<std::int64_t>() > maximum)
			throw std::runtime_error(std::string(field) + " must be a whole number " + std::to_string(minimum) + ".." +
									 std::to_string(maximum) + " (got " + v.dump() + ")");
		return v.get<int>();
	};
	settings.workers = whole("workers", 0, MAX_BUILDING_WORKER_REQUEST);
	settings.builders = whole("builders", 0, MAX_BUILDING_WORKER_REQUEST);
	settings.range = whole("range", 0, 32767);
	std::optional<int> priority;
	if (args.contains("priority"))
	{
		const auto &p = args.at("priority");
		const auto text = p.is_string() ? p.get<std::string>() : std::string();
		// As game_building: medium is the default a click gives ("normal" is accepted too).
		if (text != "low" && text != "medium" && text != "normal" && text != "high")
			throw std::runtime_error("priority must be \"low\", \"medium\" or \"high\" (got " + p.dump() + ")");
		priority = text == "low" ? -1 : text == "high" ? 1 : 0;
	}
	const bool customised = settings.workers || settings.builders || settings.range;
	std::string requestedRefusal;
	if (const auto error = gui.placeFromAutomation(name, x, y, radius, &at, reserved, &size,
												   customised ? &settings : nullptr, &requestedRefusal);
		!error.empty())
		throw std::runtime_error(error);
	const auto shortName = name.substr(0, name.find('.'));
	placements.push_back({shortName, at.first, at.second, size.first, size.second, gui.game.stepCounter});
	while (placements.size() > 16)
		placements.pop_front();
	Json result = {{"queued", true},
				   {"session", session},
				   {"building", name},
				   {"x", at.first},
				   {"y", at.second},
				   {"note", "Queued like a click: the site or flag appears after the next simulation step, "
							"so it has no id yet. game_world (detail brief) lists it under placements with "
							"its id and generation once it does (or says it never did); change its settings "
							"with game_building."}};
	// near: how far from the requested top-left it went, and why that spot was refused.
	if (at.first != x || at.second != y)
	{
		const int w = gui.game.map.getW(), h = gui.game.map.getH();
		const auto shortest = [](int d, int size)
		{
			d = ((d % size) + size) % size;
			return d > size / 2 ? d - size : d;
		};
		const int dx = shortest(at.first - x, w), dy = shortest(at.second - y, h);
		result["moved"] = {{"dx", dx}, {"dy", dy}, {"tiles", std::max(std::abs(dx), std::abs(dy))}};
		if (!requestedRefusal.empty())
			result["moved"]["requested_refused"] = requestedRefusal;
	}
	// A flag's workers are the units that serve it: explorers for an exploration flag,
	// warriors for a war flag.
	if (args.contains("workers") && (shortName == "explorationflag" || shortName == "warflag"))
		result["workers_note"] = std::string("workers is how many ") +
								 (shortName == "warflag" ? "warriors" : "explorers") + " this flag requests";
	// What the create order carries, after capping at what the type allows.
	Json applied = Json::object();
	for (const auto &[field, value] : {std::pair{"workers", settings.workers}, std::pair{"builders", settings.builders},
									   std::pair{"range", settings.range}})
		if (value)
		{
			applied[field] = *value;
			if (args.at(field).get<int>() != *value)
				applied[std::string(field) + "_capped"] = true;
		}
	if (priority)
	{
		// The create order has no priority: the building panel's priority order follows
		// once the site or flag exists.
		pendingPriorities.push_back({shortName, at.first, at.second, *priority, gui.game.stepCounter});
		while (pendingPriorities.size() > 16)
			pendingPriorities.pop_front();
		applied["priority"] = args.at("priority");
		applied["priority_note"] = "set by a follow-up order once the site or flag appears (the create order "
								   "has no priority)";
	}
	if (!applied.empty())
		result["settings"] = std::move(applied);
	return result;
}
void LocalCommander::applyPendingPriorities()
{
	if (pendingPriorities.empty())
		return;
	const auto observedTick = gui.simulationThreaded ? gui.drawnScene().tick : gui.game.stepCounter;
	if (observedTick < nextPriorityCheck)
		return;
	nextPriorityCheck = observedTick + 5;
	std::vector<Json> orders;
	boundary(
		[&]
		{
			auto scope = observations.captureObservation();
			observations.observe();
			const auto buildings = json(observations.query("buildings", {}));
			for (auto it = pendingPriorities.begin(); it != pendingPriorities.end();)
			{
				bool found = false;
				for (const auto &b : buildings)
				{
					const auto key = b.value("key", std::string());
					if (b.value("team", -1) == team && b.value("x", -1) == it->x && b.value("y", -1) == it->y &&
						key.substr(0, key.find('.')) == it->building)
					{
						orders.push_back({{"type", "priority"},
										  {"building", {{"id", b.value("id", 0)}, {"generation", b.value("generation", 0)}}},
										  {"priority", it->priority}});
						found = true;
						break;
					}
				}
				// Never appeared (refused by the game): game_world's placements say so.
				it = found || gui.game.stepCounter > it->tick + 200 ? pendingPriorities.erase(it) : std::next(it);
			}
		});
	for (const auto &order : orders)
		try
		{
			submit({order});
		}
		catch (const std::exception &)
		{
			// The building went in the meantime, or the queue is full: nothing to retry.
		}
}
Json LocalCommander::view(const Json &args)
{
	// Without x/y: the colony's Swarm, or its first building, centred.
	int x = -1, y = -1;
	std::string centredOn;
	if (args.contains("x") != args.contains("y"))
		throw std::runtime_error("Pass both x and y, or neither to centre on your colony");
	if (args.contains("x"))
		x = args.at("x"), y = args.at("y");
	else
		boundary(
			[&]
			{
				for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
				{
					const auto *b = gui.game.teams[team]->myBuildings[slot];
					if (!b || b->type->isVirtual)
						continue;
					const bool swarm = b->type->type == "swarm";
					if (x < 0 || swarm)
					{
						x = b->posX + b->type->width / 2, y = b->posY + b->type->height / 2;
						centredOn = swarm ? "your swarm" : "your first building";
					}
					if (swarm)
						break;
				}
			});
	if (x < 0)
		throw std::runtime_error("You have no buildings to centre on: pass x and y");
	if (!args.contains("x"))
		x %= int(gui.game.map.getW()), y %= int(gui.game.map.getH()); // a footprint across the edge
	if (x < 0 || y < 0 || x >= int(gui.game.map.getW()) || y >= int(gui.game.map.getH()))
		throw std::runtime_error("Tile (" + std::to_string(x) + ", " + std::to_string(y) +
								 ") is outside the map; read its size from game_world");
	// x/y is always the centre. width/height: the span to frame around it, as a player
	// zooms out to see a battle or a whole base; zoom is then chosen to fit it.
	const double cx = x + 0.5, cy = y + 0.5;
	std::optional<double> zoom;
	if (args.contains("zoom"))
		zoom = args.at("zoom").get<double>();
	if (args.contains("width") || args.contains("height"))
	{
		const int w = args.value("width", 1), h = args.value("height", 1);
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
	// The map wraps: of the tile's copies (one map width or height apart), report the
	// one nearest the screen's centre, which is the one on screen when any is.
	const double mapW = gui.game.map.getW() * 32.0, mapH = gui.game.map.getH() * 32.0;
	const double screenW = camera.visibleW() * camera.zoom, screenH = camera.visibleH() * camera.zoom;
	auto point = camera.worldToScreen((x + 0.5) * 32, (y + 0.5) * 32);
	for (int ky = -1; ky <= 1; ++ky)
		for (int kx = -1; kx <= 1; ++kx)
		{
			const auto p = camera.worldToScreen((x + 0.5) * 32 + kx * mapW, (y + 0.5) * 32 + ky * mapH);
			const auto distance = [&](const auto &q)
			{ return std::abs(q.first - screenW / 2) + std::abs(q.second - screenH / 2); };
			if (distance(p) < distance(point))
				point = p;
		}
	const bool onScreen = point.first >= 0 && point.second >= 0 && point.first < screenW && point.second < screenH;
	return {{"view",
			 {{"x", int(std::floor(camera.originX / 32))},
			  {"y", int(std::floor(camera.originY / 32))},
			  {"width", int(std::ceil(camera.visibleW() / 32))},
			  {"height", int(std::ceil(camera.visibleH() / 32))}}},
			{"tile_screen", {{"x", int(point.first)}, {"y", int(point.second)}, {"on_screen", onScreen}}},
			{"tile_pixels", 32 * camera.zoom},
			{"zoom", camera.zoom},
			{"zoom_range", {camera.minimumZoom(), MapCamera::MAX_ZOOM}},
			{"centre", centredOn.empty() ? Json{{"x", x}, {"y", y}} : Json{{"x", x}, {"y", y}, {"on", centredOn}}},
			{"note", "tile_screen is the tile's centre in logical pixels: click it with ui_click space "
					 "\"logical\" (or a control with ui_click key). Without x/y the view centres on your colony."}};
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
	// Plain text on one line, as the chat box allows: no control characters (NUL, tabs,
	// newlines, escapes, or the C1 range U+0080..U+009F), and something besides spaces.
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const auto c = static_cast<unsigned char>(text[i]);
		const bool c1 = c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
						static_cast<unsigned char>(text[i + 1]) <= 0x9F;
		if (c < 0x20 || c == 0x7F || c1)
		{
			static const char hex[] = "0123456789ABCDEF";
			const unsigned code = c1 ? static_cast<unsigned char>(text[i + 1]) : c;
			throw std::runtime_error("Chat text has a control character (U+00" + std::string{hex[code >> 4], hex[code & 15]} +
									 ") at byte " + std::to_string(i) +
									 ": send plain text on one line, without newlines, tabs or NUL");
		}
	}
	if (text.find_first_not_of(' ') == std::string::npos)
		throw std::runtime_error(text.empty() ? "Chat text is empty: pass the message to send"
											  : "Chat text is only spaces: pass the message to send");
	// Chat follows the player's age band (src/app/AgeGate.h), whoever types it.
	if (!AgeGate::chatAllowedNow())
		throw std::runtime_error(AgeGate::socialFeatures(AgeGate::local().band)
									 ? "Chat is off in this match: a player in it is under 13 or has not said their age"
									 : "Chat is off: this player is under 13 or has not said their age (asked before LAN or online play)");
	if (!gui.sendChatFromAutomation(text))
		throw std::runtime_error("Chat text is empty: pass the message to send");
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
		stepTarget = tick + steps;
		// wait: answer once the steps have run, as game_wait would (no until events).
		if (args.value("wait", false))
			return wait({{"ticks", steps}, {"until", Json::array()}, {"_step", true}});
	}
	if (action == "pause" || action == "resume")
		stepTarget = 0;
	const unsigned left = gui.heldSteps.load();
	Json result = {{"tick", tick},
				   {"paused", bool(gui.gamePaused) || gui.simulationHeld()},
				   {"paused_by", gui.gamePaused	   ? "menu"
								 : !gui.hardPause ? "none"
								 : gui.agentPaused ? "agent"
												   : "player"},
				   {"steps_remaining", left},
				   {"speed", globalContainer->settings.getGameSpeedText()},
				   {"speed_value", globalContainer->settings.gameSpeed}};
	// Unlimited speed has no fixed rate: thousands of ticks can pass between two calls.
	if (const double rate = gui.targetTickRate(); rate > 0)
		result["ticks_per_second"] = std::round(rate * 100) / 100;
	else
		result["ticks_per_second"] = nullptr, result["warning"] = "Unlimited speed: the match runs as fast as the "
																	"computer can, so much can happen between two calls";
	if (action == "step")
	{
		result["target_tick"] = tick + steps;
		result["note"] = "Stepping runs the ticks at the current speed, then holds the match paused again; "
						 "game_wait (or step with wait: true) returns once they have run. Orders sent while "
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
			Logged sighted{"enemy_building_sighted", "Building sighted: " + key.substr(0, key.find('.')) + " of team " +
							   std::to_string(owner), b.value("x", 0), b.value("y", 0), owner, gui.game.stepCounter};
			sighted.what = key.substr(0, key.find('.'));
			record(std::move(sighted));
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
		if (cell.visible && !visible.count(it->first))
		{
			// In view and no longer there: destroyed, or dismantled by its owner.
			Logged gone{"enemy_building_gone", "Building gone: " + it->second.value("type", std::string()) + " of team " +
							std::to_string(it->second.value("team", -1)),
						it->second.value("x", 0), it->second.value("y", 0), it->second.value("team", -1), gui.game.stepCounter};
			gone.what = it->second.value("type", std::string());
			record(std::move(gone));
			it = seenBuildings.erase(it);
		}
		else
			it = std::next(it);
	}
	// Own buildings that are gone since the last refresh. An identity reused by a new
	// building has another generation, so the earlier one is gone too.
	for (const auto &[gid, before] : ownBuildings)
	{
		const auto now = owned.find(gid);
		if (now != owned.end() && now->second.generation == before.generation)
			continue;
		const bool demolished = before.demolishing || demolitionOrders.count(gid) > 0;
		losses.push_back({before.type, before.x, before.y, gui.game.stepCounter, demolished});
		Logged lost{demolished ? "building_demolished" : "building_destroyed",
					(demolished ? "Building demolished: " : "Building destroyed: ") + before.type, before.x, before.y, team,
					gui.game.stepCounter};
		lost.what = before.type;
		record(std::move(lost));
		demolitionOrders.erase(gid);
	}
	while (losses.size() > 32)
		losses.pop_front();
	ownBuildings = std::move(owned);
	memoryStarted = true;
}
Json LocalCommander::unitStatus() const
{
	// The colony's own units now, as units by type counts them. Free is the top bar's
	// meaning: fed, healthy and not working; for workers, less the units that buildings
	// and flags request and do not have yet (never below 0: requested_over says by how
	// many the requests exceed the free workers). Beside it, where the others are:
	// idle (free, before requests), working (a building's job), at_flags, in_guard_zones
	// (idle in a guard zone), fighting, hungry or injured (going to or inside an Inn or
	// Hospital) and inside (in a building: eating, healing or training). Zero counts
	// are left out.
	static const char *const kinds[] = {"workers", "explorers", "warriors"};
	struct Count
	{
		int total = 0, idle = 0, working = 0, flags = 0, guard = 0, fighting = 0, hungry = 0, injured = 0, inside = 0;
	} counts[3];
	const Team *colony = gui.game.teams[team];
	for (const Unit *u : colony->liveUnits.entries())
		if (u && u->typeNum < 3)
		{
			auto &c = counts[u->typeNum];
			++c.total;
			const bool isInside = u->displacement == Unit::DIS_INSIDE || u->displacement == Unit::DIS_ENTERING_BUILDING;
			c.inside += isInside;
			if (u->medical == Unit::MED_HUNGRY)
				++c.hungry;
			else if (u->medical == Unit::MED_DAMAGED)
				++c.injured;
			else if (u->activity == Unit::ACT_FLAG)
				++c.flags;
			else if (u->activity != Unit::ACT_RANDOM)
				++c.working;
			else
			{
				++c.idle;
				if (u->displacement == Unit::DIS_ATTACKING_AROUND)
					++c.fighting;
				else if (u->typeNum == WARRIOR && gui.game.map.isGuardArea(u->posX, u->posY, colony->me))
					++c.guard;
			}
		}
	int requested = 0;
	for (const Building *b : colony->liveBuildings.entries())
		if (b && (b->type->foodable || b->type->fillable || b->runtime->attractsRole(0)))
			requested += std::max(0, b->desiredMaxUnitWorking - int(b->unitsWorking.size()));
	Json result = Json::object();
	for (int i = 0; i < 3; ++i)
	{
		const auto &c = counts[i];
		const int wanted = i == 0 ? requested : 0;
		Json entry = {{"free", std::max(0, c.idle - wanted)}, {"total", c.total}};
		for (const auto &[name, n] : {std::pair{"idle", c.idle}, std::pair{"working", c.working},
									  std::pair{"at_flags", c.flags}, std::pair{"in_guard_zones", c.guard},
									  std::pair{"fighting", c.fighting}, std::pair{"hungry", c.hungry},
									  std::pair{"injured", c.injured}, std::pair{"inside", c.inside}})
			if (n)
				entry[name] = n;
		if (i == 0)
		{
			entry["requested_unfilled"] = requested;
			if (requested > c.idle)
				entry["requested_over"] = requested - c.idle;
		}
		result[kinds[i]] = std::move(entry);
	}
	return result;
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
					orders.push_back(Script::order(gui.game, team, value(placeByName(d))));
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
	// Ids belong to a match: refuse another match's session, and a reference this
	// commander never reported unless the caller names this match's session.
	checkSession(args);
	if (!args.contains("session") &&
		!reportedRefs.count({args.at("id").get<int>(), args.at("generation").get<unsigned>()}))
		throw std::runtime_error("Building " + args.at("id").dump() + " generation " + args.at("generation").dump() +
								 " was not reported in this match: call game_world for this match's ids (or pass "
								 "this match's session, from game_world, with an id read another way)");
	const Json ref = {{"id", args.at("id")}, {"generation", args.at("generation")}};
	std::vector<Json> orders;
	Json changed = Json::array();
	// A request above the building's unit limit, or a flag range above its type's
	// maximum, is capped, and the reply says so.
	int limit = MAX_BUILDING_WORKER_REQUEST;
	int maximumRange = -1;
	std::string problem;
	bool site = false, isFlag = false, upgradable = false;
	std::string typeName, servedBy = "workers";
	boundary(
		[&]
		{
			// Say exactly what is wrong with the reference, and how to find the right one.
			const int id = args.at("id");
			if (Building::GIDtoTeam(id) != team)
			{
				problem = "Building " + std::to_string(id) + " is team " + std::to_string(Building::GIDtoTeam(id)) +
						  "'s, not yours";
				return;
			}
			const auto *b = gui.game.teams[team]->myBuildings[Building::GIDtoID(id)];
			if (!b)
			{
				problem = "You have no building with id " + std::to_string(id) + " (destroyed or never built)";
				return;
			}
			{
				auto scope = observations.captureObservation();
				observations.observe();
				for (const auto &record : json(observations.query("buildings", {})))
					if (record.value("id", -1) == id && record.value("generation", 0u) != args.at("generation").get<unsigned>())
					{
						problem = "Building " + std::to_string(id) + " is now generation " +
								  std::to_string(record.value("generation", 0u)) + " (a " + b->type->type +
								  "): the one you meant is gone";
						return;
					}
			}
			limit = std::min(limit, int(b->type->semantics.assignmentLimit));
			maximumRange = b->type->maxUnitStayRange;
			site = b->type->isBuildingSite;
			isFlag = b->type->isVirtual;
			upgradable = !site && !isFlag && b->type->nextLevel >= 0;
			typeName = b->type->type;
			if (isFlag && b->type->zonable[EXPLORER])
				servedBy = "explorers";
			else if (isFlag && b->type->zonable[WARRIOR])
				servedBy = "warriors";
		});
	if (!problem.empty())
		throw std::runtime_error(problem + "; game_world detail brief lists your buildings' ids and generations");
	// builders: the units building a site (its worker request), or upgrade/repair builders.
	auto effective = args;
	if (effective.contains("builders") && !effective.contains("workers") && !effective.contains("action"))
		effective["workers"] = effective.at("builders");
	const auto &a = effective;
	int requested = 0, rangeRequested = 0;
	if (a.contains("action"))
	{
		const auto action = a.at("action").get<std::string>();
		if ((action == "upgrade") && !upgradable)
			throw std::runtime_error(site ? "A construction site cannot be upgraded: wait until it is finished"
							   : isFlag ? "Flags have no levels to upgrade"
										: "This " + typeName + " is at its highest level");
		if (action == "move" && !isFlag)
			throw std::runtime_error("Only flags move; this is a " + typeName);
	}
	if ((a.contains("range")) && !isFlag)
		throw std::runtime_error("range is a flag's setting; this is a " + typeName);
	if (a.contains("workers"))
	{
		requested = a.at("workers");
		orders.push_back({{"type", "workers"}, {"building", ref}, {"workers", std::min(requested, limit)}});
		changed.push_back("workers");
	}
	if (a.contains("priority"))
	{
		const auto p = a.at("priority").get<std::string>();
		orders.push_back({{"type", "priority"}, {"building", ref},
						  {"priority", p == "low" ? -1 : p == "high" ? 1 : 0}});
		changed.push_back("priority");
	}
	if (a.contains("production"))
	{
		const auto &r = a.at("production");
		for (const char *field : {"workers", "explorers", "warriors"})
			if (r.contains(field) && (!r.at(field).is_number_integer() || r.at(field).get<std::int64_t>() < 0 ||
									  r.at(field).get<std::int64_t>() > 16))
				throw std::runtime_error(std::string("production.") + field + " must be a whole number 0..16 (got " +
										 r.at(field).dump() + ")");
		orders.push_back({{"type", "production"}, {"building", ref},
						  {"ratios", {r.value("workers", 0), r.value("explorers", 0), r.value("warriors", 0)}}});
		changed.push_back("production");
	}
	if (a.contains("range"))
	{
		rangeRequested = a.at("range");
		const int range = maximumRange >= 0 ? std::min(rangeRequested, maximumRange) : rangeRequested;
		orders.push_back({{"type", "range"}, {"building", ref}, {"range", range}});
		changed.push_back("range");
	}
	if (a.contains("action"))
	{
		const auto act = a.at("action").get<std::string>();
		const int builders = a.value("builders", 3);
		if (act == "upgrade" || act == "repair")
			orders.push_back({{"type", "construction"}, {"building", ref}, {"workers", builders},
							  {"futureWorkers", a.value("workers", builders)}});
		else if (act == "cancel_upgrade")
			orders.push_back({{"type", "cancelConstruction"}, {"building", ref}, {"workers", a.value("workers", 1)}});
		else if (act == "destroy")
		{
			orders.push_back({{"type", "delete"}, {"building", ref}});
			demolitionOrders.insert(a.at("id").get<int>());
		}
		else if (act == "cancel_destroy")
		{
			orders.push_back({{"type", "cancelDelete"}, {"building", ref}});
			demolitionOrders.erase(a.at("id").get<int>());
		}
		else if (act == "move")
		{
			if (!a.contains("x") || !a.contains("y"))
				throw std::runtime_error("move needs the flag's new x and y tile");
			orders.push_back({{"type", "moveFlag"}, {"building", ref}, {"x", a.at("x")}, {"y", a.at("y")}});
		}
		changed.push_back(act);
	}
	Json result = {{"queued", changed},
				   {"note", "Applied like the building panel's controls after the next simulation "
							"step; read game_world to confirm."}};
	if (requested > limit)
	{
		result["workers_capped_at"] = limit;
		if (limit == 0)
			result["workers_capped_reason"] = "a finished " + typeName + " takes no units (it is not worked)";
	}
	if (maximumRange >= 0 && rangeRequested > maximumRange)
		result["range_capped_at"] = maximumRange;
	if (a.contains("workers") && servedBy != "workers")
		result["workers_note"] = "workers is how many " + servedBy + " this " + typeName + " requests";
	if (orders.empty())
		throw std::runtime_error("Nothing to change: pass workers, priority, production, range or action");
	submit(orders);
	return result;
}
Json LocalCommander::zone(const Json &args)
{
	const auto kind = args.at("zone").get<std::string>();
	const int width = args.at("width"), height = args.at("height");
	const bool erase = args.value("mode", std::string("paint")) == "erase";
	Json mask = Json::array();
	for (int i = 0; i < width * height; ++i)
		mask.push_back(true);
	submit({{{"type", kind == "forbidden" ? "forbidden" : kind == "guard" ? "guardArea" : "clearArea"},
			 {"mode", erase ? 2 : 1},
			 {"x", args.at("x")},
			 {"y", args.at("y")},
			 {"width", width},
			 {"height", height},
			 {"mask", mask}}});
	return {{"queued", true},
			{"zone", kind},
			{"mode", erase ? "erase" : "paint"},
			{"area", {{"x", args.at("x")}, {"y", args.at("y")}, {"width", width}, {"height", height}}},
			{"note", erase ? "The zone is erased from this area after the next simulation step; the map "
							 "and the Flags and areas panel show it gone."
						   : "The zone is painted after the next simulation step; it appears on the map "
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
									 "end_game results screen; status.win_chance here is the current "
									 "chance the HUD shows");
	std::erase_if(metrics, [](const auto &m) { return m.winProbability; });
	static const std::vector<std::string> defaults = {"population", "net growth", "hunger",	"health",
													  "idle workers", "blocked buildings", "warriors", "intruders"};
	Json result = statsReport(metrics, {{team, history.get(), details}}, args, defaults);
	if (args.value("list", false))
		return result;
	const auto bar = gui.topBarValues();
	const auto chance = gui.localWinChancePercent();
	Json unitsFree;
	boundary([&] { unitsFree = unitStatus(); });
	result["mode"] = "match";
	result["tick"] = now;
	result["seconds"] = now / GAME_TICKS_PER_SECOND;
	result["team"] = details;
	result["status"] = {{"units_free", unitsFree},
						{"win_chance", chance ? Json(*chance) : Json(nullptr)},
						{"prestige", bar[3]},
						{"converted_in", bar[5]},
						{"converted_out", bar[6]},
						{"hud_sampled", {{"workers", bar[0]}, {"explorers", bar[1]}, {"warriors", bar[2]},
										 {"win_chance", bar[4]}}}};
	result["note"] = "Your colony only, as the in-game Statistics panel shows it. Every team's "
					 "statistics and the win probability history follow on the end_game results "
					 "screen, while it is shown.";
	return result;
}
namespace
{
// game_wait's until categories, and the event tags each one stands for. An event's tags
// are its kind and, for unit_attacked, its kind and the unit type ("unit_attacked:explorer").
const std::map<std::string, std::set<std::string>> &waitCategories()
{
	// until names: these categories, or any alert kind itself (unit_attacked...).
	static const std::map<std::string, std::set<std::string>> categories = {
		// Attacks on workers, warriors and buildings: a lone scout under fire does not count.
		{"attack", {"unit_attacked:worker", "unit_attacked:warrior", "unit_attacked:unknown", "building_attacked"}},
		{"attack_buildings", {"building_attacked"}},
		{"attack_warriors", {"unit_attacked:warrior"}},
		{"unit_attacked", {"unit_attacked"}},
		{"building_attacked", {"building_attacked"}},
		{"building_completed", {"building_completed"}},
		{"building_lost", {"building_destroyed"}},
		{"building_destroyed", {"building_destroyed"}},
		// conversion is both directions (as before); conversion_lost only your units.
		{"conversion", {"unit_converted_away", "unit_converted_to_you"}},
		{"conversion_lost", {"unit_converted_away"}},
		{"conversion_gained", {"unit_converted_to_you"}},
		{"unit_converted_away", {"unit_converted_away"}},
		{"unit_converted_to_you", {"unit_converted_to_you"}},
		{"enemy_sighted", {"enemy_building_sighted"}},
		{"enemy_building_sighted", {"enemy_building_sighted"}},
		{"enemy_building_gone", {"enemy_building_gone"}},
		{"hive_failure", {"hive_program_failed"}},
		{"hive_program_failed", {"hive_program_failed"}},
		{"hive_wake", {"hive_wake"}}};
	return categories;
}
} // namespace
Json LocalCommander::wait(const Json &args)
{
	// Everything read here is what the colony itself is told (its journal): the HUD's
	// event reports, its own losses, buildings it saw appear or go, its scripts.
	const unsigned now = gui.simulationThreaded ? gui.drawnScene().tick : gui.game.stepCounter;
	Json poll;
	const bool polling = args.contains("_poll");
	if (polling)
		poll = args.at("_poll");
	std::map<std::string, std::string> wanted; // event tag -> the until name that asked for it
	const Json defaults = {"attack", "building_lost", "conversion_lost", "enemy_sighted", "enemy_building_gone",
						   "hive_failure", "hive_wake"};
	const Json until = polling ? poll.value("until", defaults) : args.value("until", defaults);
	for (const auto &category : until)
	{
		const auto name = category.is_string() ? category.get<std::string>() : category.dump();
		const auto found = waitCategories().find(name);
		if (found == waitCategories().end())
		{
			std::string names;
			for (const auto &[n, kinds] : waitCategories())
				names += (names.empty() ? "" : ", ") + n;
			throw std::runtime_error("Unknown until name \"" + name + "\"; use one of: " + names);
		}
		for (const auto &tag : found->second)
			wanted.emplace(tag, name);
	}
	if (!polling)
	{
		// resume: let a paused match run, so pausing to think and waiting are one call each.
		if (args.value("resume", false) && (gui.gamePaused || gui.simulationHeld()))
			time({{"action", "resume"}});
		// Losses and sightings need the game_world memory: start it if needed.
		boundary(
			[&]
			{
				memoryTracked = true;
				if (!memoryStarted)
					refreshMemory();
			});
		recordFeed();
		const unsigned from = args.contains("since_tick") ? args.at("since_tick").get<unsigned>() : now;
		poll = {{"start", from},
				{"include", args.value("include", Json())},
				{"began", now},
				// A step's wait ends at the step's own end, whatever the published scene shows.
				{"target", args.value("_step", false) ? stepTarget : now + args.value("ticks", 300u)},
				{"ongoing", Json::object()},
				{"until", until}};
		if (args.value("_step", false))
			poll["step"] = true;
		// Attacks already under way when the wait starts are reported, but only a new
		// skirmish (another place, or one that had gone quiet) ends the wait.
		for (const auto &e : eventLog)
			if ((e.kind == "unit_attacked" || e.kind == "building_attacked") && e.tick + 150 >= now)
				poll["ongoing"][e.kind + std::to_string(e.x / 8) + "," + std::to_string(e.y / 8)] = true;
	}
	recordFeed();
	const unsigned start = poll.at("start"), target = poll.at("target");
	std::string reason, kind;
	Json trigger;
	if (!gui.announcedOutcome().empty())
		reason = "match_end";
	for (auto it = eventLog.rbegin(); reason.empty() && it != eventLog.rend() && it->tick > start; ++it)
	{
		auto match = wanted.find(it->kind);
		if (match == wanted.end() && it->kind == "unit_attacked")
			match = wanted.find(it->kind + ":" + (it->subject >= 0 && it->subject < 3 ? unitNames[it->subject] : "unknown"));
		if (match == wanted.end())
			continue;
		if ((it->kind == "unit_attacked" || it->kind == "building_attacked") &&
			poll["ongoing"].contains(it->kind + std::to_string(it->x / 8) + "," + std::to_string(it->y / 8)))
			continue;
		reason = match->second, kind = it->kind;
		trigger = {{"kind", it->kind}, {"event", it->event}};
		if (it->x >= 0)
			trigger["x"] = it->x, trigger["y"] = it->y;
		if (it->subject >= 0 && it->subject < 3)
			trigger["unit"] = unitNames[it->subject];
	}
	const unsigned simTick = gui.game.stepCounter;
	if (reason.empty() && std::max(now, simTick) >= target)
		reason = "ticks";
	// game_time step holds the match paused again once its ticks have run; until then
	// (the last steps can still be running) the wait is not over.
	const bool stepping = stepTarget > std::max(now, simTick) && gui.hardPause && !gui.gamePaused;
	if (reason.empty() && (gui.gamePaused || gui.simulationHeld()) && !stepping)
		reason = "paused";
	if (reason.empty() && args.value("_final", false))
		reason = "timeout";
	if (reason.empty())
		return {{"pending", true}, {"poll", poll}};
	// Done: game_world detail alerts since the wait's start (grouped, newest first).
	Json report = {{"detail", "alerts"}, {"since_tick", start}};
	if (poll.contains("include") && !poll.at("include").is_null())
		report["include"] = poll.at("include");
	Json result = world(report);
	const unsigned began = poll.value("began", start), endTick = result.value("tick", now);
	if (reason == "timeout" && endTick >= target)
		reason = "ticks"; // the published scene lagged the simulation at the last poll
	result["reason"] = reason;
	if (!trigger.is_null())
		result["trigger"] = trigger;
	result["from_tick"] = start;
	result["waited_ticks"] = endTick - began;
	if (const auto outcome = gui.announcedOutcome(); !outcome.empty())
		result["outcome"] = outcome;
	const bool step = poll.value("step", false);
	result["note"] =
		reason == "paused" ? "The match is paused, so no time passes: game_time resume first (or game_wait resume: true)."
		: reason == "ticks" ? (step ? "The steps have run; the match is paused again."
									: "The requested ticks passed without an until event.")
		: reason == "timeout"
			? std::to_string(endTick - began) + " of " + std::to_string(target - began) +
				  " ticks passed before timeout_ms (default 25000, at most 30000) ended the wait; call game_wait "
				  "again, or raise the game speed."
		: reason == "match_end" ? "The game has announced the result; ok/quit the dialog to reach end_game."
								: "Stopped for the first until event (trigger); alerts lists everything since from_tick.";
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
	// Nothing to change: report the current diplomacy, as the Alliance dialog shows it
	// (your own settings toward each team; not what the others share with you).
	const auto &scene = gui.drawnScene();
	if (!allied && !map && !food && !market)
	{
		if (!scene.world.session)
			throw std::runtime_error("No match is running");
		const auto &local = scene.entities.teams[team];
		Json teams = Json::array();
		for (int t = 0; t < scene.entities.teamCount; ++t)
		{
			if (t == team)
				continue;
			const Uint32 bit = Team::teamNumberToMask(t);
			Json players = Json::array();
			bool ai = false;
			for (const auto &pl : scene.world.session->players)
				if (pl.teamNumber == t)
					players.push_back(pl.name), ai = ai || pl.type >= BasePlayer::P_AI;
			teams.push_back({{"team", t}, {"players", players}, {"ai", ai}, {"allied", (local.allies & bit) != 0},
							 {"share_map", (local.otherVision & bit) != 0},
							 {"share_food_vision", (local.foodVision & bit) != 0},
							 {"share_market", (local.exchangeVision & bit) != 0}});
		}
		return {{"your_team", team}, {"teams_fixed", scene.world.session->fixedAlliances}, {"teams", teams},
				{"note", "Your settings toward each team. Pass team with allied/share_* to change them."}};
	}
	if (!args.contains("team"))
		throw std::runtime_error("Pass team with the settings to change (game_alliance with only token lists teams)");
	if (const auto error = gui.setAllianceFromAutomation(args.at("team").get<int>(), allied, map, food, market);
		!error.empty())
		throw std::runtime_error(error);
	Json requested = {{"team", args.at("team")}};
	for (const char *name : {"allied", "share_map", "share_food_vision", "share_market"})
		if (args.contains(name))
			requested[name] = args.at(name);
	return {{"queued", true},
			{"requested", requested},
			{"note", "Sent as the Alliance dialog's OK; it applies after the next simulation step. "
					 "share_food_vision lets that team's hungry units see your Inns (and so join "
					 "you when yours are happier)."}};
}
Json matchOver(const std::string &tool)
{
	if (tool != "game_wait")
		throw std::runtime_error("The match ended before the operation finished");
	return {{"reason", "match_end"},
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
		// No commander: either no match at all, or one these tools cannot command.
		const auto screen = screenSource() ? screenSource()() : std::string();
		const bool unsupportedMatch = globalContainer->replaying || globalContainer->liveSpectating ||
									  screen == "game_session" ||
									  (screen.empty() && active && !active->available());
		if (unsupportedMatch)
			throw std::runtime_error("Local Hive unavailable: requires a local human match; online, "
									 "replay, spectator and AI takeover modes are unsupported");
		throw std::runtime_error("No match is running" + (screen.empty() ? std::string() : " (screen " + screen + ")") +
								 ": start one from custom_game (game_guide explains how)");
	}
	return active->call(tool, args);
}
void setScreenSource(std::function<std::string()> source)
{
	screenSource() = std::move(source);
}
} // namespace Hive
