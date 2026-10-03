// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickMatchScreen.h"

#include "FormatableString.h"
#include "GUIMapPreview.h"
#include "InstanceConfig.h"
#include "MapCache.h"
#include "MessageScreen.h"
#include "OnlineHandoff.h"
#include "OnlineServices.h"
#include "PlatformClient.h"
#include "QuickMatch.h"
#include "gui/ConnectionQuality.h"
#include "ui/OnlineUI.h"

#include <ScreenStack.h>

#include <algorithm>

using namespace Glob2UI;
using Online::QuickMatch;

namespace
{
using GAGCore::FormattableString;

// Colony colours of the first seats, as the match lobby shows them (data swatches).
GAGCore::Color seatColor(int slot)
{
	static const GAGCore::Color colors[] = {{220, 50, 40},	{120, 200, 60}, {50, 190, 220}, {230, 200, 40},
											{170, 80, 210}, {240, 140, 40}, {60, 90, 220},	{230, 110, 170}};
	return colors[std::clamp(slot, 0, 7)];
}

std::string modeText(const Online::QueueInfo &queue)
{
	return queue.mode == "2v2" ? tr("[qm 2 vs 2]") : tr("[qm 1 vs 1]");
}

std::string queueTitle(const Online::QueueInfo &queue)
{
	if (queue.rated)
		return modeText(queue) + " \xC2\xB7 " + tr("[qm ranked]");
	return FormattableString(tr("[qm casual %0]")).arg(modeText(queue));
}

std::string queueDetail(const Online::QueueInfo &queue)
{
	std::string detail = queue.rated ? tr("[qm rated]") : tr("[qm unrated]");
	if (queue.mode == "2v2")
		detail += " \xC2\xB7 " + tr("[qm solo queue]");
	if (queue.aiBackfillSeconds)
		detail += " \xC2\xB7 " + FormattableString(tr("[qm ai joins after %0]"))
									 .arg(clockText(*queue.aiBackfillSeconds));
	return detail;
}

std::string mapsLine(const Online::QueueInfo &queue)
{
	if (queue.maps.empty())
		return {};
	std::string names;
	const std::size_t shown = std::min<std::size_t>(3, queue.maps.size());
	for (std::size_t i = 0; i < shown; ++i)
		names += (i ? ", " : "") + generatorTitle(queue.maps[i]);
	if (queue.maps.size() > shown)
		names += "\xE2\x80\xA6";
	return FormattableString(tr("[qm fair maps %0]")).arg(names);
}

std::string errorText(const Online::ApiError &error, std::optional<std::int64_t> cooldown)
{
	if (error.code == "rate_limited" && cooldown)
		return FormattableString(tr("[qm cooldown %0]"))
			.arg(clockText(std::max<std::int64_t>(0, (*cooldown - wallClockMs()) / 1000)));
	if (error.code == "forbidden")
		return error.message.empty() ? tr("[qm sign in to play ranked]") : error.message;
	if (error.code == "update_required")
		return tr("[online update required]");
	if (error.code == "conflict")
		return tr("[qm already searching]");
	return error.message.empty() ? tr("[online connection problem]") : error.message;
}

struct PresenterState
{
	GAGGUI::ScreenStack *screens = nullptr;
	bool open = false;
	// The pump hook and handoff are installed once per process.
	bool hooked = false;
	std::optional<Online::MatchAssignment> deferred;
};
PresenterState &presenter()
{
	static PresenterState state;
	return state;
}
} // namespace

// ================================================================ queue

QuickMatchScreen::QuickMatchScreen(GAGGUI::ScreenStack &screens)
	: screens(screens), model(Online::quickMatch()), live(true)
{
	QuickMatchPresenter::attach(screens);
	auto &client = onlineClient();
	instance = client.origin();
	calls = std::make_unique<Online::PlatformScope>(client);
	loadQueues();
}

