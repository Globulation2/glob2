// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#include "OnlineHubScreen.h"
#include "CustomGamePreferences.h"
#include "GlobalContainer.h"
#include "InstanceConfig.h"
#include "InviteLink.h"
#include "MapCatalog.h"
#include "MatchStartScreen.h"
#include "MessageScreen.h"
#include "OnlineHandoff.h"
#include "OnlineMapsScreen.h"
#include "OnlineProfileScreen.h"
#include "QuickMatch.h"
#include "QuickMatchScreen.h"
#include "OnlineMatch.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "PlatformRoom.h"
#include "RoomSetup.h"
#include "RoomScreen.h"
#include "SettingsScreen.h"
#include "SimVersion.h"
#include "ThumbSide.h"
#include "ui/OnlineUI.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <random>
#include <utility>

#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "Globulation 2"
#endif

using namespace Glob2UI;
using Online::Json;

namespace
{
constexpr Uint32 REFRESH_MS = 15000;
constexpr Uint32 TOAST_MS = 6000;

OnlineHubScreen::QuickMatch &quickMatchStarter()
{
	static OnlineHubScreen::QuickMatch start;
	return start;
}

std::string formatted(const char *key, const std::string &value)
{
	return GAGCore::FormattableString(tr(key)).arg(value);
}

std::string hostOf(const std::string &origin)
{
	const auto scheme = origin.find("://");
	return scheme == std::string::npos ? origin : origin.substr(scheme + 3);
}

std::string providerName(const std::string &id)
{
	if (id == "google")
		return "Google";
	if (id == "microsoft")
		return "Microsoft";
	if (id == "apple")
		return "Apple";
	return id;
}

// One line about a match for Recent matches: outcome letter, opponents, details, rating.
struct RecentLine
{
	std::string outcome, title, detail, rating;
	bool verified = false;
};
RecentLine recentLine(const Json &match, const std::string &me)
{
	RecentLine line;
	std::vector<std::string> others;
	std::string myOutcome;
	for (const auto &p : match.value("participants", Json::array()))
	{
		if (p.value("accountId", "") == me && !me.empty())
		{
			myOutcome = p.value("outcome", "");
			if (p.contains("rating") && p["rating"].is_object() && p["rating"].contains("after") && p["rating"].contains("before"))
			{
				const int delta = int(std::lround(p["rating"].value("after", 0.0) - p["rating"].value("before", 0.0)));
				line.rating = (delta >= 0 ? "+" : "−") + std::to_string(std::abs(delta));
			}
		}
		else
			others.push_back(p.value("displayName", ""));
	}
	line.outcome = myOutcome == "won" ? "W" : myOutcome == "lost" || myOutcome == "abandoned" ? "L" : myOutcome == "draw" ? "D" : "–";
	std::string versus;
	for (std::size_t i = 0; i < others.size() && i < 3; ++i)
		versus += (i ? ", " : "") + others[i];
	// The queue's configured name (the server sends it; older servers only the id).
	line.title = match.value("origin", "room") == "room"
					 ? tr("[hub room match]")
					 : queueDisplayName(match.value("queueId", ""), match.value("queueName", ""));
	if (!versus.empty())
		line.title += " · " + formatted("[hub versus %0]", versus);
	line.detail = match.value("mapTitle", "");
	if (match.contains("durationTicks") && match["durationTicks"].is_number_integer())
		line.detail += (line.detail.empty() ? "" : " · ") + durationText(match["durationTicks"].get<int>() / 25);
	if (!match.value("rated", false))
		line.rating = tr("[hub unrated]");
	line.verified = match.value("verification", "") == "verified";
	return line;
}
} // namespace

void OnlineHubScreen::setQuickMatch(QuickMatch start)
{
	quickMatchStarter() = std::move(start);
}

OnlineHubScreen::OnlineHubScreen(GAGGUI::ScreenStack &screens, bool connect)
	: screens(screens), pictures(std::make_unique<MapPictures>()), previews(std::make_unique<PreviewImages>())
{
	auto &services = Online::services();
	auto &platform = services.client;
	previewing = !connect;
	if (connect && platform.connection() == Online::PlatformClient::Connection::Stopped)
		platform.start(services.config.selectedOrigin());
	calls = std::make_unique<Online::PlatformScope>(platform);
	calls->onStateChange([this] { syncFromClient(); });
	calls->listen("match.updated", [](const std::string &, const Json &data) {
		if (data.contains("match"))
			Online::rememberMatch(data["match"]);
	});
	if (connect)
	{
		// The hub owns the way into a match: quick matches (and their match-found
		// prompt over any screen) start here, and so does a rematch room.
		QuickMatchPresenter::attach(screens);
		Online::setMatchHandler([this](const Online::MatchAssignment &assignment) {
			startMatch(assignment.raw.is_object() ? assignment.raw : Json());
		});
		Online::setRematchHandler([this](const Online::RematchRequest &request) {
			rematchRoom = Online::PlatformRoom::rematch(client(), Online::services().maps, Online::services().storage, request.matchId);
		});
		Online::setQueueAgainHandler([this] { queueAgain = true; });
		syncFromClient();
		refresh(true);
	}
}

OnlineHubScreen::~OnlineHubScreen()
{
	if (!previewing)
	{
		Online::setMatchHandler({});
		Online::setRematchHandler({});
		Online::setQueueAgainHandler({});
	}
	// calls (a member) cancels this screen's requests and listeners.
}

void OnlineHubScreen::startMatch(const Json &assignment)
{
	if (!assignment.is_object())
		return;
	// The search screen closes first (quickMatchClosed starts the match), so that
	// "Back to online" from the results lands here rather than on that screen.
	if (auto *search = dynamic_cast<QuickMatchScreen *>(screens.top()))
	{
		deferredMatch = assignment;
		search->handOff();
		return;
	}
	Online::OnlineMatch::Context context;
	context.fromRoom = false;
	if (const auto &queue = searchModel().queue())
	{
		context.label = queue->name.empty() ? queue->id : queue->name;
		context.label += " · " + tr(queue->rated ? "[hub ranked]" : "[hub unrated]");
		context.rated = queue->rated;
		context.ladder = queue->id;
		matchQueueId = searchModel().queues().empty() ? queue->id : searchModel().queues().front().id;
	}
	queueAgain = false;
	auto match = std::make_shared<Online::OnlineMatch>(client(), Online::services().maps, Online::services().storage, assignment, context);
	screens.push(std::make_unique<MatchStartScreen>(screens, match), [this](GAGGUI::Screen &, int) {
		// Rematch from the results screen: its room opens once the match has closed.
		if (auto room = std::move(rematchRoom))
			enterRoom(std::move(room));
		// "Find another match": the same queue, from the hub the results return to.
		else if (std::exchange(queueAgain, false))
			// The same queue, with the same Also search choices.
			findMatch(queueIndex(matchQueueId));
		refresh(true);
	});
}

void OnlineHubScreen::openProfile()
{
	screens.push(std::make_unique<OnlineProfileScreen>(screens), [this](GAGGUI::Screen &, int) { refresh(true); });
}

void OnlineHubScreen::openMaps(bool mine)
{
	auto maps = std::make_unique<OnlineMapsScreen>(screens, mine ? OnlineMapsScreen::Tab::Mine : OnlineMapsScreen::Tab::Browse);
	maps->setOrigin(OnlineMapsScreen::Origin::Hub);
	screens.push(std::move(maps), [this](GAGGUI::Screen &, int result) {
		// "Use in a room": the map is kept for the next room this player hosts (Online::useMapInRoom).
		if (result == OnlineMapsScreen::OPEN_ROOM)
			createRoom();
	});
}

Online::PlatformClient &OnlineHubScreen::client()
{
	return Online::services().client;
}

Online::QuickMatch &OnlineHubScreen::searchModel()
{
	return previewSearch ? *previewSearch : Online::quickMatch();
}

