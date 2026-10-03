// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OnlineResources.h"
#include "PlatformClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// One quick-match search at a time (screen group 3): probes the relays,
// joins the queue, follows queue.status, answers ranked accept prompts and
// hands the assigned match to the connecting flow (Online::beginMatch).
//
// It lives in the online services rather than in a screen, so a search keeps
// running while the player opens Profile or Maps. Everything runs on the UI
// thread through PlatformClient; update() is called by Online::pump().
namespace Online
{
class RelayProbe;

class QuickMatch
{
  public:
	enum class Phase
	{
		Idle,
		Probing,  // timing relay regions
		Joining,  // queue.join sent
		Searching,
		Proposed, // a ranked prompt waits for accepts (see answer())
		Starting, // grouped without a prompt, or everyone accepted: waiting for the match
		Matched,  // the match is assigned; hand-off after the start countdown
		Failed	  // see error()
	};
	// Short messages for a toast, set when something changed without the
	// player doing anything.
	enum class Notice
	{
		None,
		OpponentDeclined, // back at the front of the queue
		StartFailed,	  // the match could not start; searching again
		Declined,		  // the player declined: removed with a cooldown
		TimedOut,		  // the player did not answer in time
		Cancelled		  // the search ended elsewhere
	};
	struct Environment
	{
		// A relay probe for the instance; null joins without round trips.
		std::function<std::unique_ptr<RelayProbe>(const std::string &origin)> probe;
		// Milliseconds since the Unix epoch.
		std::function<std::int64_t()> wallClock;
		// Asks for the player's attention (window flash) when a match is found.
		std::function<void()> attention;
		// Where an assigned match goes; Online::beginMatch by default.
		std::function<void(const MatchAssignment &)> handoff;
		static Environment native();
	};

	QuickMatch(PlatformClient &client, Environment environment);
	~QuickMatch();
	QuickMatch(const QuickMatch &) = delete;
	QuickMatch &operator=(const QuickMatch &) = delete;

	// -- actions
	void search(const QueueInfo &queue, bool allowAiOpponent = true);
	// Leaves the queue (or the prompt, which counts as declining) and stops a
	// match that has not been handed off yet.
	void cancel();
	// "Allow an AI opponent": sent with queue.update while searching.
	void setAllowAiOpponent(bool allow);
	void respond(bool accept);
	void update();
	// The realtime events the search follows; public for harnesses.
	void handleEvent(const std::string &event, const Json &data);
	void dismissNotice();

	// -- state
	Phase phase() const { return state; }
	bool active() const { return state != Phase::Idle && state != Phase::Failed; }
	const std::optional<QueueInfo> &queue() const { return chosen; }
	bool allowAiOpponent() const { return allowAi; }
	const std::optional<QueueStatus> &status() const { return lastStatus; }
	const std::optional<QueueProposal> &proposal() const { return current; }
	const std::optional<MatchAssignment> &assignment() const { return assigned; }
	// The player's answer to the current prompt, if any.
	std::optional<bool> answer() const { return myAnswer; }
	Notice notice() const { return lastNotice; }
	// The other player who did not accept, for OpponentDeclined.
	const std::string &noticeName() const { return noticeWho; }
	std::optional<std::int64_t> cooldownUntil() const { return cooldown; }
	const ApiError &error() const { return problem; }
	const std::vector<RegionRtt> &regions() const { return probed; }
	// Seconds since the search began (server time when known).
	int waitedSeconds() const;
	std::optional<std::int64_t> backfillInMs() const;
	std::optional<std::int64_t> acceptInMs() const;
	std::optional<std::int64_t> startInMs() const;
	// Bumped on every change; screens rebuild when it moves.
	std::uint64_t revision() const { return changes; }
	// Countdown before a match without an accept prompt is handed off, so the
	// player sees who they play (default 3 s).
	void setStartDelayMs(std::int64_t ms) { startDelay = ms; }
	// Replaces where an assigned match goes (the screens defer it until the
	// match-found screen has closed).
	void setHandoff(std::function<void(const MatchAssignment &)> handoff) { env.handoff = std::move(handoff); }

	// Harness entry: show a search in progress without a server.
	void presentSearching(const QueueInfo &queue, const QueueStatus &status,
						  std::int64_t startedAt);

  private:
	void changed() { ++changes; }
	void join();
	void reset(Phase next);
	void fail(const ApiError &error);

	PlatformClient &client;
	// Requests and listeners with handlers, cancelled with the search.
	PlatformScope calls;
	Environment env;
	Phase state = Phase::Idle;
	std::optional<QueueInfo> chosen;
	bool allowAi = true;
	std::unique_ptr<RelayProbe> probe;
	std::vector<RegionRtt> probed;
	std::optional<PlatformClient::RequestId> joinRequest;
	bool cancelAfterJoin = false;
	std::string ticketId;
	std::int64_t startedAt = 0;
	std::optional<QueueStatus> lastStatus;
	std::int64_t statusAt = 0;
	std::optional<QueueProposal> current;
	std::int64_t proposalAt = 0;
	std::optional<bool> myAnswer;
	std::string matchId;
	std::optional<MatchAssignment> assigned;
	std::int64_t startDelay = 3000;
	Notice lastNotice = Notice::None;
	std::string noticeWho;
	std::optional<std::int64_t> cooldown;
	ApiError problem;
	std::uint64_t changes = 0;
};

// The shared search, created on first use with the online services' client
// and pumped by Online::pump().
QuickMatch &quickMatch();
const char *phaseName(QuickMatch::Phase phase);
} // namespace Online
