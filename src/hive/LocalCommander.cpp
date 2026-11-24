// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocalCommander.h"
#include "UnitConsts.h"
#include <deque>
#include <set>
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
namespace Hive
{
namespace
{
LocalCommander *active = nullptr; // main thread only
std::atomic<std::uint64_t> sessions{0};
constexpr std::size_t JournalCapacity = 256;
constexpr unsigned AlertTicks = 1500; // how long game_world keeps reporting an alert
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
		 {{"source_bytes", 131072},
		  {"programs", 8},
		  {"minimum_interval_ticks", 25},
		  {"orders_per_invocation", 32},
		  {"submission_capacity", 64},
		  {"journal_entries", JournalCapacity}}}};
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
	if (tool == "hive_result")
	{
		if (args.at("session") != session)
			throw std::runtime_error("Match session expired");
		auto it = journal.find(args.at("operation_key").get<std::string>());
		if (it == journal.end())
			throw std::runtime_error("Unknown operation key in this session");
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
	return journal.at(key);
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
		if (!eligible())
			throw std::runtime_error("Player context changed");
		if (!success)
			throw std::runtime_error(error.empty() ? "Worker failed or timed out" : error);
		std::vector<std::shared_ptr<Order>> orders;
		for (const auto &o : result.value("orders", Json::array()))
			orders.push_back(Script::order(gui.game, team, value(o)));
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
		if (!success)
			program->second.paused = true;
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
				Json request = {{"source", current.request.at("source")},
								{"snapshot", capture(observations, gui.game, team)}};
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
	const bool full = detail == "full", brief = detail == "brief";
	const int limit = args.value("limit", 100);
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
			for (const auto &b : buildings)
			{
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
			Json alerts = Json::array(), lostBuildings = Json::array();
			for (auto it = losses.rbegin(); it != losses.rend(); ++it)
			{
				const unsigned age = now > it->tick ? now - it->tick : 0;
				if (age > AlertTicks)
					continue;
				Json loss = {{"event", (it->demolished ? "Building demolished: " : "Building destroyed: ") + it->type},
							 {"kind", it->demolished ? "building_demolished" : "building_destroyed"},
							 {"x", it->x}, {"y", it->y}, {"count", 1}, {"ticks_ago", age}};
				if (!it->demolished)
					lostBuildings.push_back({{"type", it->type}, {"x", it->x}, {"y", it->y}, {"ticks_ago", age}});
				alerts.push_back(std::move(loss));
			}
			static const char *const eventKinds[GESize] = {"unit_attacked", "unit_converted_away",
													  "unit_converted_to_you", "building_attacked",
													  "building_completed"};
			Json events = Json::array();
			for (const auto &row : gui.gameEvents().rows())
			{
				const unsigned age = now > row.lastTick ? now - row.lastTick : 0;
				if (age > AlertTicks)
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
				for (const auto &b : own)
					if (b.value("x", -1) == it->x && b.value("y", -1) == it->y &&
						b.value("type", std::string()) == it->building)
						status = b.value("site", false) ? "site" : "built";
				if (status == "pending" && gui.game.stepCounter > it->tick + 40)
					status = "missing: refused by the game or destroyed before it was seen";
				followUp.push_back({{"building", it->building}, {"x", it->x}, {"y", it->y}, {"status", status}});
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
	// Zoom first, about the view's centre, so the tile ends up centred at the new scale.
	if (args.contains("zoom") && !gui.setMapZoom(args.at("zoom").get<double>()))
		throw std::runtime_error("Zoom is unavailable while the torus view reveals itself; "
								 "try again in a moment");
	gui.centreViewOnTile(x + 0.5, y + 0.5);
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
Json localCall(const std::string &tool, const Json &args)
{
	if (tool == "hive_api")
		return playerAPI();
	if (!active || !active->available())
	{
		if (tool == "hive_status")
			return {{"available", false},
					{"reason", "No active local human match"},
					{"limits", playerAPI().at("limits")}};
		throw std::runtime_error("Local Hive unavailable: requires a local human match; online, "
								 "replay, spectator and AI takeover modes are unsupported");
	}
	return active->call(tool, args);
}
} // namespace Hive
