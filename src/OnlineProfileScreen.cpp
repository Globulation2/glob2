// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineProfileScreen.h"

#include "FormatableString.h"
#include "MapCatalog.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "ui/OnlineUI.h"

#include <FileManager.h>
#include <StreamBackend.h>
#include <ScreenStack.h>
#include <Toolkit.h>

#include <algorithm>
#include <ctime>
#include <memory>

using namespace Glob2UI;
using GAGCore::FormattableString;

namespace
{
std::string monthYear(std::int64_t at)
{
	const std::time_t t = static_cast<std::time_t>(at / 1000);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	static const char *months[] = {"[online jan]", "[online feb]", "[online mar]", "[online apr]",
								   "[online may]", "[online jun]", "[online jul]", "[online aug]",
								   "[online sep]", "[online oct]", "[online nov]", "[online dec]"};
	return tr(months[std::clamp(tm.tm_mon, 0, 11)]) + " " + std::to_string(1900 + tm.tm_year);
}

GAGCore::Color outcomeColor(const std::string &outcome)
{
	const auto &palette = frontendTheme().palette;
	if (outcome == "won")
		return palette.success;
	if (outcome == "lost" || outcome == "abandoned")
		return palette.danger;
	return palette.muted;
}

std::string outcomeLetter(const std::string &outcome)
{
	if (outcome == "won")
		return tr("[profile win letter]");
	if (outcome == "lost" || outcome == "abandoned")
		return tr("[profile loss letter]");
	return tr("[profile draw letter]");
}
} // namespace

OnlineProfileScreen::OnlineProfileScreen(GAGGUI::ScreenStack &screens)
	: OnlineProfileScreen(screens, std::string())
{
}

OnlineProfileScreen::OnlineProfileScreen(GAGGUI::ScreenStack &screens, std::string accountId)
	: screens(screens), flow(screens), live(true)
{
	data.accountId = std::move(accountId);
	data.instance = onlineClient().origin();
}

OnlineProfileScreen::OnlineProfileScreen(GAGGUI::ScreenStack &screens, Data fixed)
	: screens(screens), flow(screens), live(false), data(std::move(fixed))
{
	summarize();
	started = true;
}

void OnlineProfileScreen::summarize()
{
	summary = data.profile ? Online::summarizeProfile(*data.profile, data.matches)
						   : Online::summarizeProfile(data.accountId, data.matches);
}

OnlineProfileScreen::~OnlineProfileScreen()
{
	*alive = false;
}

void OnlineProfileScreen::onEscape()
{
	if (selected >= 0)
		select(-1);
	else
		endExecute(BACK);
}

void OnlineProfileScreen::onTimer(Uint32)
{
	if (!live || started)
		return;
	auto &client = Online::services().client;
	if (client.auth() != Online::PlatformClient::Auth::SignedIn || !client.account())
		return;
	started = true;
	if (data.accountId.empty())
		data.accountId = client.account()->id;
	data.instance = client.origin();
	auto alive = this->alive;
	client.rest(HttpFetch::Method::Get, "/api/v1/instance", Online::Json(),
				[this, alive](const Online::PlatformClient::Response &response)
				{
					if (!*alive || !response.ok)
						return;
					for (const auto &queue : Online::queuesFromInstance(response.result))
						data.ladderNames[queue.id] = queue.name;
					invalidate();
				});
	client.rest(HttpFetch::Method::Get, "/api/v1/players/" + Online::urlEncode(data.accountId), Online::Json(),
				[this, alive](const Online::PlatformClient::Response &response)
				{
					if (!*alive || !response.ok)
						return;
					data.profile = Online::PlayerProfile::fromJson(response.result);
					if (!data.profile)
						return;
					data.displayName = data.profile->displayName;
					data.kind = data.profile->kind;
					if (data.profile->createdAt)
						data.since = monthYear(*data.profile->createdAt);
					summarize();
					invalidate();
				});
	load(false);
}

