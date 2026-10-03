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
#include "gui/ThumbSide.h"
#include "ui/OnlineUI.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <random>

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

OnlineHubScreen::OnlineHubScreen(GAGGUI::ScreenStack &screens, bool connect) : screens(screens)
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
			rematchRoom = Online::PlatformRoom::rematch(client(), Online::services().maps, request.matchId);
		});
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
	if (const auto &queue = Online::quickMatch().queue())
	{
		context.label = queue->name.empty() ? queue->id : queue->name;
		context.label += " · " + tr(queue->rated ? "[hub ranked]" : "[hub unrated]");
		context.rated = queue->rated;
		context.ladder = queue->id;
	}
	auto match = std::make_shared<Online::OnlineMatch>(client(), Online::services().maps, assignment, context);
	screens.push(std::make_unique<MatchStartScreen>(screens, match), [this](GAGGUI::Screen &, int) {
		// Rematch from the results screen: its room opens once the match has closed.
		if (auto room = std::move(rematchRoom))
			enterRoom(std::move(room));
		refresh(true);
	});
}

void OnlineHubScreen::openProfile()
{
	screens.push(std::make_unique<OnlineProfileScreen>(screens), [this](GAGGUI::Screen &, int) { refresh(true); });
}

void OnlineHubScreen::openMaps(bool mine)
{
	screens.push(std::make_unique<OnlineMapsScreen>(screens, mine ? OnlineMapsScreen::Tab::Mine : OnlineMapsScreen::Tab::Browse));
}

Online::PlatformClient &OnlineHubScreen::client()
{
	return Online::services().client;
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
		data.link = platform.simSupported() ? Model::Link::Online : Model::Link::UpdateRequired;
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
	// The leaderboard teaser: the top five of the main queue (the first rated one).
	if (!fetchingLeaderboard && data.queues.is_array() && !data.queues.empty())
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
			calls->rest(HttpFetch::Method::Get, Online::Api::leaderboard(ladder, 5), Json(),
						[this](const Online::PlatformClient::Response &r) {
							fetchingLeaderboard = false;
							if (!r.ok)
								return;
							data.leaderboard = r.result.value("entries", Json::array());
							invalidate();
						});
		}
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
	if (previewing)
		return;
	syncFromClient();
	refresh(false);
	// Invite links that arrived while the game runs (or at launch) land here.
	if (!trustPrompt)
		if (auto invite = Online::takePendingJoin())
			acceptInvite(invite->origin, invite->code);
	if (pendingInvite && client().connection() == Online::PlatformClient::Connection::Online &&
		client().origin() == pendingInvite->origin && client().auth() == Online::PlatformClient::Auth::SignedIn)
	{
		const std::string code = pendingInvite->code;
		pendingInvite.reset();
		joinByCode(code);
	}
}

bool OnlineHubScreen::canPlay() const
{
	return data.link == Model::Link::Online && !data.accountId.empty();
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
	if (!canPlay())
		return;
	// A fair 128×128 two-colony map: most rooms are two friends. It grows to four
	// colonies when more people join, until the host chooses a map.
	const auto setup = Online::defaultRoomSetup(2, std::random_device{}());
	const std::string name = formatted("[hub room name %0]", data.displayName);
	enterRoom(Online::PlatformRoom::create(client(), Online::services().maps, name, false, setup, true));
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
	enterRoom(Online::PlatformRoom::join(client(), Online::services().maps, invite->code));
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
	auto &search = Online::quickMatch();
	search.search(*info, search.allowAiOpponent());
	screens.push(std::make_unique<QuickMatchScreen>(screens), [this](GAGGUI::Screen &, int result) { quickMatchClosed(result); });
}

