// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocalCommander.h"
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
namespace Hive
{
namespace
{
LocalCommander *active = nullptr; // main thread only
std::atomic<std::uint64_t> sessions{0};
constexpr std::size_t JournalCapacity = 256;
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
		reports.push_back(error);
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