QuickMatchScreen::QuickMatchScreen(GAGGUI::ScreenStack &screens, QuickMatch &model,
								   std::vector<Online::QueueInfo> queues, std::string instance,
								   std::string account)
	: screens(screens), model(model), live(false), queues(std::move(queues)), queuesLoaded(true),
	  instance(std::move(instance)), account(std::move(account))
{
}

QuickMatchScreen::~QuickMatchScreen() = default;

void QuickMatchScreen::loadQueues()
{
	lastLoad = SDL_GetTicks();
	if (!calls)
		return;
	calls->instanceInfo([this](const Online::PlatformClient::Response &response)
						{
							if (response.ok)
							{
								queues = Online::queuesFromInstance(response.result);
								queuesLoaded = true;
								loadError.clear();
							}
							else
								loadError = errorText(response.error, {});
							invalidate();
						});
}

void QuickMatchScreen::find(const std::string &queueId)
{
	for (const auto &queue : queues)
		if (queue.id == queueId)
		{
			model.search(queue, model.allowAiOpponent());
			invalidate();
			return;
		}
}

void QuickMatchScreen::cancelSearch()
{
	model.cancel();
	invalidate();
}

void QuickMatchScreen::setAllowAi(bool allow)
{
	model.setAllowAiOpponent(allow);
	invalidate();
}

void QuickMatchScreen::back()
{
	// Leaving the quick-match section ends the search, as closing the hub does.
	if (model.active())
		model.cancel();
	endExecute(BACK);
}

void QuickMatchScreen::onEscape()
{
	if (model.active())
		cancelSearch();
	else
		back();
}

void QuickMatchScreen::onTimer(Uint32 tick)
{
	if (live)
	{
		model.update();
		if (!queuesLoaded && !loadError.empty() && tick - lastLoad > 5000)
			loadQueues();
		auto &client = Online::services().client;
		std::string name = client.account() ? client.account()->displayName : std::string();
		if (name != account || instance != client.origin())
		{
			account = name;
			instance = client.origin();
			invalidate();
		}
	}
	const int second = int(wallClockMs() / 1000);
	if (model.revision() != seen || (model.active() && second != shownSecond))
	{
		seen = model.revision();
		shownSecond = second;
		invalidate();
	}
}

Element QuickMatchScreen::queueCard(const Online::QueueInfo &queue, const Presentation &p, bool enabled)
{
	const UIIcon glyph = queue.mode == "2v2" ? UIIcon::Users : queue.rated ? UIIcon::Bolt : UIIcon::Robot;
	ButtonOptions findOptions;
	findOptions.enabled = enabled;
	findOptions.primary = enabled && queue.rated;
	auto content = column({row({icon(uiIcon(glyph), {20}), expanded(label(queueTitle(queue), {FontRole::Heading}))},
							   {p.pt(6), CrossAlign::Center}),
						   paragraph(queueDetail(queue), {FontRole::Support, true}),
						   align(Alignment::Left, button("qm/queue/" + queue.id + "/find", tr("[qm find match]"),
														  [this, id = queue.id] { find(id); }, findOptions))},
						  {p.pt(6)});
	CardOptions options;
	options.shadow = false;
	options.border = frontendTheme().palette.line;
	options.color = enabled ? frontendTheme().palette.field : frontendTheme().palette.disabled;
	return card(content, options);
}