void OnlineHubScreen::quickMatchClosed(int result)
{
	if (auto match = std::move(deferredMatch))
	{
		deferredMatch.reset();
		startMatch(*match);
		return;
	}
	auto &search = Online::quickMatch();
	if (result == QuickMatchScreen::SEARCH_ENDED)
	{
		const std::string notice = QuickMatchScreen::noticeText(search);
		search.dismissNotice();
		if (!notice.empty())
			showToast(notice);
	}
	else if (result == QuickMatchScreen::ACCOUNT)
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
	// A guest links the identity, so their matches and maps come along.
	client().beginBrowserSignIn("", provider);
	data.signIn = Model::SignIn::Waiting;
	invalidate();
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
		endExecute(0);
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
		cells.push_back(button("account/signin", tr("[hub sign in]"), [this] { openSignIn(); }, {.primary = true, .icon = uiIcon(UIIcon::SignIn), .iconSize = 16}));
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
		return card(column({row({icon(uiIcon(UIIcon::Warning), {22, theme().palette.danger}), expanded(column({heading(tr("[hub update required]")), paragraph(formatted("[hub update required detail %0]", hostOf(data.origin)), {FontRole::Support, true})}, {p.pt(2)}))}, {p.pt(10), CrossAlign::Start}),
							row({button("banner/update", tr("[hub get update]"), [] { GAGCore::ApplicationHost::openUrl("https://globulation2.org/download"); }, {.primary = true}),
								 button("banner/server", tr("[hub change server]"), [this] { openSettings(); })},
								{p.pt(8)})},
						   {p.pt(8)}),
					{.color = theme().palette.field, .padding = p.pt(10), .shadow = false, .border = theme().palette.danger});
	return nullptr;
}

Element OnlineHubScreen::quickMatch(const Presentation &p, bool phone)
{
	const bool enabled = canPlay();
	std::vector<Element> cards;
	for (std::size_t i = 0; i < data.queues.size(); ++i)
	{
		const Json &queue = data.queues[i];
		std::string detail = queue.value("rated", false) ? tr("[hub ranked]") : tr("[hub unrated]");
		if (queue.contains("aiBackfillSeconds") && queue["aiBackfillSeconds"].is_number_integer())
			detail += " · " + formatted("[hub ai joins after %0]", clockText(queue["aiBackfillSeconds"].get<int>()));
		ButtonOptions find;
		find.primary = i == 0;
		find.enabled = enabled;
		const int index = int(i);
		cards.push_back(card(column({row({icon(uiIcon(queue.value("rated", false) ? UIIcon::Bolt : UIIcon::Robot), {18}), label(queueDisplayName(queue.value("id", ""), queue.value("name", "")), {FontRole::Body})}, {p.pt(6), CrossAlign::Center}),
									 caption(detail),
									 button("queue/" + std::to_string(i), tr("[hub find match]"), [this, index] { findMatch(index); }, find)},
									{p.pt(6)}),
							 {.color = theme().palette.field, .padding = p.pt(10), .shadow = false, .border = theme().palette.line}));
	}
	if (cards.empty())
		cards.push_back(paragraph(data.link == Model::Link::Online ? tr("[hub no queues]") : tr("[hub queues unavailable]"), {FontRole::Support, true}));
	return column({heading(tr("[hub quick match]")), wrap(std::move(cards), {p.pt(8), p.pt(170), 3})}, {p.pt(6)});
}

Element OnlineHubScreen::roomList(const Presentation &p, bool phone)
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
		rows.push_back(row({icon(uiIcon(UIIcon::Users), {20, theme().palette.muted}), expanded(words),
							button("rooms/" + std::to_string(i) + "/join", tr("[hub join]"), [this, code] { joinByCode(code); }, join)},
						   {p.pt(8), CrossAlign::Center}));
		rows.push_back(divider());
	}
	if (shown == 0)
		rows.push_back(paragraph(tr("[hub no rooms]"), {FontRole::Support, true}));
	std::vector<Element> head{expanded(heading(tr("[hub open rooms]")))};
	if (!phone)
		// A width that holds both labels on one line ("Free seat" used to wrap, H-1).
		head.push_back(width(p.textPt(220), segments("rooms/filter", {tr("[hub all rooms]"), tr("[hub free seat]")}, roomFilter, [this](int v) { roomFilter = v; invalidate(); })));
	ButtonOptions reload;
	reload.icon = uiIcon(UIIcon::Refresh);
	reload.accessibleLabel = tr("[hub refresh]");
	reload.tooltip = reload.accessibleLabel;
	head.push_back(width(p.pt(p.touch ? 48 : 34), button("rooms/refresh", "", [this] { refresh(true); }, reload)));
	return column({row(std::move(head), {p.pt(6), CrossAlign::Center}), column(std::move(rows), {p.pt(4)})}, {p.pt(6)});
}

