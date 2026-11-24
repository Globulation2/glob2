// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CommanderView.h"
#include "HiveObservation.h"
#include "scripting/javascript/ScriptObservations.h"
#include <deque>
#include <map>
#include <set>
#include <future>
#include <functional>
class GameGUI;
namespace Hive
{
// Main-thread session service. Engine reads/order validation are parked explicitly;
// worker jobs own only filtered observations and never retain game pointers.
class LocalCommander : public CommanderView
{
	GameGUI &gui;
	Script::Observations observations;
	const int player, team;
	const std::string session;
	bool valid = true;
	unsigned tick = 0;
	struct Program
	{
		Json definition, state = nullptr, recent = nullptr;
		bool initialized = false, paused = false;
		unsigned nextTick = 0, random = 1;
		unsigned retries = 0; // consecutive retries after the worker could not start
		unsigned lastRunTick = 0, runs = 0;
		std::string pausedReason, lastError; // why a failure paused it; the latest failure
		std::uint64_t generation = 0;
	};
	struct Operation
	{
		std::string key;
		Json request;
	};
	std::map<std::string, Program> programs;
	std::map<std::string, unsigned> revisions;
	std::map<std::string, Json> journal;
	std::map<std::string, Json> requests;
	std::deque<Operation> queued;
	std::future<Json> worker;
	std::function<std::future<Json>(Json)> launch;
	Operation current;
	std::string currentProgram;
	std::uint64_t currentGeneration = 0, generation = 0;
	unsigned observationTick = 0;
	bool contextCurrent() const;
	bool eligible() const;
	void boundary(const std::function<void()> &work);
	void finish(Json result);
	void remember(const std::string &key, Json result);
	// MCP game_world / game_place / game_view: fog-filtered world state and tile-based
	// placement and camera moves, without writing a script.
	Json world(const Json &arguments);
	Json place(const Json &arguments);
	Json view(const Json &arguments);
	Json chat(const Json &arguments);
	// MCP game_time: hold, release or step the local match (a client-side hard pause).
	Json time(const Json &arguments);
	Json building(const Json &arguments);
	Json zone(const Json &arguments);
	// MCP game_stats in a match: the colony's own statistics, as the in-game
	// Statistics panel shows them (never another team's).
	Json stats(const Json &arguments);
	// MCP game_wait: let game time pass until a number of ticks or something the colony
	// would notice (attack, loss, conversion, sighting, script failure, match end). The
	// first call answers {pending, poll}; the MCP service polls each frame with _poll.
	Json wait(const Json &arguments);
	// MCP game_alliance: the Alliance dialog's OK for one other team.
	Json alliance(const Json &arguments);
	// hive_run / hive_result with wait: poll the journal until the operation finishes.
	Json awaitResult(const Json &arguments);
	// A script's create order by palette name, filled in as a click would.
	Json placeByName(Json order);
	// Adds the colony's own events and assigned counts to a Hive snapshot.
	void annotate(Json &snapshot);
	void submit(const std::vector<Json> &descriptors);
	// Why a hive_program install or replace cannot apply now (empty when it can).
	std::string programConflict(const std::string &kind, const Json &request) const;
	// The colony's own units by type, counted live (callers hold the boundary): free,
	// total, and for workers the unfilled requests of buildings and flags.
	Json unitStatus() const;
	// game_place priorities: the create order has none, so each is sent as the building
	// panel's priority order once the site or flag appears (or dropped after 200 ticks).
	struct PendingPriority
	{
		std::string building;
		int x, y, priority;
		unsigned tick;
	};
	std::deque<PendingPriority> pendingPriorities;
	unsigned nextPriorityCheck = 0;
	void applyPendingPriorities();
	// Standing-order failures, reported as game_world alerts: a program that fails is
	// paused (or, when its worker could not start, retried a few times first).
	struct Failure
	{
		std::string program, error;
		unsigned tick;
		bool paused;
	};
	std::deque<Failure> failures;
	// Other teams' buildings first seen since game_world memory started, newest last.
	struct Sighting
	{
		std::string type;
		int x, y, team;
		unsigned tick;
	};
	std::deque<Sighting> sightings;
	// Creates from scripts not yet visible as sites: findPlacement must not reuse them.
	struct PendingCreate
	{
		int x, y, type;
		unsigned tick;
	};
	std::deque<PendingCreate> scriptCreates;
	TileCache tileCache; // map tiles kept between Hive runs (see capture)
	// Everything the colony was told, as it happened: the HUD's event reports, its own
	// losses, buildings it saw appear or disappear, its scripts' failures and wakes.
	// game_world alerts, game_wait and ctx.game.events() all read this one journal.
	struct Logged
	{
		std::string kind, event;
		int x = -1, y = -1, team = -1;
		unsigned tick = 0;
		std::string program;
		bool paused = false;
	};
	std::deque<Logged> eventLog;
	std::map<std::uint64_t, std::uint64_t> rowSeen; // HUD row id -> update sequence logged
	unsigned destroyedTotal = 0, demolishedTotal = 0;
	void record(Logged entry);
	void recordFeed();
	// Alerts after a tick: grouped by kind, message and area, newest first.
	Json groupedAlerts(unsigned since, unsigned now, std::size_t cap) const;
	// Recent game_place requests, followed up in game_world until resolved.
	struct Placement
	{
		std::string building;
		int x, y, width, height;
		unsigned tick;
	};
	std::deque<Placement> placements;
	// What this player has seen of other teams' buildings, kept as last seen (fog of
	// war): refreshed every 50 ticks from the same visibility as Hive observations,
	// once an agent has asked for game_world. Until then nothing reads it, and a
	// periodic refresh would park the simulation in every local game.
	std::map<int, Json> seenBuildings;
	bool memoryTracked = false, memoryStarted = false;
	unsigned nextMemoryTick = 0;
	void refreshMemory();
	// The colony's own buildings as last refreshed, to report the ones it has lost:
	// the game raises no event when a building is destroyed, so a building that
	// disappears without being demolished is reported as destroyed.
	struct Owned
	{
		std::string type;
		int x, y;
		unsigned generation;
		bool demolishing;
	};
	std::map<int, Owned> ownBuildings;
	struct Loss
	{
		std::string type;
		int x, y;
		unsigned tick;
		bool demolished;
	};
	std::deque<Loss> losses;
	std::set<int> demolitionOrders; // game_building destroy requests, by building id
	int convertedAwaySeen = -1;

  public:
	explicit LocalCommander(GameGUI &gui, std::function<std::future<Json>(Json)> launch = {});
	~LocalCommander() override;
	void update();
	void clear();
	bool available() const override { return valid; }
	bool local() const override { return true; }
	Json status() const;
	Json call(const std::string &tool, const Json &arguments);
	Json standingOrders() const override;
	void stop() override;
	void change(const std::string &id, const std::string &action) override;
};
Json localCall(const std::string &tool, const Json &arguments);
// game_wait's answer once the match it waited in has ended.
Json matchOver(const std::string &tool);
Json playerAPI();
// hive_api: the Hive reference by section, or searched, rather than in one piece.
Json apiDocs(const Json &arguments);
} // namespace Hive
