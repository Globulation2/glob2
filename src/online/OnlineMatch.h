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

#include "PlatformClient.h"
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

	/// Where the result is: the match still runs on the relay (another player has
	/// not left yet), it ended and the verifier is replaying it, or it is settled.
	enum class Phase
	{
		Waiting,
		Verifying,
		Done,
	};
	/// Waiting expects every player to leave within about this long, Verifying the
	/// verifier to finish; beyond it the card says so (`slow`).
	static constexpr std::uint64_t WAITING_EXPECTED_MS = 45000;
	static constexpr std::uint64_t VERIFYING_EXPECTED_MS = 60000;
	/// How often poll() asks GET /api/v1/matches/{id} while the result is open.
	static constexpr std::uint64_t POLL_MS = 10000;

	/// Starts listening to match.updated on the client (no-op without one).
	void listen(PlatformClient &client);
	/// Applies a MatchSummary (match.updated data.match, GET /api/v1/matches/{id}
	/// match, or a fixture).
	void apply(const Json &summary);
	/// Called every frame by the results screen: re-reads the match from the REST
	/// API every POLL_MS while it is not Done (a missed event cannot leave the card
	/// spinning), and sets `slow` once the phase outlasts its expected time.
	/// Returns true when something shown changed.
	bool poll(std::uint64_t nowMs);
	Phase phase() const;

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
	/// Platform outcome of this account: won | lost | draw | unresolved | abandoned.
	std::string outcome;
	/// MatchSummary status (starting | running | ended | cancelled); empty until the
	/// platform has said anything about the match.
	std::string status;
	/// The phase has lasted longer than expected (see poll()).
	bool slow = false;
	/// Another player opened this quick match's rematch room (match.rematchOffered).
	std::string rematchOfferedBy;
	/// Bumped on every change, so screens can poll cheaply.
	unsigned revision = 0;

	/// <origin>/matches/<id>: the web match page.
	std::string matchPageUrl() const;

  private:
	PlatformClient *client = nullptr;
	// The poll and the listeners, cancelled with the result.
	std::unique_ptr<PlatformScope> calls;
	std::uint64_t lastPoll = 0, phaseSince = 0;
	Phase lastPhase = Phase::Waiting;
	bool polling = false;
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
	/// Load step: the engine's current loading stage key ("[Loading game graphics]"
	/// while the browser is still downloading the game sprites), else empty.
	std::string loadStage() const;
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
	// The reconnect request, cancelled with the match.
	PlatformScope calls;
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
/// newest first, at most 20): fresher than the history API for a moment after a match.
const Json &recentMatches();
void rememberMatch(const Json &summary);
/// The hub's Recent matches: the history API's page (newest first), each row
/// replaced by a summary seen live since, and live summaries the page does not
/// have yet in front; at most `limit`.
Json mergeRecentMatches(const Json &history, const Json &live, std::size_t limit);
} // namespace Online