void OnlineHubScreen::preview(Model model)
{
	previewing = true;
	data = std::move(model);
	invalidate();
}

void OnlineHubScreen::syncFromClient()
{
	if (previewing)
		return;
	auto &platform = client();
	data.origin = platform.origin();
	using C = Online::PlatformClient::Connection;
	const auto connection = platform.connection();
	if (connection == C::Online)
	{
		data.link = platform.simSupported() ? Model::Link::Online : Model::Link::UpdateRequired;
		using Mismatch = Online::PlatformClient::SimMismatch;
		const auto mismatch = platform.simMismatch();
		data.outdated = mismatch == Mismatch::ClientBehind	 ? Model::Outdated::Client
						: mismatch == Mismatch::ServerBehind ? Model::Outdated::Server
															 : Model::Outdated::Unknown;
	}
	else if (connection == C::Waiting || (connection == C::Stopped && !platform.lastError().empty()))
		data.link = Model::Link::Offline;
	else
		data.link = Model::Link::Connecting;
	data.retryInSeconds = int((platform.retryInMs() + 999) / 1000);
	if (const auto &account = platform.account())
	{
		data.displayName = account->displayName;
		data.accountKind = account->kind;
		data.accountId = account->id;
		data.linkedProviders.clear();
		for (const auto &identity : account->raw.value("identities", Json::array()))
			data.linkedProviders.push_back(identity.value("provider", ""));
	}
	else
	{
		data.displayName.clear();
		data.accountKind.clear();
	}
	const auto &handoff = platform.handoff();
	using H = Online::PlatformClient::Handoff::State;
	if (handoff.state == H::Starting || handoff.state == H::Waiting)
	{
		data.signIn = Model::SignIn::Waiting;
		data.confirmationCode = handoff.confirmationCode;
		data.browserOpened = handoff.state == H::Starting || handoff.browserOpened;
	}
	else if (data.signIn == Model::SignIn::Waiting)
	{
		data.signIn = Model::SignIn::Closed;
		if (handoff.state == H::Completed)
			showToast(formatted("[hub signed in as %0]", data.displayName));
		else if (handoff.state == H::Failed && handoff.failure != "cancelled")
			showToast(handoff.failure == "conflict" ? tr("[hub sign in conflict]") : formatted("[hub sign in failed %0]", handoff.failure));
	}
	data.recent = Online::mergeRecentMatches(history, Online::recentMatches(), 5);
	invalidate();
}

void OnlineHubScreen::showToast(const std::string &text)
{
	data.toast = text;
	toastAt = now ? now : 1;
	invalidate();
}

void OnlineHubScreen::refresh(bool force)
{
	if (previewing || data.link == Model::Link::Offline)
		return;
	if (!force && now - lastRefresh < REFRESH_MS)
		return;
	lastRefresh = now;
	if (!fetchingInstance && data.instanceName.empty())
	{
		fetchingInstance = true;
		calls->instanceInfo([this](const Online::PlatformClient::Response &r) {
			fetchingInstance = false;
			if (!r.ok)
				return;
			data.instanceName = r.result.value("name", "");
			data.queues = r.result.value("queues", Json::array());
			data.multiQueue = false;
			for (const auto &feature : r.result.value("features", Json::array()))
				if (feature == "queue.multi")
					data.multiQueue = true;
			data.providers = r.result.value("authProviders", Json::array());
			invalidate();
			// The leaderboard teaser needs the queue list.
			refresh(true);
		});
	}
	// Recent matches come from the history API; a summary pushed live since
	// (match.updated) replaces its row until the next fetch.
	if (!fetchingHistory && !data.accountId.empty())
	{
		fetchingHistory = true;
		calls->rest(HttpFetch::Method::Get, Online::Api::playerMatches(data.accountId, 5), Json(),
					[this, account = data.accountId](const Online::PlatformClient::Response &r) {
						fetchingHistory = false;
						if (!r.ok || account != data.accountId)
							return;
						history = r.result.value("items", Json::array());
						data.recent = Online::mergeRecentMatches(history, Online::recentMatches(), 5);
						invalidate();
					});
	}
	// The Leaderboard section: the top of the main queue's ladder (the first rated
	// one), fetched only while it is shown, and where this account stands on it.
	if (section == Section::Leaderboard && !fetchingLeaderboard && data.queues.is_array() && !data.queues.empty())
	{
		const Json *main = &data.queues[0];
		for (const auto &queue : data.queues)
			if (queue.value("rated", false))
			{
				main = &queue;
				break;
			}
		const std::string ladder = main->value("id", "");
		if (!ladder.empty())
		{
			fetchingLeaderboard = true;
			data.leaderboardName = main->value("name", ladder);
			calls->rest(HttpFetch::Method::Get, Online::Api::leaderboard(ladder, 50), Json(),
						[this](const Online::PlatformClient::Response &r) {
							fetchingLeaderboard = false;
							if (!r.ok)
								return;
							data.leaderboard = r.result.value("entries", Json::array());
							invalidate();
						});
			if (!fetchingStanding && data.accountKind == "registered")
			{
				fetchingStanding = true;
				calls->rest(HttpFetch::Method::Get, Online::Api::player(data.accountId), Json(),
							[this, ladder](const Online::PlatformClient::Response &r) {
								fetchingStanding = false;
								if (!r.ok)
									return;
								data.myRank = 0;
								if (const auto profile = Online::PlayerProfile::fromJson(r.result))
									for (const auto &rating : profile->ratings)
										if (rating.ladder == ladder && rating.rank && !rating.provisional)
										{
											data.myRank = *rating.rank;
											data.myRating = rating.rating;
										}
								invalidate();
							});
			}
		}
	}
	// Live counts: whether a search is likely to find a person (the server caches them 30 s).
	if (!fetchingStats)
	{
		fetchingStats = true;
		calls->rest(HttpFetch::Method::Get, Online::Api::stats(), Json(), [this](const Online::PlatformClient::Response &r) {
			fetchingStats = false;
			if (!r.ok)
				return;
			data.playersOnline = r.result.value("playersOnline", -1);
			data.searching.clear();
			for (const auto &queue : r.result.value("queues", Json::array()))
				if (queue.is_object() && queue.contains("id") && queue["id"].is_string())
					data.searching[queue["id"].get<std::string>()] = queue.value("searching", 0);
			invalidate();
		});
	}
	if (!fetchingRooms)
	{
		fetchingRooms = true;
		const std::string sim = Online::SimVersion::local().key();
		calls->rest(HttpFetch::Method::Get, Online::Api::rooms(sim), Json(), [this](const Online::PlatformClient::Response &r) {
			fetchingRooms = false;
			if (!r.ok)
				return;
			data.rooms = Json::array();
			// Rooms already playing are hidden: no live spectating in v1.
			for (const auto &room : r.result.value("items", Json::array()))
				if (room.value("status", "open") == "open")
					data.rooms.push_back(room);
			invalidate();
		});
	}
}