Element OnlineHubScreen::recentMatches(const Presentation &p, bool phone)
{
	if (!data.recent.is_array() || data.recent.empty())
		return nullptr;
	const auto palette = theme().palette;
	std::vector<Element> rows;
	for (std::size_t i = 0; i < data.recent.size() && i < (phone ? 3u : 5u); ++i)
	{
		const auto line = recentLine(data.recent[i], data.accountId);
		const GAGCore::Color tone = line.outcome == "W" ? palette.success : line.outcome == "L" ? palette.danger : palette.neutral;
		auto badge = sized({p.pt(22), p.pt(22)}, canvas("", {p.pt(22), p.pt(22)}, [tone, letter = line.outcome, palette](Canvas &c, Rect r, const Frame &) {
			c.fillRounded(r, 3, tone);
			const int w = c.measurer().width(FontRole::Support, letter);
			c.text({r.x + (r.w - w) / 2, r.y + (r.h - c.measurer().lineHeight(FontRole::Support)) / 2}, FontRole::Support, letter, palette.paper);
		}));
		std::vector<Element> cells{badge, expanded(column({label(line.title), caption(line.detail)}, {0})), caption(line.rating, false)};
		if (line.verified)
			cells.push_back(icon(uiIcon(UIIcon::ShieldCheck), {14, palette.success}));
		rows.push_back(row(std::move(cells), {p.pt(8), CrossAlign::Center}));
	}
	return column({heading(tr("[hub recent matches]")), column(std::move(rows), {p.pt(6)})}, {p.pt(6)});
}

