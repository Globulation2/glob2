// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

// From a match.start (MatchAssignment) to a running game, for every online way into
// a match: rooms (PlatformRoom) and quick match (the queue screens). The starting
// screen (MatchStartScreen) polls an OnlineMatch every frame and shows its steps:
//
//   Seat     the assignment arrived and parsed (MatchSetup checked)
//   Map      the map blob is fetched by hash into the MapCache (skipped when cached)
//   Load     Engine::initTurnMatchTask builds the game, a slice per frame
//   Relay    the TurnSession connects to the ticket's relay and says Hello
//   Players  waiting for the relay's first turns; other seats' presence is shown
//   Playing  takeEngine() hands the engine to GameSessionScreen
//
// A relay that refuses the match as new (Reject 5: draining or full) before the first
// tick is reported with match.reconnect {relayUnavailable: true}; the platform moves the
// match and the new assignment restarts the flow from Load.
//
// OnlineMatchResult outlives the game: the results screen shows the outcome and the
// rating card, which match.updated events update live (verifying -> verified).

#include "PlatformProtocol.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Engine;
namespace GAGCore
{
class CooperativeTask;
}
namespace Turn
{
class TurnTransport;
}

namespace Online
{
class PlatformClient;

/// What the results screen shows about an online match, kept up to date from
/// match.updated while it lives.
class OnlineMatchResult
{
  public:
	enum class Verification
	{
		Pending,
		Verified,
		Diverged,
		Unverifiable,
		NotApplicable,
	};
	OnlineMatchResult(std::string origin, std::string matchId, std::string accountId);
	~OnlineMatchResult();
	OnlineMatchResult(const OnlineMatchResult &) = delete;
	OnlineMatchResult &operator=(const OnlineMatchResult &) = delete;

	/// Starts listening to match.updated on the client (no-op without one).
	void listen(PlatformClient &client);
	/// Applies a MatchSummary (match.updated data.match, or a fixture).
	void apply(const Json &summary);

	std::string origin, matchId, accountId;
	/// "1 vs 1 · Ranked", "Room · Sunday 2v2"
	std::string label;
	std::string mapTitle;
	bool fromRoom = true;
	bool rated = false;
	std::string ladder;
	Verification verification = Verification::Pending;
	/// Rating before the match and the server's result once verified.
	std::optional<double> ratingBefore, ratingAfter;
	/// The client's guess before verification (optional assignment field
	/// ratingPreview; see docs/multiplayer/client.md).
	std::optional<double> ratingExpectedWin, ratingExpectedLoss;
	bool provisional = false;
	/// Platform outcome of this account: won | lost | unresolved | abandoned.
	std::string outcome;
	/// Bumped on every change, so screens can poll cheaply.
	unsigned revision = 0;

	/// <origin>/matches/<id>: the web match page.
	std::string matchPageUrl() const;

  private:
	PlatformClient *client = nullptr;
	std::uint64_t listener = 0;
};

/// Optional context from the screen that started a match.
struct OnlineMatchContext
{
	std::string label;    ///< "1 vs 1 · Ranked" or the room's name
	bool fromRoom = true; ///< rooms return to the room; queues to the hub
	bool rated = false;
	std::string ladder;
};

class OnlineMatch
{
  public:
	enum class Step
	{
		Seat,
		Map,
		Load,
		Relay,
		Players,
		Playing,
		Failed,
	};
	using Context = OnlineMatchContext;
	struct Player
	{
		int seat = 0;
		std::string name;
		bool local = false;
		bool ai = false;
		std::uint8_t r = 128, g = 128, b = 128;
		/// 0..100 when known; -1 unknown.
		int progress = -1;
		/// "map", "loading", "ready", "connecting", "left"
		std::string state;
	};
	/// Builds the turn transport for a relay URL (RelayTransport outside tests).
	using TransportFactory = std::function<std::shared_ptr<Turn::TurnTransport>(const std::string &relayUrl)>;

	/// `client` provides the origin, the bearer token for the map download and
	/// match.reconnect; it must outlive this object.
	OnlineMatch(PlatformClient &client, Json assignment, Context context = {});
	~OnlineMatch();
	OnlineMatch(const OnlineMatch &) = delete;
	OnlineMatch &operator=(const OnlineMatch &) = delete;

	/// Advances every step that can make progress. nowMs is SDL ticks.
	void update(std::uint64_t nowMs);
	Step step() const { return current; }
	/// Leaves before the first tick: tells the relay, then gives up the engine.
	void leave();

	const std::string &matchId() const { return id; }
	int localSeat() const { return seat; }
	const Context &context() const { return ctx; }
	/// The MatchSetup as received.
	const Json &setup() const { return setupJson; }
	/// Relay host, round trip once known (ms, -1 unknown).
	std::string relayName() const;
	int relayRttMs() const;
	/// Map step: whether the map was already cached, and the download state.
	bool mapWasCached() const { return cachedMap; }
	std::string mapTitle() const;
	std::string mapFile() const { return mapPath; }
	/// The relay moved the match (Reject 5) this many times.
	int relayMoves() const { return moves; }
	/// Seats with their progress, for the per-player rows.
	std::vector<Player> players() const;
	/// Why the match could not start (Failed).
	const std::string &failure() const { return problem; }

	/// Once Playing: the initialized engine with the running turn session; it carries
	/// result() for the results screen.
	std::unique_ptr<Engine> takeEngine();
	std::shared_ptr<OnlineMatchResult> result() const { return outcome; }

	/// Tests: replaces RelayTransport.
	void setTransportFactory(TransportFactory factory) { makeTransport = std::move(factory); }
	/// Harness: freezes the flow at a step with fixed progress (no network).
	void preview(Step step, std::vector<Player> players, std::string relay, int rttMs);

  private:
	void fail(const std::string &why);
	void parseAssignment();
	void startMap();
	void startLoad();
	void reportRelayUnavailable();
	void reset();

	PlatformClient &client;
	Json assignment;
	Context ctx;
	Step current = Step::Seat;
	std::string id, problem, mapPath, relayUrl, ticket;
	Json setupJson;
	int seat = -1;
	bool cachedMap = false;
	int moves = 0;
	bool reconnectPending = false;
	std::uint64_t reconnectRequest = 0;
	bool previewing = false;
	std::vector<Player> previewPlayers;
	std::string previewRelay;
	int previewRtt = -1;
	std::uint64_t loadedAt = 0;

	struct Running;
	std::unique_ptr<Running> run;
	TransportFactory makeTransport;
	std::shared_ptr<OnlineMatchResult> outcome;
};

const char *stepName(OnlineMatch::Step step);

/// Matches this client saw end or change (MatchSummary objects from match.updated,
/// newest first, at most 20), for the hub's Recent matches until the platform has
/// history endpoints.
const Json &recentMatches();
void rememberMatch(const Json &summary);
} // namespace Online