void OnlineHubScreen::onTimer(Uint32 tick)
{
	now = tick;
	if (!data.toast.empty() && toastAt && tick - toastAt > TOAST_MS)
	{
		data.toast.clear();
		invalidate();
	}
	if (pictures && pictures->update())
		invalidate();
	if (previewing)
		return;
	watchSearch();
	syncFromClient();
	refresh(false);
	if (!mapLaunchOrigin.empty() && (!Online::pendingMapPlay() || Online::pendingMapPlay()->mode == Online::MapPlayRequest::Mode::Local))
	{
		mapLaunchOrigin.clear();
		trustPrompt.reset();
		pendingInvite.reset();
	}
	if (const auto &play = Online::pendingMapPlay(); play && play->mode == Online::MapPlayRequest::Mode::Multiplayer)
	{
		if (mapLaunchOrigin != play->origin)
		{
			trustPrompt.reset();
			pendingInvite.reset();
			mapLaunchOrigin = play->origin;
			acceptInvite(play->origin, "");
		}
		if (!trustPrompt && canPlay() && client().origin() == play->origin)
		{
			auto request = Online::takePendingMapPlay();
			mapLaunchOrigin.clear();
			pendingInvite.reset();
			const auto setup = Online::defaultRoomSetup(2, 0);
			enterRoom(Online::PlatformRoom::create(client(), Online::services().maps, Online::services().storage,
				formatted("[hub room name %0]", data.displayName), false, setup, false, request->map));
			return;
		}
	}
	// Invite links that arrived while the game runs (or at launch) land here.
	if (!trustPrompt)
		if (auto invite = Online::takePendingJoin())
			acceptInvite(invite->origin, invite->code);
	if (pendingInvite && client().connection() == Online::PlatformClient::Connection::Online &&
		client().origin() == pendingInvite->origin && client().auth() == Online::PlatformClient::Auth::SignedIn)
	{
		const std::string code = pendingInvite->code;
		pendingInvite.reset();
		if (!code.empty())
			joinByCode(code);
	}
}

bool OnlineHubScreen::canPlay() const
{
	return data.link == Model::Link::Online && !data.accountId.empty();
}

std::vector<std::string> OnlineHubScreen::searchAlso(int chosen) const
{
	std::vector<std::string> ids;
	if (!data.multiQueue || !data.queues.is_array())
		return ids;
	for (std::size_t i = 0; i < data.queues.size(); ++i)
		if (int(i) != chosen && alsoQueues.count(data.queues[i].value("id", "")) && canQueue(data.queues[i]))
			ids.push_back(data.queues[i].value("id", ""));
	return ids;
}

Element OnlineHubScreen::alsoToggles(int chosen, const Presentation &p)
{
	// Servers that take one search in several queues: the first match found wins.
	if (!data.multiQueue || !data.queues.is_array() || data.queues.size() < 2)
		return nullptr;
	std::vector<Element> toggles;
	for (std::size_t i = 0; i < data.queues.size(); ++i)
	{
		const Json &queue = data.queues[i];
		if (int(i) == chosen || !canQueue(queue))
			continue;
		const std::string id = queue.value("id", "");
		toggles.push_back(toggle("queue/also/" + id, formatted("[hub also search %0]", queueDisplayName(id, queue.value("name", ""))),
								 alsoQueues.count(id) > 0,
								 [this, id](bool on) {
									 if (on)
										 alsoQueues.insert(id);
									 else
										 alsoQueues.erase(id);
									 invalidate();
								 },
								 !searchModel().active()));
	}
	return toggles.empty() ? nullptr : column(std::move(toggles), {p.pt(2)});
}

bool OnlineHubScreen::canQueue(const Json &queue) const
{
	return canPlay() && !(queue.value("rated", false) && data.accountKind == "guest");
}

int OnlineHubScreen::defaultQueue() const
{
	if (!data.queues.is_array())
		return 0;
	for (std::size_t i = 0; i < data.queues.size(); ++i)
		if (!(data.queues[i].value("rated", false) && data.accountKind == "guest"))
			return int(i);
	return 0;
}

int OnlineHubScreen::queueIndex(const std::string &queueId) const
{
	if (data.queues.is_array())
		for (std::size_t i = 0; i < data.queues.size(); ++i)
			if (data.queues[i].value("id", "") == queueId)
				return int(i);
	return -1;
}

void OnlineHubScreen::enterRoom(std::shared_ptr<RoomBackend> room)
{
	screens.push(std::make_unique<RoomScreen>(screens, std::move(room)), [this](GAGGUI::Screen &, int result) {
		if (result == QUIT_APPLICATION)
			endExecute(QUIT_APPLICATION);
		refresh(true);
	});
}

void OnlineHubScreen::createRoom()
{
	createRoom(listRoom);
}

void OnlineHubScreen::createRoom(bool listed)
{
	if (!canPlay())
		return;
	// A fair 128×128 two-colony map: most rooms are two friends. It grows to four
	// colonies when more people join, until the host chooses a map.
	const auto setup = Online::defaultRoomSetup(2, std::random_device{}());
	const std::string name = formatted("[hub room name %0]", data.displayName);
	enterRoom(Online::PlatformRoom::create(client(), Online::services().maps, Online::services().storage, name, listed, setup, true));
}

void OnlineHubScreen::joinByCode(const std::string &codeOrLink)
{
	const auto invite = Online::parseInvite(codeOrLink, data.origin.empty() ? Online::services().config.selectedOrigin() : data.origin);
	if (!invite)
	{
		showToast(tr("[hub invalid code]"));
		return;
	}
	if (invite->origin != client().origin())
	{
		acceptInvite(invite->origin, invite->code);
		return;
	}
	if (!canPlay())
	{
		pendingInvite = Invite{invite->origin, invite->code};
		return;
	}
	joinDraft.clear();
	joinField = false;
	enterRoom(Online::PlatformRoom::join(client(), Online::services().maps, Online::services().storage, invite->code));
}

void OnlineHubScreen::acceptInvite(const std::string &origin, const std::string &code)
{
	auto &config = Online::services().config;
	if (origin == client().origin() || config.isTrusted(origin))
	{
		if (origin != client().origin())
		{
			client().start(origin);
			pendingInvite = Invite{origin, code};
			return;
		}
		if (!code.empty())
			joinByCode(code);
		return;
	}
	trustPrompt = Invite{origin, code};
	trustPrompt->remember = Online::REMEMBER_TRUST_BY_DEFAULT;
	invalidate();
}

void OnlineHubScreen::answerTrust(bool join)
{
	if (!trustPrompt)
		return;
	auto invite = *trustPrompt;
	trustPrompt.reset();
	if (!join && !mapLaunchOrigin.empty())
	{
		Online::takePendingMapPlay();
		mapLaunchOrigin.clear();
	}
	if (join)
	{
		Online::services().config.trust(invite.origin, invite.remember);
		client().start(invite.origin);
		pendingInvite = invite;
	}
	invalidate();
}

void OnlineHubScreen::findMatch(int queueIndex)
{
	if (!canPlay() || !data.queues.is_array() || queueIndex < 0 || queueIndex >= int(data.queues.size()))
		return;
	const Json &queue = data.queues[std::size_t(queueIndex)];
	if (!canQueue(queue))
	{
		openSignIn();
		return;
	}
	if (const auto &start = quickMatchStarter())
	{
		start(screens, queue, true);
		return;
	}
	const auto info = Online::QueueInfo::fromJson(queue);
	if (!info)
	{
		showToast(tr("[hub quick match unavailable]"));
		return;
	}
	// The chosen queue first, then any others the player also wants to search.
	std::vector<Online::QueueInfo> queues{*info};
	for (const auto &id : searchAlso(queueIndex))
		if (const int other = this->queueIndex(id); other >= 0)
			if (auto also = Online::QueueInfo::fromJson(data.queues[std::size_t(other)]))
				queues.push_back(*also);
	auto &search = searchModel();
	search.search(queues, search.allowAiOpponent());
	// The hub shows the search as a strip (SearchStrip); Details opens its screen.
	invalidate();
}

void OnlineHubScreen::openSearch()
{
	screens.push(std::make_unique<QuickMatchScreen>(screens), [this](GAGGUI::Screen &, int result) { quickMatchClosed(result); });
}

void OnlineHubScreen::watchSearch()
{
	auto &search = searchModel();
	if (searchTicker.due(search))
		invalidate();
	// What changed without the player doing anything (an opponent declined, the
	// search ended or failed) becomes a toast here, on every way back to the hub.
	if (const std::string notice = QuickMatchScreen::noticeText(search); !notice.empty())
	{
		search.dismissNotice();
		showToast(notice);
	}
	if (search.phase() == Online::QuickMatch::Phase::Failed)
	{
		const std::string reason = QuickMatchScreen::failureText(search);
		search.cancel();
		showToast(reason);
	}
}