Element QuickMatchScreen::searchPanel(const Presentation &p, bool phone)
{
	const auto &palette = frontendTheme().palette;
	const auto &queue = *model.queue();
	const auto &status = model.status();
	std::vector<Element> facts;
	auto fact = [&](UIIcon glyph, const std::string &text)
	{
		if (!text.empty())
			facts.push_back(row({icon(uiIcon(glyph), {16}), expanded(paragraph(text, {FontRole::Support}))},
								{p.pt(6), CrossAlign::Center}));
	};
	std::string range, region;
	// The region probe's round trip is an estimate of the match's Ping (the relay is
	// picked later), so it reads "ping about 42 ms · Good".
	auto estimate = [](int ms) {
		return ConnectionQuality::labelled(tr("[qm ping about]"),
		                                   ConnectionQuality::Metric::Ping, ms, [](const char *key) { return tr(key); });
	};
	if (status && status->ratingMin && status->ratingMax)
		range = FormattableString(tr("[qm opponents rated %0 to %1]")).arg(*status->ratingMin).arg(*status->ratingMax);
	if (status && !status->region.empty())
		region = FormattableString(tr("[qm region %0]")).arg(status->region) +
				 (status->rttMs ? " \xC2\xB7 " + estimate(*status->rttMs) : std::string());
	else if (!model.regions().empty())
		region = FormattableString(tr("[qm region %0]")).arg(model.regions().front().region) + " \xC2\xB7 " +
				 estimate(model.regions().front().rttMs);

	std::string phaseText = tr("[qm searching]");
	if (model.phase() == QuickMatch::Phase::Probing)
		phaseText = tr("[qm measuring relays]");
	else if (model.phase() == QuickMatch::Phase::Joining)
		phaseText = tr("[qm joining queue]");

	// AI backfill line and bar.
	std::vector<Element> backfill;
	if (queue.aiBackfillSeconds)
	{
		std::string line;
		const auto remaining = model.backfillInMs();
		if (!model.allowAiOpponent())
			line = tr("[qm waiting for a person]");
		else if (remaining)
			line = FormattableString(tr(queue.rated ? "[qm ai in %0 rated]" : "[qm ai in %0]"))
					   .arg(clockText((*remaining + 999) / 1000));
		else
			line = FormattableString(tr("[qm ai after %0]")).arg(clockText(*queue.aiBackfillSeconds));
		std::vector<Element> lineParts{icon(uiIcon(UIIcon::Robot), {16}),
									   expanded(paragraph(line, {FontRole::Support}))};
		if (status && !status->backfillAi.empty() && model.allowAiOpponent() && !phone)
			lineParts.push_back(caption(aiTitle(status->backfillAi) +
										(status->backfillAiRating ? " \xC2\xB7 " + std::to_string(*status->backfillAiRating) : "")));
		backfill.push_back(row(std::move(lineParts), {p.pt(6), CrossAlign::Center}));
		const int total = std::max(1, *queue.aiBackfillSeconds);
		const int elapsed = model.allowAiOpponent()
								? std::clamp(total - int(remaining.value_or(total * 1000ll) / 1000), 0, total)
								: 0;
		backfill.push_back(progress(elapsed, total));
	}
	auto allowAi = toggle("qm/allow-ai", phone ? tr("[qm allow ai short]") : tr("[qm allow ai]"), model.allowAiOpponent(),
						  [this](bool value) { setAllowAi(value); }, bool(queue.aiBackfillSeconds));
	ButtonOptions cancelOptions;
	cancelOptions.shortcut = SDLK_ESCAPE;
	auto cancel = button("qm/cancel", phone ? tr("[qm cancel]") : tr("[qm cancel search]"),
						 [this] { cancelSearch(); }, cancelOptions);

	const std::string clock = clockText(model.waitedSeconds());
	std::string usual;
	if (status && status->typicalWaitSeconds)
		usual = FormattableString(tr("[qm usual wait %0]")).arg(clockText(*status->typicalWaitSeconds));

	if (phone)
	{
		std::vector<Element> right;
		if (!range.empty() && status)
			right.push_back(caption(std::to_string(*status->ratingMin) + "\xE2\x80\x93" + std::to_string(*status->ratingMax), false));
		if (!region.empty())
			right.push_back(caption(status && !status->region.empty()
										? status->region + (status->rttMs ? " \xC2\xB7 " + estimate(*status->rttMs) : "")
										: region));
		std::vector<Element> parts{
			caption(phaseText + " \xC2\xB7 " + queueTitle(queue), false),
			row({expanded(label(clock, {FontRole::Title})), column(std::move(right), {p.pt(2), CrossAlign::End})},
				{p.pt(8), CrossAlign::Center})};
		for (auto &part : backfill)
			parts.push_back(part);
		parts.push_back(row({allowAi, expanded(cancel)}, {p.pt(8), CrossAlign::Center}));
		return column(std::move(parts), {p.pt(6)});
	}

	fact(UIIcon::Trophy, range.empty() ? std::string() : range + " \xC2\xB7 " + tr("[qm widening]"));
	fact(UIIcon::Online, region);
	fact(UIIcon::Map, mapsLine(queue));
	auto timer = column({caption(phaseText), label(clock, {FontRole::Title}), caption(usual)}, {p.pt(2)});
	auto summary = column({row({icon(uiIcon(queue.mode == "2v2" ? UIIcon::Users : UIIcon::Bolt), {20}),
								label(queueTitle(queue), {FontRole::Heading})},
							   {p.pt(6), CrossAlign::Center}),
						   column(std::move(facts), {p.pt(4)})},
						  {p.pt(6)});
	std::vector<Element> parts{row({width(p.pt(150), timer), expanded(summary)}, {p.pt(12), CrossAlign::Start}), divider()};
	for (auto &part : backfill)
		parts.push_back(part);
	std::vector<Element> controls{expanded(allowAi)};
	if (queue.rated && queue.aiBackfillSeconds)
		controls.insert(controls.begin() + 1, caption(tr("[qm ais are rated]")));
	controls.push_back(cancel);
	parts.push_back(row(std::move(controls), {p.pt(8), CrossAlign::Center}));
	CardOptions options;
	options.shadow = false;
	options.border = palette.accent;
	options.color = palette.field;
	return card(column(std::move(parts), {p.pt(8)}), options);
}