void OnlineProfileScreen::load(bool more)
{
	if (loading)
		return;
	loading = true;
	problem.clear();
	std::string path = "/api/v1/players/" + Online::urlEncode(data.accountId) + "/matches?limit=50";
	if (more && !cursor.empty())
		path += "&cursor=" + Online::urlEncode(cursor);
	auto alive = this->alive;
	Online::services().client.rest(HttpFetch::Method::Get, path, Online::Json(),
								   [this, alive, more](const Online::PlatformClient::Response &response)
								   {
									   if (!*alive)
										   return;
									   loading = false;
									   if (!response.ok)
									   {
										   problem = response.error.code == "not_found" || response.error.code == "http_404"
														 ? tr("[profile history unavailable]")
														 : (response.error.message.empty() ? tr("[online connection problem]")
																						   : response.error.message);
										   invalidate();
										   return;
									   }
									   auto page = Online::parseMatchList(response.result);
									   if (!more)
										   data.matches.clear();
									   for (auto &match : page.items)
										   data.matches.push_back(std::move(match));
									   cursor = page.nextCursor;
									   data.now = wallClockMs();
									   summarize();
									   invalidate();
								   });
	invalidate();
}

void OnlineProfileScreen::loadMore()
{
	if (live && !cursor.empty())
		load(true);
}

void OnlineProfileScreen::setFilter(Filter value)
{
	filter = value;
	selected = -1;
	invalidate();
}

void OnlineProfileScreen::select(int index)
{
	selected = index;
	invalidate();
}

void OnlineProfileScreen::openWebProfile()
{
	openInstancePage(data.instance, "/players/" + data.accountId);
}

void OnlineProfileScreen::openMatchPage(const std::string &matchId)
{
	openInstancePage(data.instance, "/matches/" + matchId);
}

void OnlineProfileScreen::replay(const std::string &matchId)
{
	if (!live)
		return;
	status = tr("[profile downloading replay]");
	invalidate();
	auto alive = this->alive;
	// The verified replay the server keeps for the match.
	Online::services().client.restRaw(
		HttpFetch::Method::Get, "/api/v1/matches/" + Online::urlEncode(matchId) + "/artifacts/replay", {}, {},
		[this, alive, matchId](const Online::PlatformClient::Response &file)
		{
			if (!*alive)
				return;
			if (!file.ok || file.body.empty())
			{
				status = tr("[profile no replay]");
				invalidate();
				return;
			}
			const std::string path = "replays/online-" + matchId + ".replay";
			std::unique_ptr<GAGCore::StreamBackend> out(GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(path));
			if (!out || !out->isValid())
			{
				status = tr("[profile no replay]");
				invalidate();
				return;
			}
			out->write(file.body.data(), file.body.size());
			out.reset();
			status.clear();
			invalidate();
			flow.replay(path);
		},
		64 * 1024 * 1024);
}

std::vector<int> OnlineProfileScreen::visible() const
{
	std::vector<int> out;
	for (int i = 0; i < int(data.matches.size()); ++i)
	{
		const auto &m = data.matches[i];
		const bool keep = filter == Filter::All || (filter == Filter::Ranked && m.rated) ||
						  (filter == Filter::Rooms && m.origin == "room") || (filter == Filter::VersusAi && m.aiFilled());
		if (keep)
			out.push_back(i);
	}
	return out;
}

std::string OnlineProfileScreen::ladderName(const std::string &ladder) const
{
	auto found = data.ladderNames.find(ladder);
	return found != data.ladderNames.end() && !found->second.empty() ? found->second : ladder;
}