void OnlineHubScreen::quickMatchClosed(int result)
{
	if (auto match = std::move(deferredMatch))
	{
		deferredMatch.reset();
		startMatch(*match);
		return;
	}
	// A search that ended or failed there reaches the hub as a toast (watchSearch).
	if (result == QuickMatchScreen::ACCOUNT)
	{
		if (data.accountKind == "registered")
			openAccountMenu(true);
		else
			openSignIn();
	}
	refresh(true);
}

void OnlineHubScreen::openSignIn()
{
	accountMenu = false;
	data.signIn = Model::SignIn::Choosing;
	invalidate();
}

void OnlineHubScreen::signInWith(const std::string &provider)
{
	if (previewing)
		return;
	// The page is only known once the server replies; open its tab during this click.
	GAGCore::ApplicationHost::prepareUrlWindow();
	// A guest links the identity, so their matches and maps come along.
	client().beginBrowserSignIn("", provider);
	data.signIn = Model::SignIn::Waiting;
	invalidate();
}

void OnlineHubScreen::copyCode()
{
	if (data.confirmationCode.empty())
		return;
	showToast(tr(GAGCore::ApplicationHost::copyText(data.confirmationCode) ? "[hub code copied]" : "[room copy failed]"));
}

void OnlineHubScreen::cancelSignIn()
{
	if (!previewing && data.signIn == Model::SignIn::Waiting)
		client().cancelBrowserSignIn();
	data.signIn = Model::SignIn::Closed;
	invalidate();
}

void OnlineHubScreen::signOut()
{
	accountMenu = false;
	if (!previewing)
		client().signOut();
	invalidate();
}

void OnlineHubScreen::openAccountMenu(bool open)
{
	accountMenu = open;
	invalidate();
}

void OnlineHubScreen::openSettings()
{
	accountMenu = false;
	auto settings = std::make_unique<SettingsScreen>();
	settings->selectCategory(SettingsScreen::Category::Online);
	screens.push(std::move(settings), [this](GAGGUI::Screen &, int) {
		syncFromClient();
		data.instanceName.clear();
		refresh(true);
	});
}

void OnlineHubScreen::onEscape()
{
	if (!mapLaunchOrigin.empty())
	{
		Online::takePendingMapPlay();
		mapLaunchOrigin.clear();
		pendingInvite.reset();
	}
	if (trustPrompt)
		answerTrust(false);
	else if (data.signIn != Model::SignIn::Closed)
		cancelSignIn();
	else if (accountMenu)
		openAccountMenu(false);
	else if (joinField)
	{
		joinField = false;
		invalidate();
	}
	else
	{
		// Leaving Online ends the search: nothing outside it shows or cancels it.
		if (searchModel().active())
			searchModel().cancel();
		endExecute(0);
	}
}

// --------------------------------------------------------------------- build

Element OnlineHubScreen::accountChip(const Presentation &p)
{
	const auto palette = theme().palette;
	std::string name = data.displayName.empty() ? tr("[hub signing in]") : data.displayName;
	const bool guest = data.accountKind != "registered";
	std::vector<Element> words{label(name)};
	if (!p.compact())
		words.push_back(caption(guest ? tr("[hub guest kept on device]") : formatted("[hub signed in on %0]", hostOf(data.origin))));
	auto initial = sized({p.pt(28), p.pt(28)}, canvas("", {p.pt(28), p.pt(28)}, [palette, letter = name.substr(0, 1)](Canvas &c, Rect r, const Frame &) {
		c.fillRounded(r, r.w / 2, palette.neutral);
		const int w = c.measurer().width(FontRole::Body, letter);
		c.text({r.x + (r.w - w) / 2, r.y + (r.h - c.measurer().lineHeight(FontRole::Body)) / 2}, FontRole::Body, letter, palette.paper);
	}));
	ButtonOptions chip;
	chip.flat = true;
	chip.alignLeft = true;
	// Narrow phones drop the initial so the menu button stays on screen.
	std::vector<Element> cells{p.compact() ? nullptr : initial, column(std::move(words), {0}),
							   button("account/menu", "", [this] { openAccountMenu(!accountMenu); },
									  {.flat = true, .tooltip = tr("[hub account menu]"), .accessibleLabel = tr("[hub account menu]"), .icon = uiIcon(UIIcon::More)})};
	if (guest && !p.compact() && !data.displayName.empty())
		// Secondary: Find match is the one primary action on the hub.
		cells.push_back(button("account/signin", tr("[hub sign in]"), [this] { openSignIn(); }, {.icon = uiIcon(UIIcon::SignIn), .iconSize = 16}));
	return row(std::move(cells), {p.pt(8), CrossAlign::Center});
}

Element OnlineHubScreen::banner(const Presentation &p)
{
	if (data.link == Model::Link::Offline)
	{
		auto text = column({label(formatted("[hub cant reach %0]", hostOf(data.origin)), {FontRole::Body, false, TextAlign::Left, theme().palette.danger}),
							paragraph(GAGCore::FormattableString(tr("[hub offline detail %0]")).arg(data.retryInSeconds), {FontRole::Support, true})},
						   {p.pt(2)});
		return card(column({row({icon(uiIcon(UIIcon::WifiOff), {22, theme().palette.danger}), expanded(text)}, {p.pt(10), CrossAlign::Start}),
							row({button("banner/retry", tr("[hub retry now]"), [this] { client().retryNow(); }),
								 button("banner/server", tr("[hub change server]"), [this] { openSettings(); })},
								{p.pt(8)})},
						   {p.pt(8)}),
					{.color = theme().palette.field, .padding = p.pt(10), .shadow = false, .border = theme().palette.danger});
	}
	if (data.link == Model::Link::UpdateRequired)
	{
		// Only offer the download when this copy may be the one that is out of date.
		const bool serverBehind = data.outdated == Model::Outdated::Server;
		const bool clientBehind = data.outdated == Model::Outdated::Client;
		const char *headingKey = serverBehind ? "[hub server behind]" : clientBehind ? "[hub update required]" : "[hub version mismatch]";
		const char *detailKey = serverBehind ? "[hub server behind detail %0]" : clientBehind ? "[hub update required detail %0]" : "[hub version mismatch detail %0]";
		std::vector<Element> actions;
		if (!serverBehind)
			actions.push_back(button("banner/update", tr("[hub get update]"), [] { GAGCore::ApplicationHost::openUrl("https://globulation2.org/download"); }, {.primary = true}));
		actions.push_back(button("banner/server", tr("[hub change server]"), [this] { openSettings(); }));
		return card(column({row({icon(uiIcon(UIIcon::Warning), {22, theme().palette.danger}), expanded(column({heading(tr(headingKey)), paragraph(formatted(detailKey, hostOf(data.origin)), {FontRole::Support, true})}, {p.pt(2)}))}, {p.pt(10), CrossAlign::Start}),
							row(std::move(actions), {p.pt(8)})},
						   {p.pt(8)}),
					{.color = theme().palette.field, .padding = p.pt(10), .shadow = false, .border = theme().palette.danger});
	}
	return nullptr;
}