Element QuickMatchScreen::build(const Presentation &p)
{
	const bool phone = p.touch && p.compact();
	const bool searching = model.active();
	std::vector<Element> body;

	if (model.notice() != QuickMatch::Notice::None)
	{
		std::string text;
		switch (model.notice())
		{
		case QuickMatch::Notice::OpponentDeclined:
			text = model.noticeName().empty() ? tr("[qm opponent declined]")
											  : std::string(FormattableString(tr("[qm %0 did not accept]")).arg(model.noticeName()));
			break;
		case QuickMatch::Notice::StartFailed:
			text = tr("[qm start failed]");
			break;
		case QuickMatch::Notice::Declined:
			text = tr("[qm you declined]");
			break;
		case QuickMatch::Notice::TimedOut:
			text = tr("[qm you did not answer]");
			break;
		default:
			text = tr("[qm search ended]");
			break;
		}
		CardOptions toast;
		toast.color = frontendTheme().palette.ink;
		toast.shadow = false;
		body.push_back(card(row({expanded(paragraph(text, {FontRole::Body, false, TextAlign::Left, frontendTheme().palette.paper})),
								 button("qm/notice/dismiss", tr("[ok]"), [this] { model.dismissNotice(); invalidate(); })},
								{p.pt(8), CrossAlign::Center}),
							toast));
	}
	if (model.phase() == QuickMatch::Phase::Failed)
		body.push_back(row({expanded(paragraph(errorText(model.error(), model.cooldownUntil()))),
							button("qm/retry", tr("[qm try again]"), [this] { model.cancel(); invalidate(); })},
						   {p.pt(8), CrossAlign::Center}));

	if (!queuesLoaded)
		body.push_back(paragraph(loadError.empty() ? tr("[online connecting]") : loadError, {FontRole::Body, true}));
	else if (queues.empty())
		body.push_back(paragraph(tr("[qm no queues]"), {FontRole::Body, true}));

	Element thumb;
	if (!phone)
	{
		body.insert(body.begin(), label(tr("[qm quick match]"), {FontRole::Heading}));
		if (searching && model.queue())
		{
			std::vector<Element> others;
			for (const auto &queue : queues)
				if (queue.id != model.queue()->id)
					others.push_back(queueCard(queue, p, false));
			body.push_back(row({expanded(searchPanel(p, false), 2), expanded(column(std::move(others), {p.pt(8)}), 1)},
							   {p.pt(10), CrossAlign::Start}));
		}
		else
		{
			std::vector<Element> cards;
			for (const auto &queue : queues)
				cards.push_back(queueCard(queue, p, true));
			if (!cards.empty())
				body.push_back(wrap(std::move(cards), {p.pt(10), p.pt(260), 3}));
		}
	}
	else
	{
		// Phones: the queues (or the running search) form the thumb block.
		if (searching && model.queue())
			thumb = searchPanel(p, true);
		else
		{
			std::vector<Element> rows;
			for (const auto &queue : queues)
			{
				ButtonOptions options;
				options.primary = queue.rated;
				rows.push_back(row({expanded(column({label(queueTitle(queue)), caption(queueDetail(queue))}, {p.pt(2)})),
									button("qm/queue/" + queue.id + "/find", tr("[qm find match]"),
										   [this, id = queue.id] { find(id); }, options)},
								   {p.pt(8), CrossAlign::Center}));
			}
			rows.push_back(button("back", tr("[goto main menu]"), [this] { back(); }, {.shortcut = SDLK_ESCAPE}));
			thumb = column(std::move(rows), {p.pt(8)});
		}
		body.push_back(paragraph(tr(searching ? "[qm phone searching note]" : "[qm phone intro]"), {FontRole::Support, true}));
	}

	OnlinePanel panel;
	panel.title = tr("[qm online]");
	panel.subtitle = originHost(instance);
	if (!account.empty())
	{
		CardOptions chip;
		chip.shadow = false;
		chip.border = frontendTheme().palette.line;
		chip.color = frontendTheme().palette.field;
		chip.padding = p.pt(6);
		panel.headerRight = card(row({icon(uiIcon(UIIcon::Player), {20}), label(account)}, {p.pt(6), CrossAlign::Center}), chip);
	}
	panel.body = scroll("qm/body", column(std::move(body), {p.pt(10)}));
	panel.note = tr("[qm keeps running note]");
	panel.actions = {{"back", tr("[goto main menu]"), [this] { back(); }, false, SDLK_ESCAPE}};
	panel.thumbBlock = thumb;
	return onlinePanel(std::move(panel), p);
}

