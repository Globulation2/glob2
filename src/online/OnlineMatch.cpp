// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "OnlineMatch.h"

#include "Engine.h"
#include "MapCache.h"
#include "MatchSetup.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "RelayTransport.h"
#include "Team.h"
#include "TurnSession.h"
#include "Utilities.h"
#include "GeneratorRegistry.h"
#include "GenerationRequest.h"
#include <StringTable.h>
#include <Toolkit.h>

#include <CooperativeSlice.h>
#include <CooperativeTask.h>

#include <iostream>

namespace Online
{
// --------------------------------------------------------------- results

namespace
{
Json &recentStore()
{
	static Json store = Json::array();
	return store;
}
} // namespace

const Json &recentMatches()
{
	return recentStore();
}

void rememberMatch(const Json &summary)
{
	if (!summary.is_object() || !summary.contains("id"))
		return;
	Json &store = recentStore();
	for (auto it = store.begin(); it != store.end(); ++it)
		if ((*it).value("id", "") == summary.value("id", ""))
		{
			store.erase(it);
			break;
		}
	store.insert(store.begin(), summary);
	while (store.size() > 20)
		store.erase(store.end() - 1);
}

Json mergeRecentMatches(const Json &history, const Json &live, std::size_t limit)
{
	Json merged = Json::array();
	auto idOf = [](const Json &m) { return m.is_object() ? m.value("id", "") : std::string(); };
	auto liveFor = [&](const std::string &id) -> const Json * {
		if (live.is_array())
			for (const auto &m : live)
				if (idOf(m) == id)
					return &m;
		return nullptr;
	};
	auto inHistory = [&](const std::string &id) {
		if (history.is_array())
			for (const auto &m : history)
				if (idOf(m) == id)
					return true;
		return false;
	};
	if (live.is_array())
		for (const auto &m : live)
			if (!idOf(m).empty() && !inHistory(idOf(m)) && merged.size() < limit)
				merged.push_back(m);
	if (history.is_array())
		for (const auto &m : history)
		{
			if (idOf(m).empty() || merged.size() >= limit)
				continue;
			const Json *fresh = liveFor(idOf(m));
			merged.push_back(fresh ? *fresh : m);
		}
	return merged;
}

OnlineMatchResult::OnlineMatchResult(std::string origin, std::string matchId, std::string accountId)
	: origin(std::move(origin)), matchId(std::move(matchId)), accountId(std::move(accountId))
{
}

OnlineMatchResult::~OnlineMatchResult()
{
	*alive = false;
	if (client && listener)
		client->removeListener(listener);
	if (client && rematchListener)
		client->removeListener(rematchListener);
}

OnlineMatchResult::Phase OnlineMatchResult::phase() const
{
	if (verification != Verification::Pending || status == "cancelled")
		return Phase::Done;
	return status == "ended" ? Phase::Verifying : Phase::Waiting;
}

bool OnlineMatchResult::poll(std::uint64_t nowMs)
{
	bool changed = false;
	const Phase current = phase();
	if (phaseSince == 0 || current != lastPhase)
	{
		lastPhase = current;
		phaseSince = nowMs;
		changed = slow;
		slow = false;
	}
	const std::uint64_t expected = current == Phase::Waiting ? WAITING_EXPECTED_MS : VERIFYING_EXPECTED_MS;
	if (current != Phase::Done && !slow && nowMs - phaseSince > expected)
	{
		slow = true;
		changed = true;
	}
	if (client && current != Phase::Done && !polling && (lastPoll == 0 || nowMs - lastPoll >= POLL_MS))
	{
		lastPoll = nowMs;
		polling = true;
		client->rest(HttpFetch::Method::Get, "/api/v1/matches/" + matchId, Json(),
					 [this, alive = alive](const PlatformClient::Response &response) {
						 if (!*alive)
							 return;
						 polling = false;
						 if (response.ok && response.result.contains("match"))
							 apply(response.result["match"]);
					 });
	}
	if (changed)
		++revision;
	return changed;
}

void OnlineMatchResult::listen(PlatformClient &platform)
{
	if (client)
		return;
	client = &platform;
	listener = platform.addListener("match.updated", [this](const std::string &, const Json &data) {
		if (data.contains("match") && data["match"].is_object() && data["match"].value("id", "") == matchId)
			apply(data["match"]);
	});
	rematchListener = platform.addListener("match.rematchOffered", [this](const std::string &, const Json &data) {
		if (data.value("matchId", "") != matchId)
			return;
		rematchOfferedBy = data.value("host", "?");
		++revision;
	});
}

void OnlineMatchResult::apply(const Json &summary)
{
	if (!summary.is_object())
		return;
	rememberMatch(summary);
	const std::string status = summary.value("verification", "pending");
	if (status == "verified")
		verification = Verification::Verified;
	else if (status == "diverged")
		verification = Verification::Diverged;
	else if (status == "unverifiable")
		verification = Verification::Unverifiable;
	else if (status == "not_applicable")
		verification = Verification::NotApplicable;
	else
		verification = Verification::Pending;
	rated = summary.value("rated", rated);
	if (summary.contains("status") && summary["status"].is_string())
		this->status = summary["status"].get<std::string>();
	fromRoom = summary.value("origin", fromRoom ? "room" : "queue") == "room";
	if (summary.contains("mapTitle") && summary["mapTitle"].is_string())
		mapTitle = summary["mapTitle"].get<std::string>();
	if (summary.contains("participants") && summary["participants"].is_array())
		for (const auto &participant : summary["participants"])
		{
			if (participant.value("accountId", "") != accountId || accountId.empty())
				continue;
			if (participant.contains("outcome") && participant["outcome"].is_string())
				outcome = participant["outcome"].get<std::string>();
			if (participant.contains("rating") && participant["rating"].is_object())
			{
				const Json &rating = participant["rating"];
				ladder = rating.value("ladder", ladder);
				if (rating.contains("before") && rating["before"].is_number())
					ratingBefore = rating["before"].get<double>();
				if (rating.contains("after") && rating["after"].is_number())
					ratingAfter = rating["after"].get<double>();
				provisional = rating.value("provisional", provisional);
			}
		}
	++revision;
}

std::string OnlineMatchResult::matchPageUrl() const
{
	return origin + "/matches/" + matchId;
}

// --------------------------------------------------------------- match flow

struct OnlineMatch::Running
{
	std::unique_ptr<MapCache::Download> download;
	std::unique_ptr<Engine> engine;
	std::optional<GAGCore::CooperativeTask> task;
	GAGCore::CooperativeSlice slice;
	std::shared_ptr<Turn::TurnTransport> transport;
	std::string previousRng = getSyncRandState();
	bool accepted = false;
	~Running()
	{
		task.reset();
		if (engine && !accepted)
		{
			engine->cancelInitialization();
			engine.reset();
			setSyncRandState(previousRng);
		}
		if (transport && !accepted)
			transport->close();
	}
};

const char *stepName(OnlineMatch::Step step)
{
	switch (step)
	{
	case OnlineMatch::Step::Seat:
		return "seat";
	case OnlineMatch::Step::Map:
		return "map";
	case OnlineMatch::Step::Load:
		return "load";
	case OnlineMatch::Step::Relay:
		return "relay";
	case OnlineMatch::Step::Players:
		return "players";
	case OnlineMatch::Step::Playing:
		return "playing";
	case OnlineMatch::Step::Failed:
		return "failed";
	}
	return "?";
}

OnlineMatch::OnlineMatch(PlatformClient &client, Json assignment, Context context)
	: client(client), assignment(std::move(assignment)), ctx(std::move(context))
{
	makeTransport = [](const std::string &url) { return std::make_shared<RelayTransport>(url); };
	parseAssignment();
	const auto &account = client.account();
	outcome = std::make_shared<OnlineMatchResult>(client.origin(), id, account ? account->id : std::string());
	outcome->label = ctx.label;
	outcome->fromRoom = ctx.fromRoom;
	outcome->rated = ctx.rated;
	outcome->ladder = ctx.ladder;
	outcome->mapTitle = mapTitle();
	// Optional preview of the rating change before verification (not in the
	// protocol yet: MatchAssignment.ratingPreview {ladder, before, ifWon, ifLost}).
	if (this->assignment.contains("ratingPreview") && this->assignment["ratingPreview"].is_object())
	{
		const Json &preview = this->assignment["ratingPreview"];
		if (preview.contains("before") && preview["before"].is_number())
			outcome->ratingBefore = preview["before"].get<double>();
		if (preview.contains("ifWon") && preview["ifWon"].is_number())
			outcome->ratingExpectedWin = preview["ifWon"].get<double>();
		if (preview.contains("ifLost") && preview["ifLost"].is_number())
			outcome->ratingExpectedLoss = preview["ifLost"].get<double>();
		outcome->ladder = preview.value("ladder", outcome->ladder);
		outcome->provisional = preview.value("provisional", false);
	}
	outcome->listen(client);
}

OnlineMatch::~OnlineMatch()
{
	if (reconnectRequest)
		client.cancelRequest(reconnectRequest);
}

void OnlineMatch::parseAssignment()
{
	try
	{
		id = assignment.at("matchId").get<std::string>();
		seat = assignment.at("seat").get<int>();
		relayUrl = assignment.at("relayUrl").get<std::string>();
		ticket = assignment.at("ticket").get<std::string>();
		setupJson = assignment.at("setup");
		// Validate now so a bad setup fails at the first step, not after a download.
		MatchSetup::fromJson(setupJson);
	}
	catch (const std::exception &error)
	{
		fail(std::string("the match assignment is invalid: ") + error.what());
	}
}

void OnlineMatch::fail(const std::string &why)
{
	problem = why;
	current = Step::Failed;
	std::cerr << "Online match " << id << ": " << why << std::endl;
}

void OnlineMatch::reset()
{
	run.reset();
}

void OnlineMatch::startMap()
{
	run = std::make_unique<Running>();
	const std::string hash = setupJson["map"].value("hash", "");
	auto &maps = services().maps;
	cachedMap = maps.contains(hash);
	HttpFetch::Headers headers;
	if (!client.accessToken().empty())
		headers.push_back({"Authorization", "Bearer " + client.accessToken()});
	run->download = maps.fetch(client.origin(), hash, headers);
	current = Step::Map;
}

void OnlineMatch::startLoad()
{
	try
	{
		Engine::TurnMatchStart start;
		start.setup = MatchSetup::fromJson(setupJson);
		start.mapFile = resolveMatchMap(start.setup, mapPath);
		start.localSeat = seat;
		run->transport = makeTransport(relayUrl);
		start.transport = run->transport;
		start.config.ticket = ticket;
		run->engine = std::make_unique<Engine>();
		run->task.emplace(run->engine->initTurnMatchTask(std::move(start)));
		current = Step::Load;
	}
	catch (const std::exception &error)
	{
		fail(error.what());
	}
}

void OnlineMatch::reportRelayUnavailable()
{
	reconnectPending = true;
	++moves;
	std::cerr << "Online match " << id << ": the relay refused the match as new; asking for another relay"
			  << std::endl;
	reconnectRequest = client.request("match.reconnect", Json{{"matchId", id}, {"relayUnavailable", true}},
				   [this](const PlatformClient::Response &response) {
					   reconnectPending = false;
					   reconnectRequest = 0;
					   if (!response.ok)
					   {
						   fail("no relay could take the match (" + response.error.code + ")");
						   return;
					   }
					   assignment = response.result;
					   reset();
					   parseAssignment();
					   if (current != Step::Failed)
						   startMap();
				   });
}

void OnlineMatch::update(std::uint64_t nowMs)
{
	if (previewing || current == Step::Failed || current == Step::Playing || reconnectPending)
		return;
	if (current == Step::Seat)
	{
		startMap();
		return;
	}
	if (current == Step::Map)
	{
		switch (run->download->state())
		{
		case MapCache::Download::State::Pending:
			return;
		case MapCache::Download::State::Failed:
			fail("the map could not be downloaded: " + run->download->error());
			return;
		case MapCache::Download::State::Done:
			mapPath = run->download->path();
			startLoad();
			return;
		}
	}
	if (current == Step::Load)
	{
		try
		{
			if (!run->slice.advance(*run->task))
				return;
			if (!run->task->result())
			{
				fail("the game could not be loaded: " + run->engine->getInitializationDiagnostic());
				return;
			}
		}
		catch (const std::exception &error)
		{
			fail(std::string("the game could not be loaded: ") + error.what());
			return;
		}
		run->task.reset();
		loadedAt = nowMs;
		current = Step::Relay;
	}
	auto *session = run && run->engine ? run->engine->turnSession() : nullptr;
	if (!session)
		return;
	session->update(nowMs * 1000);
	switch (session->state())
	{
	case Turn::TurnSession::State::Rejected:
		if (session->rejectReason() == Turn::RejectReason::MatchOver && session->executedTick() == 0 && moves < 3)
			reportRelayUnavailable();
		else
			fail("the relay refused this client (reason " + std::to_string(int(session->rejectReason())) + ")");
		return;
	case Turn::TurnSession::State::Running:
		current = Step::Players;
		if (session->horizon() > 0)
			current = Step::Playing;
		return;
	default:
		return;
	}
}

void OnlineMatch::leave()
{
	if (run && run->engine)
		if (auto *session = run->engine->turnSession())
			session->quit();
	run.reset();
	current = Step::Failed;
	problem.clear();
}

std::unique_ptr<Engine> OnlineMatch::takeEngine()
{
	if (current != Step::Playing || !run || !run->engine)
		return {};
	run->accepted = true;
	run->engine->setOnlineResult(outcome);
	auto engine = std::move(run->engine);
	// The engine's session keeps its own reference to the transport.
	run.reset();
	return engine;
}

std::string OnlineMatch::relayName() const
{
	if (previewing)
		return previewRelay;
	std::string name = relayUrl;
	const auto scheme = name.find("://");
	if (scheme != std::string::npos)
		name = name.substr(scheme + 3);
	while (!name.empty() && name.back() == '/')
		name.pop_back();
	const auto slash = name.rfind('/');
	return slash == std::string::npos ? name : name.substr(slash + 1);
}

int OnlineMatch::relayRttMs() const
{
	if (previewing)
		return previewRtt;
	if (!run || !run->engine)
		return -1;
	auto *session = run->engine->turnSession();
	return session && session->rttMicros() > 0 ? int(session->rttMicros() / 1000) : -1;
}

std::string OnlineMatch::mapTitle() const
{
	if (assignment.contains("mapTitle") && assignment["mapTitle"].is_string())
		return assignment["mapTitle"].get<std::string>();
	if (setupJson.contains("map") && setupJson["map"].contains("generator"))
	{
		// The landscape's own (translated) name, as the custom-game screen shows it.
		const std::string id = setupJson["map"]["generator"].value("generatorId", "");
		const int method = GeneratorRegistry::builtins().idOf(id);
		if (!GeneratorRegistry::builtins().find(method))
			return id;
		std::string name = GAGCore::Toolkit::getStringTable()->getString("[" + std::string(GenerationRequest::methodName(method)) + "]");
		if (name.size() > 2 && name.front() == '[' && name.back() == ']')
			name = name.substr(1, name.size() - 2);
		return name;
	}
	return {};
}

void OnlineMatch::preview(Step step, std::vector<Player> players, std::string relay, int rttMs)
{
	previewing = true;
	current = step;
	previewPlayers = std::move(players);
	previewRelay = std::move(relay);
	previewRtt = rttMs;
}

std::vector<OnlineMatch::Player> OnlineMatch::players() const
{
	if (previewing)
		return previewPlayers;
	std::vector<Player> result;
	if (!setupJson.contains("seats") || !setupJson["seats"].is_array())
		return result;
	Engine *engine = run ? run->engine.get() : nullptr;
	auto *session = engine && current >= Step::Relay ? engine->turnSession() : nullptr;
	for (const auto &entry : setupJson["seats"])
	{
		Player player;
		player.seat = entry.value("seat", 0);
		player.name = entry.value("name", "");
		player.ai = !entry.value("human", true);
		player.local = player.seat == seat;
		const int team = entry.value("team", player.seat);
		if (engine && current >= Step::Relay)
			if (team >= 0 && team < Team::MAX_COUNT)
				if (Team *t = engine->gameTeam(team))
				{
					player.r = t->color.r;
					player.g = t->color.g;
					player.b = t->color.b;
				}
		if (player.ai)
		{
			player.state = "ready";
			player.progress = 100;
		}
		else if (player.local)
		{
			switch (current)
			{
			case Step::Seat:
			case Step::Map:
				player.state = "map";
				player.progress = 20;
				break;
			case Step::Load:
				player.state = "loading";
				player.progress = 60;
				break;
			case Step::Relay:
				player.state = "connecting";
				player.progress = 85;
				break;
			default:
				player.state = "ready";
				player.progress = 100;
				break;
			}
		}
		else if (session)
		{
			switch (session->presence(player.seat))
			{
			case Turn::PresenceState::Connected:
			case Turn::PresenceState::Lagging:
				player.state = "ready";
				player.progress = 100;
				break;
			case Turn::PresenceState::Left:
				player.state = "left";
				player.progress = 0;
				break;
			default:
				player.state = "connecting";
				break;
			}
		}
		else
			player.state = "connecting";
		result.push_back(player);
	}
	return result;
}
} // namespace Online