Element OnlineHubScreen::signInPanel(const Presentation &p)
{
	std::vector<Element> parts{heading(tr("[hub sign in]"))};
	if (data.signIn == Model::SignIn::Choosing)
	{
		parts.push_back(paragraph(tr("[hub sign in intro]"), {FontRole::Support, true}));
		std::vector<std::pair<std::string, std::string>> providers;
		for (const auto &provider : data.providers)
			if (provider.value("kind", "") != "local")
				providers.push_back({provider.value("id", ""), provider.value("displayName", providerName(provider.value("id", "")))});
		// Without external providers the one way in is the instance's own sign-in
		// page: say that, rather than "Continue with Sign in in the browser".
		if (providers.empty())
			parts.push_back(button("signin/page", tr("[hub sign in browser]"), [this] { signInWith(""); }, {.primary = true, .alignLeft = true, .shortcut = SDLK_RETURN, .icon = uiIcon(UIIcon::SignIn)}));
		for (const auto &[id, name] : providers)
			parts.push_back(button("signin/" + id, formatted("[hub continue with %0]", name), [this, id] { signInWith(id); }, {.alignLeft = true, .icon = uiIcon(UIIcon::SignIn)}));
		parts.push_back(button("signin/cancel", tr("[Cancel]"), [this] { cancelSignIn(); }, {.shortcut = SDLK_ESCAPE}));
	}
	else
	{
		// The browser does the rest and the game signs in by itself. The code is
		// only for comparing, when the page opens on another network.
		parts.push_back(paragraph(tr(data.browserOpened ? "[hub finish in browser, game continues]" : "[hub browser did not open]")));
		std::string code = data.confirmationCode;
		if (code.size() == 6)
			code = code.substr(0, 3) + " · " + code.substr(3);
		if (!code.empty())
			parts.push_back(row({expanded(paragraph(formatted("[hub compare code %0]", code), {FontRole::Support, true})),
								 button("signin/copy", tr("[room copy]"), [this] { copyCode(); }, {.icon = uiIcon(UIIcon::Copy), .iconSize = 16})},
								{p.pt(6), CrossAlign::Center}));
		std::vector<Element> actions{button("signin/reopen", tr(data.browserOpened ? "[hub open page again]" : "[hub sign in browser]"), [this] { if (!previewing) client().openSignInPage(); },
											{.primary = !data.browserOpened, .icon = uiIcon(UIIcon::ExternalLink), .iconSize = 16}),
									 button("signin/cancel", tr("[Cancel]"), [this] { cancelSignIn(); }, {.shortcut = SDLK_ESCAPE})};
		// Phones stack the two buttons: side by side they do not fit.
		parts.push_back(p.compact() ? column(std::move(actions), {p.pt(8)}) : row(std::move(actions), {p.pt(8)}));
	}
	return card(column(std::move(parts), {p.pt(8)}), {.padding = p.pt(16)});
}

Element OnlineHubScreen::trustPanel(const Presentation &p)
{
	const Invite &invite = *trustPrompt;
	std::vector<Element> parts{heading(tr("[trust title]")), title(hostOf(invite.origin)),
							   caption(formatted("[trust invited to room %0]", invite.code)),
							   paragraph(tr("[trust not official]")),
							   paragraph(formatted("[trust separate identity %0]", hostOf(data.origin))),
							   paragraph(tr("[trust what it sees]"), {FontRole::Support, true}),
							   toggle("trust/remember", tr("[trust remember]"), invite.remember, [this](bool v) { if (trustPrompt) trustPrompt->remember = v; invalidate(); }),
							   row({button("trust/cancel", tr("[Cancel]"), [this] { answerTrust(false); }, {.shortcut = SDLK_ESCAPE}),
									button("trust/join", tr("[trust join as guest]"), [this] { answerTrust(true); }, {.primary = true, .shortcut = SDLK_RETURN})},
								   {p.pt(8)})};
	return card(column(std::move(parts), {p.pt(8)}), {.padding = p.pt(16)});
}

Element OnlineHubScreen::accountPanel(const Presentation &p)
{
	const bool guest = data.accountKind != "registered";
	std::vector<Element> items;
	items.push_back(heading(data.displayName));
	if (guest)
		items.push_back(button("account/signin-menu", tr("[hub sign in]"), [this] { openSignIn(); }, {.flat = true, .alignLeft = true, .icon = uiIcon(UIIcon::SignIn)}));
	// Linked accounts, the server and the display name all live in Settings › Online.
	items.push_back(button("account/settings", tr("[hub online settings]"), [this] { openSettings(); }, {.flat = true, .alignLeft = true, .icon = uiIcon(UIIcon::Settings)}));
	if (!guest)
		items.push_back(button("account/signout", tr("[hub sign out]"), [this] { signOut(); }, {.flat = true, .alignLeft = true, .icon = uiIcon(UIIcon::Leave)}));
	items.push_back(button("account/close", tr("[Close]"), [this] { openAccountMenu(false); }, {.shortcut = SDLK_ESCAPE}));
	return card(column(std::move(items), {p.pt(4)}), {.padding = p.pt(12)});
}

Element OnlineHubScreen::queuePicker(const Presentation &p, bool stacked)
{
	std::vector<std::string> names;
	for (const auto &queue : data.queues)
		names.push_back(queue.value("name", queue.value("mode", "")));
	std::vector<Element> parts;
	if (!names.empty())
	{
		if (!p.landscape())
			parts.push_back(caption(tr("[hub quick match]")));
		// Until the player picks one, the default follows the account (guests: Casual).
		const int chosen = std::clamp(selectedQueue < 0 ? defaultQueue() : selectedQueue, 0, int(names.size()) - 1);
		if (stacked)
			// One full-width choice per line: three side by side do not hold their
			// names on a narrow phone with large text.
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				const int index = int(i);
				parts.push_back(button("queue/choice/" + std::to_string(i), names[i], [this, index] { selectedQueue = index; invalidate(); },
									   {.selected = index == chosen}));
			}
		else
			parts.push_back(segments("queue/choice", names, chosen, [this](int v) { selectedQueue = v; invalidate(); }));
		if (canPlay() && !canQueue(data.queues[std::size_t(chosen)]))
		{
			parts.push_back(caption(tr("[qm sign in to play ranked]")));
			parts.push_back(button("queue/signin", tr("[hub sign in]"), [this] { openSignIn(); }, {.primary = true, .icon = uiIcon(UIIcon::SignIn)}));
		}
		else
			parts.push_back(button("queue/find", tr("[hub find match]"), [this, chosen] { findMatch(chosen); }, {.primary = true, .enabled = canPlay() && !searchModel().active(), .icon = uiIcon(UIIcon::Bolt)}));
	}
	return parts.empty() ? nullptr : column(std::move(parts), {p.pt(6)});
}

Element OnlineHubScreen::thumbBlock(const Presentation &p, bool withQueues)
{
	std::vector<Element> parts;
	if (joinField)
	{
		TextFieldOptions field;
		field.placeholder = tr("[hub code placeholder]");
		field.autoFocus = true;
		field.submit = [this](const std::string &v) { joinByCode(v); };
		parts.push_back(row({expanded(textField("join/code", joinDraft, [this](const std::string &v) { joinDraft = v; }, field)),
							 button("join/go", tr("[hub join]"), [this] { joinByCode(joinDraft); }, {.primary = true, .enabled = canPlay()})},
							{p.pt(6), CrossAlign::Center}));
	}
	if (withQueues)
		if (auto picker = queuePicker(p))
			parts.push_back(picker);
	ButtonOptions back;
	back.icon = uiIcon(UIIcon::Back);
	back.accessibleLabel = tr("[Back]");
	back.tooltip = back.accessibleLabel;
	back.shortcut = SDLK_ESCAPE;
	std::vector<Element> line{width(p.pt(52), button("back", "", [this] { onEscape(); }, back)),
							  expanded(button("join/open", tr(p.landscape() ? "[hub code]" : "[hub join code]"), [this] { joinField = !joinField; invalidate(); }, {.selected = joinField})),
							  expanded(button("room/create", tr("[hub room]"), [this] { createRoom(); }, {.enabled = canPlay(), .icon = p.landscape() ? IconRef() : uiIcon(UIIcon::Plus)}))};
	if (ThumbSide::left())
		std::reverse(line.begin(), line.end());
	parts.push_back(row(std::move(line), {p.pt(8), CrossAlign::Center}));
	return column(std::move(parts), {p.pt(6)});
}