// ============================================================ match found

struct MatchFoundScreen::Download
{
	std::unique_ptr<Online::MapCache::Download> fetch;
};

MatchFoundScreen::MatchFoundScreen(QuickMatch &model) : model(model) {}
MatchFoundScreen::~MatchFoundScreen() = default;

void MatchFoundScreen::accept()
{
	model.respond(true);
	invalidate();
}

void MatchFoundScreen::decline()
{
	model.respond(false);
	invalidate();
}

void MatchFoundScreen::cancel()
{
	model.cancel();
	endExecute(CLOSED);
}

void MatchFoundScreen::onEscape()
{
	if (model.phase() == QuickMatch::Phase::Proposed && !model.answer())
		decline();
	else
		cancel();
}

void MatchFoundScreen::onTimer(Uint32)
{
	const auto phase = model.phase();
	if (phase != QuickMatch::Phase::Proposed && phase != QuickMatch::Phase::Starting &&
		phase != QuickMatch::Phase::Matched)
	{
		endExecute(presenter().deferred ? STARTED : CLOSED);
		return;
	}
	// Fetch the map as soon as the match is assigned, to show its preview.
	if (model.assignment() && mapHash.empty() && Online::servicesCreated())
	{
		mapHash = model.assignment()->mapHash();
		auto &services = Online::services();
		HttpFetch::Headers headers;
		if (!services.client.accessToken().empty())
			headers.emplace_back("Authorization", "Bearer " + services.client.accessToken());
		if (auto cached = services.maps.path(mapHash))
		{
			preview = std::make_unique<MapPreview>();
			preview->setMapThumbnail(*cached);
			previewReady = true;
		}
		else
		{
			download = std::make_unique<Download>();
			download->fetch = services.maps.fetch(services.client.origin(), mapHash, headers);
		}
	}
	if (download && download->fetch &&
		download->fetch->state() == Online::MapCache::Download::State::Done)
	{
		preview = std::make_unique<MapPreview>();
		preview->setMapThumbnail(download->fetch->path());
		previewReady = true;
		download.reset();
		invalidate();
	}
	const std::int64_t tenth = wallClockMs() / 250;
	if (model.revision() != seen || tenth != shownTenth)
	{
		seen = model.revision();
		shownTenth = tenth;
		invalidate();
	}
}