std::string OnlineProfileScreen::matchTitle(const Online::MatchSummary &match) const
{
	const auto *me = match.participant(data.accountId);
	std::vector<std::string> allies, rivals;
	for (const auto &p : match.participants)
	{
		if (me && &p == me)
			continue;
		const std::string name = p.human ? p.displayName : p.displayName + " (" + tr("[qm ai badge]") + ")";
		(me && p.team == me->team ? allies : rivals).push_back(name);
	}
	auto join = [](const std::vector<std::string> &names)
	{
		std::string out;
		for (std::size_t i = 0; i < names.size() && i < 3; ++i)
			out += (i ? ", " : "") + names[i];
		if (names.size() > 3)
			out += "\xE2\x80\xA6";
		return out;
	};
	if (match.origin == "room" && match.participants.size() > 2)
		return FormattableString(tr("[profile room with %0]")).arg(int(match.participants.size()));
	std::string text;
	if (!allies.empty())
		text = FormattableString(tr("[profile with %0 vs %1]")).arg(join(allies)).arg(join(rivals));
	else
		text = FormattableString(tr("[profile vs %0]")).arg(join(rivals));
	const std::size_t perSide = std::max<std::size_t>(1, match.participants.size() / 2);
	return std::to_string(perSide) + " " + tr("[qm vs]") + " " + std::to_string(perSide) + " \xC2\xB7 " + text;
}

std::string OnlineProfileScreen::matchKind(const Online::MatchSummary &match) const
{
	std::string kind = match.origin == "room" ? tr("[profile room]") : match.rated ? tr("[qm ranked]") : tr("[profile casual]");
	if (match.origin == "queue" && match.aiFilled())
		kind += " \xC2\xB7 " + tr("[profile ai filled]");
	return kind;
}

Element OnlineProfileScreen::ratingCard(const Online::LadderSummary &ladder, const Presentation &p, bool phone)
{
	std::vector<Element> top{label(std::to_string(long(std::lround(ladder.rating))), {FontRole::Title})};
	if (ladder.rank && !ladder.provisional)
		top.push_back(caption("#" + std::to_string(*ladder.rank)));
	if (ladder.provisional)
		top.push_back(badge(FormattableString(tr("[profile provisional %0 games]")).arg(ladder.games), frontendTheme().palette.focus));
	std::vector<Element> parts{caption(ladderName(ladder.ladder)), row(std::move(top), {p.pt(8), CrossAlign::Center})};
	if (ladder.lastChange && phone)
		parts.push_back(caption((ladder.rank && !ladder.provisional ? "#" + std::to_string(*ladder.rank) + " \xC2\xB7 " : std::string()) +
								signedText(*ladder.lastChange)));
	if (!phone)
		parts.push_back(sparkline(ladder.trend, ladder.provisional, {p.pt(200), p.pt(26)}));
	CardOptions options;
	options.shadow = false;
	options.border = frontendTheme().palette.line;
	options.color = frontendTheme().palette.field;
	options.padding = p.pt(10);
	return card(column(std::move(parts), {p.pt(4)}), options);
}