Element OnlineHubScreen::outcomeBadge(const std::string &letter, const Presentation &p)
{
	const auto palette = theme().palette;
	const GAGCore::Color tone = letter == "W" ? palette.success : letter == "L" ? palette.danger : palette.neutral;
	return sized({p.pt(22), p.pt(22)}, canvas("", {p.pt(22), p.pt(22)}, [tone, letter, palette](Canvas &c, Rect r, const Frame &) {
		c.fillRounded(r, 3, tone);
		const int w = c.measurer().width(FontRole::Support, letter);
		c.text({r.x + (r.w - w) / 2, r.y + (r.h - c.measurer().lineHeight(FontRole::Support)) / 2}, FontRole::Support, letter, palette.paper);
	}));
}

Element OnlineHubScreen::liveLine(const Json &queue, const Presentation &p)
{
	// Shown only when the server reports both numbers.
	const auto found = data.searching.find(queue.value("id", ""));
	if (data.playersOnline < 0 || found == data.searching.end())
		return nullptr;
	const auto palette = theme().palette;
	return row({icon(uiIcon(UIIcon::Users), {16, palette.muted}),
				expanded(caption(GAGCore::FormattableString(tr("[hub online now %0 %1]")).arg(data.playersOnline).arg(found->second)))},
			   {p.pt(6), CrossAlign::Center});
}

Element OnlineHubScreen::mapPool(const Json &queue, const Presentation &p, bool phone)
{
	// The map pool, drawn: what a match in this queue will look like.
	if (!pictures || !queue.contains("maps") || !queue["maps"].is_array() || queue["maps"].empty())
		return nullptr;
	std::vector<std::string> pool;
	for (const auto &id : queue["maps"])
		if (id.is_string())
			pool.push_back(id.get<std::string>());
	std::vector<Element> thumbs;
	std::string titles;
	const std::size_t shown = std::min<std::size_t>(phone ? 3 : 4, pool.size());
	for (std::size_t i = 0; i < shown; ++i)
	{
		thumbs.push_back(previewPicture(pictures->generator(pool[i]), p.pt(phone ? 64 : 72)));
		titles += (i ? ", " : "") + generatorTitle(pool[i]);
	}
	if (pool.size() > shown)
		titles += "…";
	return column({row(std::move(thumbs), {p.pt(6), CrossAlign::Center}), caption(formatted("[qm fair maps %0]", titles))}, {p.pt(4)});
}

Element OnlineHubScreen::quickMatchCard(const Presentation &p, bool phone)
{
	const auto palette = theme().palette;
	std::vector<Element> parts{row({icon(uiIcon(UIIcon::Bolt), {22, palette.accent}), expanded(heading(tr("[hub quick match]")))}, {p.pt(8), CrossAlign::Center})};
	if (!data.queues.is_array() || data.queues.empty())
	{
		parts.push_back(paragraph(data.link == Model::Link::Online ? tr("[hub no queues]") : tr("[hub queues unavailable]"), {FontRole::Support, true}));
		return card(column(std::move(parts), {p.pt(8)}), {.color = palette.field, .padding = p.pt(14), .shadow = false, .border = palette.accent});
	}
	std::vector<std::string> names;
	for (const auto &queue : data.queues)
		names.push_back(queueDisplayName(queue.value("id", ""), queue.value("name", "")));
	// Until the player picks one, the default follows the account (guests: Casual).
	const int chosen = std::clamp(selectedQueue < 0 ? defaultQueue() : selectedQueue, 0, int(names.size()) - 1);
	const Json &queue = data.queues[std::size_t(chosen)];
	if (names.size() > 1)
		parts.push_back(segments("queue/choice", names, chosen, [this](int v) { selectedQueue = v; invalidate(); }));
	std::string detail = queue.value("rated", false) ? tr("[hub ranked]") : tr("[hub unrated]");
	if (queue.contains("aiBackfillSeconds") && queue["aiBackfillSeconds"].is_number_integer())
		detail += " · " + formatted("[hub ai joins after %0]", clockText(queue["aiBackfillSeconds"].get<int>()));
	const bool open = canQueue(queue);
	if (canPlay() && !open)
		detail = tr("[qm sign in to play ranked]");
	parts.push_back(paragraph(detail, {FontRole::Support, true}));
	if (auto live = liveLine(queue, p))
		parts.push_back(live);
	if (auto pool = mapPool(queue, p, phone))
		parts.push_back(pool);
	if (open)
		if (auto also = alsoToggles(chosen, p))
			parts.push_back(also);
	if (canPlay() && !open)
		parts.push_back(button("queue/signin", tr("[hub sign in]"), [this] { openSignIn(); }, {.primary = true, .icon = uiIcon(UIIcon::SignIn)}));
	else
		parts.push_back(button("queue/find", tr("[hub find match]"), [this, chosen] { findMatch(chosen); },
							   {.primary = true, .enabled = canPlay() && !searchModel().active(), .icon = uiIcon(UIIcon::Bolt)}));
	return card(column(std::move(parts), {p.pt(8)}), {.color = palette.field, .padding = p.pt(14), .shadow = false, .border = palette.accent});
}

Element OnlineHubScreen::friendsCard(const Presentation &p)
{
	const auto palette = theme().palette;
	TextFieldOptions codeField;
	codeField.placeholder = tr("[hub code placeholder]");
	codeField.submit = [this](const std::string &v) { joinByCode(v); };
	return card(column({row({icon(uiIcon(UIIcon::Users), {20, palette.muted}), expanded(heading(tr("[hub play with friends]")))}, {p.pt(8), CrossAlign::Center}),
						row({button("room/create", tr("[hub create room]"), [this] { createRoom(); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Plus)}),
							 spacer(p.pt(8)), caption(tr("[hub join by code]")),
							 expanded(textField("join/code", joinDraft, [this](const std::string &v) { joinDraft = v; }, codeField)),
							 button("join/go", tr("[hub join]"), [this] { joinByCode(joinDraft); }, {.enabled = canPlay() && !joinDraft.empty()})},
							{p.pt(6), CrossAlign::Center}),
						// Invite-only unless the host chooses otherwise; the room can change it later.
						toggle("room/listed", tr("[hub show in open rooms]"), listRoom, [this](bool v) { listRoom = v; invalidate(); })},
					   {p.pt(8)}),
				{.color = palette.field, .padding = p.pt(12), .shadow = false, .border = palette.line});
}

Element OnlineHubScreen::lastMatchCard(const Presentation &p, bool phone)
{
	if (!data.recent.is_array() || data.recent.empty())
		return nullptr;
	const auto palette = theme().palette;
	const Json &match = data.recent[0];
	const auto line = recentLine(match, data.accountId);
	std::vector<Element> cells;
	// The map's picture when this device has the map; no empty square otherwise.
	if (auto *picture = pictures ? pictures->cached(match.value("mapHash", "")) : nullptr; picture && !phone)
		cells.push_back(previewPicture(picture, p.pt(56)));
	cells.push_back(outcomeBadge(line.outcome, p));
	cells.push_back(expanded(column({label(line.title), caption(line.detail)}, {0})));
	cells.push_back(caption(line.rating, false));
	if (line.verified)
		cells.push_back(icon(uiIcon(UIIcon::ShieldCheck), {14, palette.success}));
	// The match page has the full result (timeline, economy, replay).
	if (const std::string id = match.value("id", ""); !id.empty() && !data.origin.empty())
	{
		ButtonOptions page{.icon = uiIcon(UIIcon::ExternalLink), .iconSize = 16};
		page.accessibleLabel = page.tooltip = tr("[profile match page]");
		cells.push_back(phone ? width(p.pt(48), button("recent/0/page", "", [origin = data.origin, id] { openInstancePage(origin, "/matches/" + id); }, page))
							  : button("recent/0/page", tr("[profile match page]"), [origin = data.origin, id] { openInstancePage(origin, "/matches/" + id); }, page));
	}
	return column({heading(tr("[hub last match]")), row(std::move(cells), {p.pt(8), CrossAlign::Center})}, {p.pt(6)});
}