Element MatchFoundScreen::seatCard(const Online::ProposalSeat &seat, const Presentation &p, bool showAnswer)
{
	std::string detail = seat.rating ? std::to_string(*seat.rating) : std::string();
	if (!seat.human)
		detail = tr("[qm ai]") + (detail.empty() ? "" : " \xC2\xB7 " + detail);
	if (seat.provisional)
		detail += " \xC2\xB7 " + tr("[qm provisional]");
	if (showAnswer && seat.human)
	{
		const std::string answer = seat.response == "accepted"	 ? tr("[qm accepted]")
								   : seat.response == "declined" ? tr("[qm declined]")
																 : tr("[qm waiting]");
		detail += (detail.empty() ? "" : " \xC2\xB7 ") + answer;
	}
	std::vector<Element> name{label(seat.displayName, {FontRole::Heading})};
	if (!seat.human)
		name.push_back(badge(tr("[qm ai badge]"), frontendTheme().palette.muted));
	CardOptions options;
	options.shadow = false;
	options.border = frontendTheme().palette.line;
	options.color = frontendTheme().palette.field;
	options.padding = p.pt(8);
	return card(row({swatch(seatColor(seat.slot), 20),
					 expanded(column({row(std::move(name), {p.pt(6), CrossAlign::Center}), caption(detail)}, {p.pt(2)}))},
					{p.pt(8), CrossAlign::Center}),
				options);
}