Element OnlineProfileScreen::matchRow(int index, const Presentation &p, bool phone)
{
	const auto &match = data.matches[index];
	const auto *me = match.participant(data.accountId);
	const std::string outcome = me ? me->outcome : std::string();
	const std::string key = "profile/match/" + match.id;
	std::string change;
	if (!match.rated)
		change = tr("[profile unrated]");
	else if (me && me->rating)
		change = signedText(me->rating->after - me->rating->before);
	else if (match.verification == "pending" || match.status != "ended")
		change = tr("[profile checking]");
	else
		change = "\xE2\x80\x93";
	std::string minutes;
	if (match.durationTicks)
		minutes = FormattableString(tr("[profile %0 min]")).arg(int(std::lround(*match.durationTicks / (25.0 * 60))));
	const std::string when = match.endedAt ? ageText(*match.endedAt, data.now ? data.now : wallClockMs())
							 : match.startedAt ? ageText(*match.startedAt, data.now ? data.now : wallClockMs())
											   : std::string();
	std::string sub = match.mapTitle;
	if (!when.empty())
		sub += (sub.empty() ? "" : " \xC2\xB7 ") + when;

	CardOptions letter;
	letter.shadow = false;
	letter.color = outcomeColor(outcome);
	letter.padding = p.pt(4);
	Element mark = width(p.pt(26), card(label(outcomeLetter(outcome), {FontRole::Caption, false, TextAlign::Center, frontendTheme().palette.paper}), letter));
	GAGCore::Color changeColor = frontendTheme().palette.muted;
	if (me && me->rating)
		changeColor = me->rating->after >= me->rating->before ? frontendTheme().palette.success : frontendTheme().palette.danger;

	if (phone)
	{
		auto text = column({label(matchTitle(match)), caption(sub + (minutes.empty() ? "" : " \xC2\xB7 " + minutes))}, {p.pt(2)});
		ButtonOptions options;
		options.flat = true;
		options.alignLeft = true;
		options.selected = selected == index;
		options.accessibleLabel = matchTitle(match);
		return stack({button(key, "", [this, index] { select(selected == index ? -1 : index); }, options),
					  padding(Insets::symmetric(p.pt(8), p.pt(4)),
							  row({mark, expanded(text), label(change, {FontRole::Heading, false, TextAlign::Right, changeColor})},
								  {p.pt(8), CrossAlign::Center}))});
	}
	ButtonOptions replayOptions;
	replayOptions.icon = uiIcon(UIIcon::CustomGame);
	ButtonOptions pageOptions;
	pageOptions.icon = uiIcon(UIIcon::ExternalLink);
	pageOptions.accessibleLabel = tr("[profile match page]");
	pageOptions.tooltip = tr("[profile match page]");
	return row({mark, expanded(column({label(matchTitle(match)), caption(sub)}, {p.pt(2)}), 3),
				expanded(caption(matchKind(match)), 1), width(p.pt(64), caption(minutes)),
				width(p.pt(80), label(change, {FontRole::Body, false, TextAlign::Left, changeColor})),
				button(key + "/replay", tr("[profile replay]"), [this, id = match.id] { replay(id); }, replayOptions),
				width(p.pt(40), button(key + "/page", "", [this, id = match.id] { openMatchPage(id); }, pageOptions))},
			   {p.pt(8), CrossAlign::Center});
}