Element OnlineHubScreen::playSection(const Presentation &p, bool phone)
{
	std::vector<Element> parts;
	// Phones keep Find match, Join code and Room in the thumb block.
	if (!phone)
	{
		parts.push_back(quickMatchCard(p, false));
		parts.push_back(friendsCard(p));
	}
	// Phones: the chosen queue's maps above the thumb block's picker.
	if (phone && data.queues.is_array() && !data.queues.empty())
	{
		const int chosen = std::clamp(selectedQueue < 0 ? defaultQueue() : selectedQueue, 0, int(data.queues.size()) - 1);
		const Json &queue = data.queues[std::size_t(chosen)];
		auto live = liveLine(queue, p);
		auto pool = mapPool(queue, p, true);
		// Here rather than in the thumb block, whose height a small phone cannot spare.
		auto also = canQueue(queue) ? alsoToggles(chosen, p) : nullptr;
		if (live || pool || also)
			parts.push_back(column({heading(tr("[hub quick match]")), live, pool, also}, {p.pt(6)}));
	}
	if (auto last = lastMatchCard(p, phone))
		parts.push_back(last);
	if (phone)
		// Both share the width, so a narrow phone never pushes Maps past the edge.
		parts.push_back(row({expanded(button("profile", tr("[hub profile history]"), [this] { openProfile(); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Player), .iconSize = 16})),
							 expanded(button("maps/browse", tr("[hub maps]"), [this] { openMaps(false); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Map), .iconSize = 16}))},
							{p.pt(6)}));
	return column(std::move(parts), {p.pt(14)});
}

Element OnlineHubScreen::roomsSection(const Presentation &p, bool phone)
{
	std::vector<Element> rows;
	int shown = 0;
	for (std::size_t i = 0; i < data.rooms.size(); ++i)
	{
		const Json &room = data.rooms[i];
		const int total = room.value("seatsTotal", 0), taken = room.value("seatsTaken", 0);
		if (roomFilter == 1 && taken >= total)
			continue;
		++shown;
		const std::string code = room.value("code", "");
		std::string detail = room.value("hostDisplayName", "");
		if (room.contains("mapTitle") && room["mapTitle"].is_string())
			detail += " · " + room["mapTitle"].get<std::string>();
		detail += " · " + GAGCore::FormattableString(tr("[hub seats %0 %1]")).arg(taken).arg(total);
		auto words = column({label(room.value("name", "")), caption(detail)}, {0});
		ButtonOptions join;
		join.enabled = canPlay() && taken < total;
		// The catalog map's server preview when there is one; an icon otherwise.
		Element picture = icon(uiIcon(UIIcon::Users), {20, theme().palette.muted});
		if (const std::string url = room.value("mapPreviewUrl", ""); !url.empty() && previews && !previewing)
			picture = previewPicture(previews->get(&client(), url, [this] { invalidate(); }), p.pt(40));
		rows.push_back(row({picture, expanded(words),
							button("rooms/" + std::to_string(i) + "/join", tr("[hub join]"), [this, code] { joinByCode(code); }, join)},
						   {p.pt(8), CrossAlign::Center}));
		rows.push_back(divider());
	}
	// An empty list is the usual case on a small server: say so and offer the two
	// ways to play anyway.
	if (shown == 0)
		rows.push_back(emptyState(uiIcon(UIIcon::Users), tr("[hub no rooms]"),
								  {button("rooms/empty/create", tr("[hub create room]"), [this] { createRoom(true); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Plus)}),
								   button("rooms/empty/quick", tr("[hub quick match]"), [this] { showSection(Section::Play); }, {.icon = uiIcon(UIIcon::Bolt)})},
								  p));
	std::vector<Element> head{expanded(heading(tr("[hub open rooms]")))};
	if (!phone)
		// A width that holds both labels on one line ("Free seat" used to wrap, H-1).
		head.push_back(width(p.textPt(220), segments("rooms/filter", {tr("[hub all rooms]"), tr("[hub free seat]")}, roomFilter, [this](int v) { roomFilter = v; invalidate(); })));
	ButtonOptions reload;
	reload.icon = uiIcon(UIIcon::Refresh);
	reload.accessibleLabel = tr("[hub refresh]");
	reload.tooltip = reload.accessibleLabel;
	head.push_back(width(p.pt(p.touch ? 48 : 34), button("rooms/refresh", "", [this] { refresh(true); }, reload)));
	std::vector<Element> parts{row(std::move(head), {p.pt(6), CrossAlign::Center}), column(std::move(rows), {p.pt(4)})};
	if (!phone)
		parts.push_back(friendsCard(p));
	return column(std::move(parts), {p.pt(10)});
}

Element OnlineHubScreen::leaderboardSection(const Presentation &p, bool phone)
{
	const auto palette = theme().palette;
	std::vector<Element> parts;
	const std::string title = data.leaderboardName.empty() ? tr("[hub leaderboard]") : formatted("[hub leaderboard %0]", data.leaderboardName);
	parts.push_back(heading(title));
	// Where the player stands comes first: the reason most people open a leaderboard.
	std::string standing;
	if (data.accountKind == "guest")
		standing = tr("[hub guests not ranked]");
	else if (data.myRank > 0)
		standing = GAGCore::FormattableString(tr("[hub your rank %0 %1]")).arg(data.myRank).arg(int(std::lround(data.myRating)));
	else if (!data.accountId.empty())
		standing = tr("[hub not placed]");
	if (!standing.empty())
		parts.push_back(card(row({icon(uiIcon(UIIcon::Trophy), {20, palette.accent}), expanded(paragraph(standing))}, {p.pt(8), CrossAlign::Center}),
							 {.color = palette.field, .padding = p.pt(10), .shadow = false, .border = palette.line}));
	std::vector<Element> rows;
	for (std::size_t i = 0; i < data.leaderboard.size(); ++i)
	{
		const Json &entry = data.leaderboard[i];
		const Json &entity = entry.value("entity", Json::object());
		const Json &account = entity.value("account", Json::object());
		const std::string name = entity.value("kind", "") == "ai" ? aiTitle(entity.value("ai", "")) : account.value("displayName", "?");
		const bool me = !data.accountId.empty() && account.value("id", "") == data.accountId;
		const int rating = int(std::lround(entry.value("rating", 0.0)));
		Element line = row({width(p.pt(34), caption(std::to_string(entry.value("rank", int(i) + 1)), false)), expanded(label(name)),
							caption(std::to_string(rating), false)},
						   {p.pt(8), CrossAlign::Center});
		rows.push_back(me ? card(line, {.color = palette.selected, .padding = p.pt(4), .shadow = false}) : line);
	}
	if (rows.empty())
		rows.push_back(emptyState(uiIcon(UIIcon::Trophy), tr("[hub leaderboard empty]"), {}, p));
	parts.push_back(column(std::move(rows), {p.pt(4)}));
	// Below the list, on its own line: beside the title it overflowed with large text.
	if (!data.origin.empty())
		parts.push_back(align(Alignment::Left, button("leaderboard/full", tr("[hub full leaderboard]"), [this] { GAGCore::ApplicationHost::openUrl(data.origin + "/leaderboard"); },
													  {.icon = uiIcon(UIIcon::ExternalLink), .iconSize = 16})));
	return column(std::move(parts), {p.pt(10)});
}