Element MatchFoundScreen::build(const Presentation &p)
{
	const bool phone = p.touch && p.compact();
	const auto &proposal = model.proposal();
	if (!proposal)
		return center(paragraph(tr("[qm searching]")));
	const bool ranked = proposal->requiresAccept;
	const bool againstAi = proposal->ais > 0;
	const bool rated = proposal->rated.value_or(model.queue() && model.queue()->rated);
	std::string mode = model.queue() ? modeText(*model.queue()) : std::string();
	mode += " \xC2\xB7 " + (rated ? tr("[qm ranked]") : tr("[qm unrated]"));

	std::string titleText = ranked ? tr("[qm match found]") : againstAi ? tr("[qm playing an ai]") : tr("[qm match found]");
	std::string mapText = generatorTitle(proposal->generatorId);
	if (proposal->width && proposal->height)
		mapText += " \xC2\xB7 " + std::to_string(*proposal->width) + " \xC3\x97 " + std::to_string(*proposal->height);
	std::string regionText;
	if (!proposal->region.empty())
	{
		regionText = FormattableString(tr("[qm region %0]")).arg(proposal->region);
		for (const auto &r : model.regions())
			if (r.region == proposal->region)
				regionText = FormattableString(tr("[qm region %0]")).arg(r.region) + " \xC2\xB7 " +
							 ConnectionQuality::labelled(tr("[qm ping about]"),
														 ConnectionQuality::Metric::Ping, r.rttMs,
														 [](const char *key) { return tr(key); });
	}

	// Sides: own side first.
	std::vector<Element> sides;
	const auto *own = proposal->own();
	const int ownSide = own ? own->side : 0;
	std::vector<Element> mine, theirs;
	for (const auto &seat : proposal->seats)
		(seat.side == ownSide ? mine : theirs).push_back(seatCard(seat, p, ranked));
	auto sideColumn = [&](std::vector<Element> cards) { return expanded(column(std::move(cards), {p.pt(6)})); };
	Element versus = phone && !p.landscape()
						 ? row({sideColumn(std::move(mine)), label(tr("[qm vs]")), sideColumn(std::move(theirs))},
							   {p.pt(6), CrossAlign::Center})
						 : row({sideColumn(std::move(mine)), label(tr("[qm vs]")), sideColumn(std::move(theirs))},
							   {p.pt(8), CrossAlign::Center});

	std::vector<Element> info{label(mapText, {FontRole::Heading}), versus};
	if (!ranked && againstAi)
		for (const auto &seat : proposal->seats)
			if (!seat.human)
			{
				const std::string profile = aiProfile(seat.ai);
				if (!profile.empty())
					info.push_back(paragraph(profile, {FontRole::Support, true}));
				break;
			}
	if (!regionText.empty())
		info.push_back(caption(regionText));
	const int pictureSize = phone ? std::min(p.safe.w - p.pt(24), p.pt(previewReady ? 240 : 150)) : p.pt(170);
	Element picture = previewReady && preview ? mapPreview("found/map", *preview, p.points(pictureSize))
											  : previewPicture(nullptr, pictureSize);
	Element content = phone && !p.landscape()
						  ? column({center(picture), column(std::move(info), {p.pt(8)})}, {p.pt(10)})
						  : row({picture, expanded(column(std::move(info), {p.pt(8)}))}, {p.pt(12), CrossAlign::Start});

	std::vector<Element> intro;
	if (!ranked)
		intro.push_back(paragraph(againstAi ? tr(rated ? "[qm ai explanation rated]" : "[qm ai explanation]")
											: tr("[qm casual explanation]"),
								  {FontRole::Support}));

	// Bottom: countdown and answers.
	Element bottom;
	if (ranked)
	{
		const std::int64_t total = model.queue() && model.queue()->acceptSeconds > 0 ? model.queue()->acceptSeconds * 1000ll : 10000;
		const auto remaining = model.acceptInMs().value_or(0);
		const bool accepted = model.answer().value_or(false);
		ButtonOptions acceptOptions;
		acceptOptions.primary = true;
		acceptOptions.selected = accepted;
		acceptOptions.enabled = !model.answer().has_value();
		acceptOptions.shortcut = SDLK_RETURN;
		acceptOptions.icon = uiIcon(UIIcon::Check);
		ButtonOptions declineOptions;
		declineOptions.enabled = !model.answer().has_value();
		auto acceptButton = button("found/accept", accepted ? tr("[qm accepted button]") : tr("[qm accept]"),
								   [this] { accept(); }, acceptOptions);
		auto declineButton = button("found/decline", tr("[qm decline]"), [this] { decline(); }, declineOptions);
		const int ring = p.pt(phone ? 44 : 60);
		auto note = paragraph(tr(phone ? "[qm both must accept]" : "[qm accept explanation]"), {FontRole::Support, true});
		if (phone && p.landscape())
			bottom = column({align(Alignment::Right, countdownRing(remaining, total, ring)), acceptButton, declineButton}, {p.pt(6)});
		else if (phone)
			bottom = column({row({countdownRing(remaining, total, ring), expanded(note)}, {p.pt(8), CrossAlign::Center}),
							 row({expanded(declineButton, 2), expanded(acceptButton, 3)}, {p.pt(8)})},
							{p.pt(8)});
		else
			bottom = row({countdownRing(remaining, total, ring), expanded(note), width(p.pt(150), declineButton),
						  width(p.pt(170), acceptButton)},
						 {p.pt(10), CrossAlign::Center});
	}
	else
	{
		const auto remaining = model.startInMs().value_or(0);
		const std::string starting = model.assignment()
										 ? std::string(FormattableString(tr("[qm starting in %0]")).arg(int((remaining + 999) / 1000)))
										 : tr("[qm preparing match]");
		ButtonOptions cancelOptions;
		cancelOptions.shortcut = SDLK_ESCAPE;
		bottom = row({expanded(label(starting, {FontRole::Body, true})),
					  width(p.pt(phone ? 140 : 150), button("found/cancel", tr("[qm cancel]"), [this] { cancel(); }, cancelOptions))},
					 {p.pt(8), CrossAlign::Center});
	}

	std::vector<Element> heading{row({expanded(label(titleText, {FontRole::Heading})), caption(mode)}, {p.pt(8), CrossAlign::Center})};
	for (auto &e : intro)
		heading.push_back(e);
	if (phone)
	{
		CardOptions sheet;
		sheet.padding = p.pt(12);
		sheet.shadow = false;
		if (p.landscape())
			return card(row({expanded(scroll("found/body", column({column(std::move(heading), {p.pt(6)}), content}, {p.pt(8)})), 3),
							 expanded(align(Alignment::Bottom, bottom), 2)},
							{p.pt(10)}),
						sheet);
		return card(footer(scroll("found/body", column({column(std::move(heading), {p.pt(6)}), content}, {p.pt(10)})), bottom), sheet);
	}
	CardOptions dialog;
	dialog.padding = p.pt(18);
	return center(maxWidth(p.pt(720), card(column({column(std::move(heading), {p.pt(6)}), content, bottom}, {p.pt(14)}), dialog)));
}