Element OnlineHubScreen::leaderboardTeaser(const Presentation &p)
{
	std::vector<Element> rows;
	for (std::size_t i = 0; i < data.leaderboard.size() && i < 5; ++i)
	{
		const Json &entry = data.leaderboard[i];
		const Json &entity = entry.value("entity", Json::object());
		std::string name = entity.value("kind", "") == "ai" ? entity.value("ai", "AI")
															 : entity.value("account", Json::object()).value("displayName", "?");
		const int rating = int(std::lround(entry.value("rating", 0.0)));
		rows.push_back(row({width(p.pt(22), caption(std::to_string(entry.value("rank", int(i) + 1)), false)), expanded(label(name)),
							caption(std::to_string(rating), false)},
						   {p.pt(8), CrossAlign::Center}));
	}
	if (rows.empty())
		rows.push_back(paragraph(tr("[hub leaderboard empty]"), {FontRole::Support, true}));
	std::vector<Element> foot;
	if (data.accountKind == "guest")
		foot.push_back(expanded(paragraph(tr("[hub guests not ranked]"), {FontRole::Support, true})));
	else
		foot.push_back(expanded(spacer(0)));
	foot.push_back(button("leaderboard/full", tr("[hub full leaderboard]"), [this] { GAGCore::ApplicationHost::openUrl(data.origin + "/leaderboard"); },
						  {.icon = uiIcon(UIIcon::ExternalLink), .iconSize = 16}));
	const std::string title = data.leaderboardName.empty() ? tr("[hub leaderboard]") : formatted("[hub leaderboard %0]", data.leaderboardName);
	return column({heading(title), column(std::move(rows), {p.pt(4)}), row(std::move(foot), {p.pt(6), CrossAlign::Center})}, {p.pt(6)});
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
		parts.push_back(paragraph(tr("[hub finish in browser]")));
		parts.push_back(caption(tr("[hub check code]")));
		std::string code = data.confirmationCode;
		if (code.size() == 6)
			code = code.substr(0, 3) + " · " + code.substr(3);
		parts.push_back(center(title(code.empty() ? "…" : code)));
		parts.push_back(paragraph(formatted("[hub page opened at %0]", hostOf(data.origin) + "/signin"), {FontRole::Support, true}));
		parts.push_back(row({button("signin/reopen", tr("[hub open page again]"), [this] { if (!previewing) client().openSignInPage(); }),
							 button("signin/cancel", tr("[Cancel]"), [this] { cancelSignIn(); }, {.shortcut = SDLK_ESCAPE})},
							{p.pt(8)}));
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
	std::string linked;
	for (const auto &provider : data.linkedProviders)
		linked += (linked.empty() ? "" : ", ") + providerName(provider);
	if (guest)
		items.push_back(button("account/signin-menu", tr("[hub sign in]"), [this] { openSignIn(); }, {.flat = true, .alignLeft = true, .icon = uiIcon(UIIcon::SignIn)}));
	items.push_back(button("account/linked", linked.empty() ? tr("[hub linked accounts]") : formatted("[hub linked %0]", linked), [this] { openSettings(); }, {.flat = true, .alignLeft = true, .icon = uiIcon(UIIcon::Link)}));
	items.push_back(button("account/server", formatted("[hub server %0]", hostOf(data.origin)), [this] { openSettings(); }, {.flat = true, .alignLeft = true, .icon = uiIcon(UIIcon::Server)}));
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
		selectedQueue = std::clamp(selectedQueue, 0, int(names.size()) - 1);
		if (stacked)
			// One full-width choice per line: three side by side do not hold their
			// names on a narrow phone with large text.
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				const int index = int(i);
				parts.push_back(button("queue/choice/" + std::to_string(i), names[i], [this, index] { selectedQueue = index; invalidate(); },
									   {.selected = index == selectedQueue}));
			}
		else
			parts.push_back(segments("queue/choice", names, selectedQueue, [this](int v) { selectedQueue = v; invalidate(); }));
		parts.push_back(button("queue/find", tr("[hub find match]"), [this] { findMatch(selectedQueue); }, {.primary = true, .enabled = canPlay(), .icon = uiIcon(UIIcon::Bolt)}));
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
		status = formatted("[hub update required at %0]", hostOf(data.origin));
		break;
	}
	auto headline = row({expanded(column({phone ? heading(tr("[hub online]")) : title(tr("[hub online]")), caption(status)}, {0})), accountChip(p)}, {p.pt(8), CrossAlign::Center});
	if (p.compact())
		// A narrow phone (or large text) gives the status line the full width under
		// the title and account chip, rather than wrapping it word by word beside the
		// chip and pushing Back off the bottom.
		headline = column({row({expanded(heading(tr("[hub online]"))), accountChip(p)}, {p.pt(8), CrossAlign::Center}), caption(status)}, {0});
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
		if (auto b = banner(p))
			list.push_back(b);
		if (crowded)
			if (auto picker = queuePicker(p, true))
				list.push_back(picker);
		list.push_back(roomList(p, true));
		if (auto recent = recentMatches(p, true))
			list.push_back(recent);
		// Both share the width, so a narrow phone never pushes Maps past the edge.
		list.push_back(row({expanded(button("profile", tr("[hub profile history]"), [this] { openProfile(); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Users), .iconSize = 16})),
							expanded(button("maps/browse", tr("[hub maps]"), [this] { openMaps(false); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Map), .iconSize = 16}))},
						   {p.pt(6)}));
		Element body = overlay ? scroll("hub/overlay", overlay) : scroll("hub/scroll", column(std::move(list), {p.pt(12)}));
		std::vector<Element> page{headline};
		for (auto &t : toast)
			page.push_back(t);
		if (!overlay && p.landscape())
			// Landscape: the thumb block becomes the right-hand (thumb-side) rail.
			page.push_back(expanded(ThumbSide::left() ? row({width(p.pt(250), scroll("hub/thumb", thumbBlock(p))), expanded(body)}, {p.pt(10), CrossAlign::End})
													  : row({expanded(body), width(p.pt(250), scroll("hub/thumb", thumbBlock(p)))}, {p.pt(10), CrossAlign::End})));
		else
		{
			page.push_back(expanded(body));
			if (!overlay)
				page.push_back(thumbBlock(p, !crowded));
		}
		return padding({p.pt(4), p.pt(4), p.pt(4), p.pt(4)}, card(column(std::move(page), {p.pt(8)}), {.padding = p.pt(10)}));
	}
	std::vector<Element> leftColumn;
	if (auto b = banner(p))
		leftColumn.push_back(b);
	leftColumn.push_back(quickMatch(p, false));
	TextFieldOptions codeField;
	codeField.placeholder = tr("[hub code placeholder]");
	codeField.submit = [this](const std::string &v) { joinByCode(v); };
	leftColumn.push_back(row({button("room/create", tr("[hub create room]"), [this] { createRoom(); }, {.primary = true, .enabled = canPlay(), .icon = uiIcon(UIIcon::Plus)}),
							  spacer(p.pt(8)), caption(tr("[hub join by code]")),
							  expanded(textField("join/code", joinDraft, [this](const std::string &v) { joinDraft = v; }, codeField)),
							  button("join/go", tr("[hub join]"), [this] { joinByCode(joinDraft); }, {.enabled = canPlay() && !joinDraft.empty()})},
							 {p.pt(6), CrossAlign::Center}));
	leftColumn.push_back(roomList(p, false));
	std::vector<Element> rightColumn;
	if (auto recent = recentMatches(p, false))
		rightColumn.push_back(recent);
	rightColumn.push_back(row({button("profile", tr("[hub profile history]"), [this] { openProfile(); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Users), .iconSize = 16})}, {p.pt(6)}));
	rightColumn.push_back(heading(tr("[hub maps]")));
	rightColumn.push_back(row({button("maps/browse", tr("[hub browse maps]"), [this] { openMaps(false); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Map), .iconSize = 16}),
							   button("maps/mine", tr("[hub my maps]"), [this] { openMaps(true); }, {.enabled = canPlay(), .icon = uiIcon(UIIcon::Upload), .iconSize = 16})},
							  {p.pt(6)}));
	rightColumn.push_back(leaderboardTeaser(p));
	Element body;
	if (overlay)
		// Scrolls rather than squeezing its buttons or overlapping the footer when
		// large text makes it taller than the window.
		body = center(maxWidth(p.pt(480), scroll("hub/overlay", overlay)));
	else
		body = adaptive([left = column(std::move(leftColumn), {p.pt(12)}), right = column(std::move(rightColumn), {p.pt(8)})](const LayoutContext &ctx, Size available) -> Element {
			if (available.w < ctx.presentation.pt(860))
				return scroll("hub/scroll", column({left, right}, {ctx.presentation.pt(14)}));
			return row({expanded(scroll("hub/scroll", left), 3), expanded(scroll("hub/side", right), 2)}, {ctx.presentation.pt(18), CrossAlign::Stretch});
		});
	std::vector<MenuAction> buttons{{"settings", tr("[hub online settings]"), [this] { openSettings(); }},
									{"back", tr("[Back]"), [this] { onEscape(); }, false, SDLK_ESCAPE}};
	// The game version identifies the build; the simulation version is for Settings.
	// Where the two buttons and the version do not share a line (a portrait tablet
	// with large text), the version goes above the buttons.
	auto footerRow = adaptive([version = caption(instance + " · " + PACKAGE_VERSION),
							   footerActions = actions(std::move(buttons), p, ActionStyle::Compact)](const LayoutContext &ctx, Size available) -> Element {
		if (available.w < ctx.presentation.textPt(760))
			return column({version, footerActions}, {ctx.presentation.pt(6)});
		return row({expanded(version), footerActions}, {ctx.presentation.pt(8), CrossAlign::Center});
	});
	std::vector<Element> page{headline};
	for (auto &t : toast)
		page.push_back(t);
	page.push_back(expanded(body));
	page.push_back(divider());
	page.push_back(footerRow);
	const int w = std::min(p.safe.w - p.pt(24), p.pt(1100));
	const int h = std::min(p.safe.h - p.pt(24), p.pt(760));
	return center(sized({w, h}, card(column(std::move(page), {p.pt(10)}), {.padding = p.pt(18)})));
}