Element OnlineProfileScreen::build(const Presentation &p)
{
	const bool phone = p.touch && p.compact();
	std::vector<Element> body;

	// Ratings and aggregates.
	std::vector<Element> cards;
	for (const auto &ladder : summary.ladders)
		cards.push_back(ratingCard(ladder, p, phone));
	CardOptions stat;
	stat.shadow = false;
	stat.border = frontendTheme().palette.line;
	stat.color = frontendTheme().palette.field;
	stat.padding = p.pt(10);
	if (summary.recent > 0)
	{
		const int percent = int(std::lround(100.0 * summary.wins / std::max(1, summary.recent)));
		std::vector<Element> parts{caption(FormattableString(tr("[profile win rate last %0]")).arg(summary.recent)),
								   label(std::to_string(percent) + " %", {FontRole::Title})};
		if (!phone)
			parts.push_back(caption(FormattableString(tr("[profile %0 w %1 l %2 d]")).arg(summary.wins).arg(summary.losses).arg(summary.draws)));
		cards.push_back(card(column(std::move(parts), {p.pt(4)}), stat));
	}
	if (summary.medianMinutes)
		cards.push_back(card(column({caption(tr("[profile typical game]")),
									 label(FormattableString(tr("[profile %0 min]")).arg(*summary.medianMinutes), {FontRole::Title}),
									 phone ? empty() : caption(tr("[profile median length]"))},
									{p.pt(4)}),
							 stat));
	if (!summary.bestMap.empty() && !phone)
		cards.push_back(card(column({caption(tr("[profile best map]")), label(summary.bestMap, {FontRole::Heading}),
									 caption(FormattableString(tr("[profile %0 w %1 l]")).arg(summary.bestMapWins).arg(summary.bestMapLosses))},
									{p.pt(4)}),
							 stat));
	if (!cards.empty())
		body.push_back(wrap(std::move(cards), {p.pt(8), phone ? p.pt(130) : p.pt(170), phone ? 2 : 5}));
	else if (!loading && problem.empty())
		body.push_back(paragraph(tr("[profile no rated games]"), {FontRole::Body, true}));

	// Matches.
	std::vector<std::string> filters{tr("[profile all]"), tr("[qm ranked]"), tr("[profile rooms]"), tr("[profile vs ai]")};
	Element filterControl = segments("profile/filter", filters, int(filter), [this](int i) { setFilter(Filter(i)); });
	body.push_back(phone ? column({label(tr("[profile matches]"), {FontRole::Heading}), filterControl}, {p.pt(6)})
						 : row({expanded(label(tr("[profile matches]"), {FontRole::Heading})), width(p.pt(360), filterControl)},
							   {p.pt(8), CrossAlign::Center}));
	std::vector<Element> rows;
	for (int index : visible())
	{
		if (!rows.empty())
			rows.push_back(divider());
		rows.push_back(matchRow(index, p, phone));
	}
	if (rows.empty())
		rows.push_back(paragraph(loading ? tr("[online loading]") : problem.empty() ? tr("[profile no matches]") : problem,
								 {FontRole::Body, true}));
	if (!cursor.empty())
		rows.push_back(button("profile/more", tr("[online load more]"), [this] { loadMore(); }));
	CardOptions list;
	list.shadow = false;
	list.border = frontendTheme().palette.line;
	list.color = frontendTheme().palette.field;
	list.padding = p.pt(8);
	body.push_back(card(column(std::move(rows), {p.pt(6)}), list));
	if (!status.empty())
		body.push_back(caption(status));

	OnlinePanel panel;
	panel.title = data.displayName.empty() ? tr("[profile title]") : data.displayName;
	std::string subtitle = originHost(data.instance);
	if (data.kind == "guest")
		subtitle += " \xC2\xB7 " + tr("[profile guest]");
	if (!data.since.empty())
		subtitle += " \xC2\xB7 " + std::string(FormattableString(tr("[profile playing since %0]")).arg(data.since));
	panel.subtitle = subtitle;
	ButtonOptions web;
	web.icon = uiIcon(UIIcon::ExternalLink);
	if (!phone)
		panel.headerRight = button("profile/web", tr("[profile full profile web]"), [this] { openWebProfile(); }, web);
	panel.body = scroll("profile/body", column(std::move(body), {p.pt(10)}));
	panel.note = tr("[profile replays note]");
	panel.actions = {{"back", tr("[goto main menu]"), [this] { endExecute(BACK); }, false, SDLK_ESCAPE}};
	if (phone)
	{
		ButtonOptions backOptions;
		backOptions.icon = uiIcon(UIIcon::Back);
		backOptions.accessibleLabel = tr("[goto main menu]");
		backOptions.shortcut = SDLK_ESCAPE;
		Element backButton = width(p.pt(56), button("back", "", [this] { onEscape(); }, backOptions));
		if (selected >= 0 && selected < int(data.matches.size()))
		{
			const auto &match = data.matches[selected];
			ButtonOptions replayOptions;
			replayOptions.icon = uiIcon(UIIcon::CustomGame);
			replayOptions.primary = true;
			ButtonOptions pageOptions;
			pageOptions.icon = uiIcon(UIIcon::ExternalLink);
			panel.thumbBlock = column({label(matchTitle(match), {FontRole::Heading}), caption(matchKind(match)),
									   row({backButton, expanded(button("profile/sheet/page", tr("[profile match page]"),
																		[this, id = match.id] { openMatchPage(id); }, pageOptions)),
											expanded(button("profile/sheet/replay", tr("[profile replay]"),
															[this, id = match.id] { replay(id); }, replayOptions))},
										   {p.pt(8)})},
									  {p.pt(6)});
		}
		else
			panel.thumbBlock = row({backButton, expanded(button("profile/web", tr("[profile web profile]"), [this] { openWebProfile(); }, web))},
								   {p.pt(8)});
	}
	return onlinePanel(std::move(panel), p);
}