// ============================================================== presenter

namespace QuickMatchPresenter
{
namespace
{
// To the connecting flow; without one in this build, say where the match is.
void handOff(const Online::MatchAssignment &match)
{
	if (Online::beginMatch(match) || !presenter().screens)
		return;
	(void)Online::takePendingMatch();
	presenter().screens->push(std::make_unique<MessageScreen>(
		FormattableString(tr("[qm match assigned %0 %1]")).arg(match.matchId).arg(match.relayUrl),
		std::vector<std::string>{tr("[ok]")}));
}
} // namespace

void detach(GAGGUI::ScreenStack &screens)
{
	auto &state = presenter();
	if (state.screens == &screens)
	{
		state.screens = nullptr;
		state.open = false;
	}
}

void attach(GAGGUI::ScreenStack &screens)
{
	auto &state = presenter();
	state.screens = &screens;
	if (state.hooked)
		return;
	state.hooked = true;
	auto &model = Online::quickMatch();
	model.setHandoff([](const Online::MatchAssignment &assignment) { presenter().deferred = assignment; });
	Online::addPumpHook(
		[]
		{
			auto &state = presenter();
			auto &model = Online::quickMatch();
			if (!state.screens)
				return;
			if (!state.open && state.deferred)
			{
				// Handed off while no prompt was open (the player was elsewhere).
				auto match = std::move(*state.deferred);
				state.deferred.reset();
				handOff(match);
				return;
			}
			const auto phase = model.phase();
			const bool found = phase == QuickMatch::Phase::Proposed || phase == QuickMatch::Phase::Starting ||
							   phase == QuickMatch::Phase::Matched;
			if (!found || state.open)
				return;
			state.open = true;
			state.screens->push(std::make_unique<MatchFoundScreen>(model),
								[](GAGGUI::Screen &, int)
								{
									auto &state = presenter();
									state.open = false;
									if (state.deferred)
									{
										auto match = std::move(*state.deferred);
										state.deferred.reset();
										handOff(match);
									}
								});
		});
}
} // namespace QuickMatchPresenter