Element OnlineHubScreen::sectionNav(const Presentation &p, bool phone)
{
	const std::vector<std::string> names{tr("[hub play]"), tr("[hub rooms]"), tr("[hub leaderboard]")};
	if (phone)
		// "Leaderboard" does not fit a third of a phone; the tab says Ranks.
		return segments("hub/section", {names[0], names[1], tr("[hub ranks]")}, int(section), [this](int v) { showSection(Section(v)); });
	auto item = [&](const std::string &key, const std::string &text, UIIcon glyph, std::function<void()> action, bool selected, bool enabled = true) {
		return button(key, text, std::move(action), {.selected = selected, .enabled = enabled, .flat = !selected, .alignLeft = true, .icon = uiIcon(glyph)});
	};
	std::string rooms = names[1];
	if (const int open = data.rooms.is_array() ? int(data.rooms.size()) : 0; open > 0)
		rooms += " (" + std::to_string(open) + ")";
	// Scrolls rather than squeezing its items below the touch target when large text
	// makes it taller than a portrait tablet's window.
	return scroll("hub/nav", column({item("hub/section/play", names[0], UIIcon::Start, [this] { showSection(Section::Play); }, section == Section::Play),
				   item("hub/section/rooms", rooms, UIIcon::Users, [this] { showSection(Section::Rooms); }, section == Section::Rooms),
				   item("hub/section/leaderboard", names[2], UIIcon::Trophy, [this] { showSection(Section::Leaderboard); }, section == Section::Leaderboard),
				   divider(),
				   // Their own screens; Back returns here.
				   item("maps/browse", tr("[hub maps]"), UIIcon::Map, [this] { openMaps(false); }, false, canPlay()),
				   item("profile", tr("[hub profile history]"), UIIcon::Player, [this] { openProfile(); }, false, canPlay()),
				   divider(),
				   item("settings", tr("[hub online settings]"), UIIcon::Settings, [this] { openSettings(); }, false)},
				  {p.pt(4)}));
}

Element OnlineHubScreen::sectionBody(const Presentation &p, bool phone)
{
	switch (section)
	{
	case Section::Rooms:
		return roomsSection(p, phone);
	case Section::Leaderboard:
		return leaderboardSection(p, phone);
	default:
		return playSection(p, phone);
	}
}

void OnlineHubScreen::showSection(Section next)
{
	section = next;
	invalidate();
	// The full leaderboard is only fetched while it is shown.
	if (next == Section::Leaderboard)
		refresh(true);
}

Element OnlineHubScreen::build(const Presentation &p)
{
	const bool phone = p.compact() || (p.touch && p.shortLandscape());
	const std::string instance = data.instanceName.empty() ? hostOf(data.origin) : data.instanceName;
	std::string status;
	switch (data.link)
	{
	case Model::Link::Online:
		status = formatted("[hub online at %0]", hostOf(data.origin));
		break;
	case Model::Link::Connecting:
		status = formatted("[hub connecting to %0]", hostOf(data.origin));
		break;
	case Model::Link::Offline:
		status = formatted("[hub offline at %0]", hostOf(data.origin));
		break;
	case Model::Link::UpdateRequired:
		status = formatted(data.outdated == Model::Outdated::Server	  ? "[hub server behind at %0]"
						   : data.outdated == Model::Outdated::Client ? "[hub update required at %0]"
																	  : "[hub version mismatch at %0]",
						   hostOf(data.origin));
		break;
	}
	auto headline = row({expanded(column({heading(tr("[hub online]")), caption(status)}, {0})), accountChip(p)}, {p.pt(8), CrossAlign::Center});
	if (p.compact())
		// A narrow phone (or large text) gives the status line the full width under
		// the title and account chip, rather than wrapping it word by word beside the
		// chip and pushing Back off the bottom.
	{
		std::vector<Element> lines{row({expanded(heading(tr("[hub online]"))), accountChip(p)}, {p.pt(8), CrossAlign::Center})};
		// The offline and update banners already name the server and the problem.
		if (data.link != Model::Link::Offline && data.link != Model::Link::UpdateRequired)
			lines.push_back(caption(status));
		headline = column(std::move(lines), {0});
	}
	// Modal panels take the body: sign-in, the trust prompt and the account menu.
	Element overlay = trustPrompt ? trustPanel(p) : data.signIn != Model::SignIn::Closed ? signInPanel(p) : accountMenu ? accountPanel(p) : nullptr;
	std::vector<Element> toast;
	if (!data.toast.empty())
		toast.push_back(card(paragraph(data.toast), {.color = theme().palette.selected, .padding = p.pt(8), .shadow = false}));
	if (phone)
	{
		// A portrait phone too short for its text (a small phone at large text sizes)
		// scrolls the quick-match picker with the lists, so the fixed thumb row
		// (Back, Join code, Room) keeps its full height.
		const bool crowded = !p.landscape() && p.points(p.safe.h) < 500 * p.textGrowth;
		std::vector<Element> list;
		// First in the scrolled list rather than fixed above it: a small phone with
		// large text has no height to spare for its buttons.
		if (auto strip = SearchStrip::build(searchModel(), p, [this] { openSearch(); }))
			list.push_back(strip);
		if (auto b = banner(p))
			list.push_back(b);
		// A crowded page scrolls the section tabs too, so the fixed thumb row keeps its height.
		if (crowded && !overlay)
			list.push_back(sectionNav(p, true));
		if (crowded && section == Section::Play)
			if (auto picker = queuePicker(p, true))
				list.push_back(picker);
		list.push_back(sectionBody(p, true));
		Element body = overlay ? scroll("hub/overlay", overlay) : scroll("hub/scroll", column(std::move(list), {p.pt(12)}));
		std::vector<Element> page{headline};
		for (auto &t : toast)
			page.push_back(t);
		if (!overlay && !crowded)
			page.push_back(sectionNav(p, true));
		// The queue picker and Find match belong to Play; the other sections keep
		// Back, Join code and Room at thumb reach.
		const bool withQueues = section == Section::Play && !crowded;
		if (!overlay && p.landscape())
			// Landscape: the thumb block becomes the right-hand (thumb-side) rail.
			page.push_back(expanded(ThumbSide::left() ? row({width(p.pt(250), scroll("hub/thumb", thumbBlock(p, section == Section::Play))), expanded(body)}, {p.pt(10), CrossAlign::End})
													  : row({expanded(body), width(p.pt(250), scroll("hub/thumb", thumbBlock(p, section == Section::Play)))}, {p.pt(10), CrossAlign::End})));
		else
		{
			page.push_back(expanded(body));
			if (!overlay)
				page.push_back(thumbBlock(p, withQueues));
		}
		return padding({p.pt(4), p.pt(4), p.pt(4), p.pt(4)}, card(column(std::move(page), {p.pt(8)}), {.padding = p.pt(10)}));
	}
	std::vector<Element> content;
	if (auto b = banner(p))
		content.push_back(b);
	content.push_back(sectionBody(p, false));
	Element body;
	if (overlay)
		// Scrolls rather than squeezing its buttons or overlapping the footer when
		// large text makes it taller than the window.
		body = center(maxWidth(p.pt(480), scroll("hub/overlay", overlay)));
	else
		body = row({width(p.textPt(210), sectionNav(p, false)), expanded(scroll("hub/scroll", column(std::move(content), {p.pt(12)})))},
				   {p.pt(18), CrossAlign::Stretch});
	std::vector<MenuAction> buttons{{"back", tr("[Back]"), [this] { onEscape(); }, false, SDLK_ESCAPE}};
	// The game version identifies the build; the simulation version is for Settings.
	auto footerRow = row({expanded(caption(instance + " · " + PACKAGE_VERSION)), actions(std::move(buttons), p)}, {p.pt(8), CrossAlign::Center});
	std::vector<Element> page{headline};
	for (auto &t : toast)
		page.push_back(t);
	if (!overlay)
		if (auto strip = SearchStrip::build(searchModel(), p, [this] { openSearch(); }))
			page.push_back(strip);
	page.push_back(expanded(body));
	page.push_back(divider());
	page.push_back(footerRow);
	const int w = std::min(p.safe.w - p.pt(24), p.pt(1100));
	const int h = std::min(p.safe.h - p.pt(24), p.pt(760));
	return center(sized({w, h}, card(column(std::move(page), {p.pt(10)}), {.padding = p.pt(18)})));
}
