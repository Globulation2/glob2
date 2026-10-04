// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickMatch.h"

#include "RelayProbe.h"

#include <algorithm>
#include <chrono>

namespace Online
{
namespace
{
const char *QUEUE_EVENTS[] = {"queue.status", "queue.proposal", "queue.proposalEnded",
							  "queue.matchFound", "match.start"};

std::int64_t systemWallClock()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

} // namespace

const char *phaseName(QuickMatch::Phase phase)
{
	switch (phase)
	{
	case QuickMatch::Phase::Idle:
		return "idle";
	case QuickMatch::Phase::Probing:
		return "probing";
	case QuickMatch::Phase::Joining:
		return "joining";
	case QuickMatch::Phase::Searching:
		return "searching";
	case QuickMatch::Phase::Proposed:
		return "proposed";
	case QuickMatch::Phase::Starting:
		return "starting";
	case QuickMatch::Phase::Matched:
		return "matched";
	case QuickMatch::Phase::Failed:
		return "failed";
	}
	return "unknown";
}

QuickMatch::QuickMatch(PlatformClient &client, Environment environment)
	: client(client), calls(client), env(std::move(environment))
{
	if (!env.wallClock)
		env.wallClock = systemWallClock;
	for (const char *event : QUEUE_EVENTS)
		calls.listen(event, [this](const std::string &name, const Json &data)
					 { handleEvent(name, data); });
}

// calls (a member) cancels the requests and removes the listeners. Leaving a
// ticket goes through the client directly: it has no handler and must still
// reach the server.
QuickMatch::~QuickMatch() = default;

void QuickMatch::reset(Phase next)
{
	if (joinRequest)
		calls.cancel(*joinRequest);
	joinRequest.reset();
	probe.reset();
	cancelAfterJoin = false;
	ticketId.clear();
	tickets.clear();
	statuses.clear();
	lastStatus.reset();
	current.reset();
	myAnswer.reset();
	matchId.clear();
	assigned.reset();
	state = next;
	changed();
}

void QuickMatch::fail(const ApiError &error)
{
	reset(Phase::Failed);
	problem = error;
	if (error.code == "rate_limited" && error.details.is_object())
		if (auto until = error.details.find("until"); until != error.details.end() && until->is_string())
			cooldown = parseTimestamp(until->get<std::string>());
	changed();
}

void QuickMatch::search(const QueueInfo &queue, bool allowAiOpponent)
{
	search(std::vector<QueueInfo>{queue}, allowAiOpponent);
}

void QuickMatch::search(const std::vector<QueueInfo> &queues, bool allowAiOpponent)
{
	if (queues.empty())
		return;
	if (active())
		cancel();
	reset(Phase::Probing);
	problem = {};
	cooldown.reset();
	lastNotice = Notice::None;
	searched = queues;
	chosen = queues.front();
	allowAi = allowAiOpponent;
	startedAt = env.wallClock();
	probed.clear();
	if (env.probe && !client.origin().empty())
		probe = env.probe(client.origin());
	if (!probe)
		join();
	changed();
}

void QuickMatch::join()
{
	probe.reset();
	state = Phase::Joining;
	Json params = {{"queueId", searched.front().id}, {"regions", regionsJson(probed)}, {"allowAiOpponent", allowAi}};
	// Further queues of the same search ('queue.multi' servers only; the hub checks).
	if (searched.size() > 1)
	{
		Json more = Json::array();
		for (std::size_t i = 1; i < searched.size(); ++i)
			more.push_back(searched[i].id);
		params["queueIds"] = std::move(more);
	}
	joinRequest = calls.request(
		"queue.join", std::move(params),
		[this](const PlatformClient::Response &response)
		{
			joinRequest.reset();
			if (!response.ok)
			{
				fail(response.error);
				return;
			}
			const std::string ticket = response.result.value("ticketId", std::string());
			if (cancelAfterJoin)
			{
				if (!ticket.empty())
					client.request("queue.leave", Json{{"ticketId", ticket}}, {});
				reset(Phase::Idle);
				return;
			}
			ticketId = ticket;
			tickets[ticket] = searched.front().id;
			for (const auto &entry : response.result.value("tickets", Json::array()))
				if (entry.is_object() && entry.contains("ticketId") && entry["ticketId"].is_string())
					tickets[entry["ticketId"].get<std::string>()] = entry.value("queueId", std::string());
			if (auto joined = parseTimestamp(response.result.value("joinedAt", std::string())))
				startedAt = *joined;
			if (state == Phase::Joining)
				state = Phase::Searching;
			changed();
		});
	changed();
}

void QuickMatch::cancel()
{
	switch (state)
	{
	case Phase::Idle:
		return;
	case Phase::Failed:
		reset(Phase::Idle);
		return;
	case Phase::Probing:
		reset(Phase::Idle);
		return;
	case Phase::Joining:
		// The ticket is left as soon as join answers.
		cancelAfterJoin = true;
		state = Phase::Idle;
		changed();
		return;
	default:
		break;
	}
	// Searching, a prompt (leaving declines it) or a starting match. A match
	// the server already created is simply not joined; the relay ends it as
	// no contest when nobody connects.
	if (!ticketId.empty() && !assigned)
		client.request("queue.leave", Json{{"ticketId", ticketId}}, {});
	lastNotice = Notice::None;
	reset(Phase::Idle);
}

void QuickMatch::setAllowAiOpponent(bool allow)
{
	if (allow == allowAi)
		return;
	allowAi = allow;
	if (!ticketId.empty() && (state == Phase::Searching || state == Phase::Proposed))
		calls.request("queue.update", Json{{"ticketId", ticketId}, {"allowAiOpponent", allow}},
					  [this, allow](const PlatformClient::Response &response)
					  {
						  if (!response.ok && allowAi == allow && state == Phase::Searching)
						  {
							  allowAi = !allow;
							  changed();
						  }
					  });
	changed();
}

void QuickMatch::respond(bool accept)
{
	if (state != Phase::Proposed || !current || myAnswer)
		return;
	myAnswer = accept;
	const std::string proposalId = current->proposalId;
	calls.request("queue.respond", Json{{"proposalId", proposalId}, {"accept", accept}},
				  [this, accept, proposalId](const PlatformClient::Response &response)
				  {
					  if (!current || current->proposalId != proposalId)
						  return;
					  if (!response.ok)
					  {
						  // The prompt ended meanwhile; proposalEnded says how.
						  myAnswer.reset();
						  changed();
						  return;
					  }
					  if (!accept)
					  {
						  lastNotice = Notice::Declined;
						  reset(Phase::Idle);
					  }
				  });
	changed();
}

void QuickMatch::dismissNotice()
{
	if (lastNotice == Notice::None)
		return;
	lastNotice = Notice::None;
	noticeWho.clear();
	changed();
}

void QuickMatch::handleEvent(const std::string &event, const Json &data)
{
	if (event == "queue.status")
	{
		auto status = QueueStatus::fromJson(data);
		if (!status || !ownsTicket(status->ticketId) || state != Phase::Searching)
			return;
		statuses[status->queueId] = *status;
		lastStatus = status;
		statusAt = env.wallClock();
		startedAt = statusAt - status->waitedSeconds * 1000ll;
		changed();
	}
	else if (event == "queue.proposal")
	{
		auto proposal = QueueProposal::fromJson(data);
		if (!proposal || !ownsTicket(proposal->ticketId))
			return;
		// The queue that found the match is the one the prompt and the match are about.
		showQueue(proposal->queueId);
		if (state != Phase::Searching && state != Phase::Proposed && state != Phase::Starting)
			return;
		const bool fresh = !current || current->proposalId != proposal->proposalId;
		current = proposal;
		if (fresh)
		{
			proposalAt = env.wallClock();
			myAnswer.reset();
			lastNotice = Notice::None;
			if (env.attention)
				env.attention();
		}
		if (const auto *own = current->own())
			if (own->response == "accepted")
				myAnswer = true;
		if (state != Phase::Starting)
			state = proposal->requiresAccept ? Phase::Proposed : Phase::Starting;
		changed();
	}
	else if (event == "queue.proposalEnded")
	{
		auto ended = ProposalEnded::fromJson(data);
		if (!ended || !ownsTicket(ended->ticketId))
			return;
		if (ended->outcome == "requeued")
		{
			lastNotice = ended->reason == "start_failed" ? Notice::StartFailed : Notice::OpponentDeclined;
			noticeWho.clear();
			if (current)
				for (const auto &seat : current->seats)
					if (seat.human && !seat.you && seat.response != "accepted")
					{
						noticeWho = seat.displayName;
						break;
					}
			current.reset();
			myAnswer.reset();
			// Every queue of the search is searching again.
			if (!searched.empty())
				chosen = searched.front();
			state = Phase::Searching;
			changed();
			return;
		}
		const Notice notice = ended->reason == "timeout" ? Notice::TimedOut : Notice::Declined;
		cooldown = ended->cooldownUntil;
		reset(Phase::Idle);
		lastNotice = notice;
		changed();
	}
	else if (event == "queue.matchFound")
	{
		const std::string found = data.value("ticketId", std::string());
		if (found.empty() || !ownsTicket(found))
			return;
		showQueue(tickets[found]);
		matchId = data.value("matchId", std::string());
		if (state == Phase::Proposed || state == Phase::Searching)
			state = Phase::Starting;
		changed();
	}
	else if (event == "match.start")
	{
		auto assignment = MatchAssignment::fromJson(data);
		if (!assignment || (state != Phase::Starting && state != Phase::Proposed))
			return;
		if (!matchId.empty() && assignment->matchId != matchId)
			return;
		matchId = assignment->matchId;
		assigned = assignment;
		state = Phase::Matched;
		changed();
		update();
	}
}

void QuickMatch::update()
{
	if (state == Phase::Probing && probe)
	{
		probe->update();
		if (probe->done())
		{
			probed = probe->results();
			join();
		}
	}
	if (state == Phase::Matched && assigned)
	{
		const auto wait = startInMs();
		if (!wait || *wait <= 0)
		{
			const auto match = *assigned;
			reset(Phase::Idle);
			if (env.handoff)
				env.handoff(match);
		}
	}
}

int QuickMatch::waitedSeconds() const
{
	if (!active())
		return 0;
	return static_cast<int>(std::max<std::int64_t>(0, env.wallClock() - startedAt) / 1000);
}

std::optional<std::int64_t> QuickMatch::backfillInMs() const
{
	if (!allowAi)
		return {};
	// The soonest AI among the search's queues: whichever fills first ends the search.
	std::optional<std::int64_t> soonest;
	for (const auto &[queueId, status] : statuses)
		if (status.aiBackfillAt && (!soonest || *status.aiBackfillAt < *soonest))
			soonest = status.aiBackfillAt;
	if (!soonest && lastStatus && lastStatus->aiBackfillAt)
		soonest = lastStatus->aiBackfillAt;
	if (!soonest)
		return {};
	return std::max<std::int64_t>(0, *soonest - env.wallClock());
}

void QuickMatch::showQueue(const std::string &queueId)
{
	for (const auto &queue : searched)
		if (queue.id == queueId)
			chosen = queue;
}

std::optional<std::int64_t> QuickMatch::acceptInMs() const
{
	if (state != Phase::Proposed || !current || !current->expiresAt)
		return {};
	return std::max<std::int64_t>(0, *current->expiresAt - env.wallClock());
}

std::optional<std::int64_t> QuickMatch::startInMs() const
{
	if (!current)
		return {};
	// Accepted prompts start at once; others give a short look at the opponent.
	if (current->requiresAccept)
		return 0;
	return std::max<std::int64_t>(0, proposalAt + startDelay - env.wallClock());
}

void QuickMatch::presentSearching(const QueueInfo &queue, const QueueStatus &status,
								  std::int64_t searchStartedAt)
{
	reset(Phase::Searching);
	chosen = queue;
	searched = {queue};
	ticketId = status.ticketId;
	tickets[status.ticketId] = queue.id;
	statuses[queue.id] = status;
	allowAi = status.allowAiOpponent.value_or(true);
	lastStatus = status;
	startedAt = searchStartedAt;
	changed();
}

} // namespace Online
