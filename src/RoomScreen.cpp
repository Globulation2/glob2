// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
#include "RoomScreen.h"
#include "AINames.h"
#include "CustomGameOtherOptions.h"
#include "CustomGameScreen.h"
#include "CustomGameSetup.h"
#include "Engine.h"
#include "GUIMapPreview.h"
#include "GameLoadScreen.h"
#include "GameSessionScreen.h"
#include "GlobalContainer.h"
#include "MatchStartScreen.h"
#include "OnlineHandoff.h"
#include "PlatformRoom.h"
#include "MessageScreen.h"
#include "Order.h"
#include "gui/ThumbSide.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <SDL.h>

using namespace Glob2UI;

namespace
{
constexpr std::size_t CHAT_HISTORY = 200;
// The newest chat line shows above the phone tab bar for this long.
constexpr Uint32 CHAT_TOAST_MS = 5000;
// How long a copy button says "Copied".
constexpr Uint32 COPIED_MS = 2000;

std::string formatted(const char *key, const std::string &value)
{
	return GAGCore::FormattableString(tr(key)).arg(value);
}

GAGCore::Color colorOf(const std::optional<std::array<std::uint8_t, 3>> &color)
{
	if (!color)
		return GAGCore::Color(160, 172, 149);
	return GAGCore::Color((*color)[0], (*color)[1], (*color)[2]);
}

// The rules a room plays with, as "label: value" lines, in the custom-game order.
std::vector<std::pair<std::string, std::string>> ruleLines(const CustomGameSetup &s)
{
	auto onOff = [](bool on) { return tr(on ? "[room rule on]" : "[room rule off]"); };
	auto level = [](int value) { return value == 0 ? tr("[room rule off]") : std::to_string(value); };
	std::vector<std::pair<std::string, std::string>> lines;
	auto add = [&](int index, std::string value) {
		lines.push_back({tr(std::string("[") + CustomGameSetup::ruleDefinitions[std::size_t(index)].label + "]"), std::move(value)});
	};
	add(0, s.prestige ? tr("[room rule prestige]") : tr("[room rule conquest]"));
	add(17, s.suddenDeathMinutes ? GAGCore::FormattableString(tr("[results minutes %0]")).arg(s.suddenDeathMinutes) : tr("[room rule off]"));
	add(1, s.revealed ? tr("[room rule revealed]") : tr("[room rule hidden]"));
	add(2, s.locked ? tr("[room rule fixed]") : tr("[room rule free]"));
	add(5, onOff(s.noResourceGrowth));
	add(6, level(s.resourceScarcity));
	add(7, onOff(s.instantConstruction));
	add(8, level(s.stockpileStart));
	add(9, onOff(s.noHunger));
	add(10, onOff(s.unitUpgradesDisabled));
	add(11, level(s.glassCannonLevel));
	add(12, onOff(s.unitsFearless));
	add(13, onOff(s.permadeathDisabled));
	add(14, onOff(s.peacefulMode));
	add(15, level(s.buildingHpLevel));
	return lines;
}
} // namespace

RoomScreen::RoomScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<RoomBackend> room)
	: screens(screens), room(std::move(room)), preview(std::make_unique<MapPreview>())
{
	// "Use in a room" from the map browser lands in the open online room.
	if (this->room->kind() == RoomBackend::Kind::Online)
		Online::setRoomMapHandler([this](const Online::RoomMapChoice &choice) {
			pendingCatalogMap = std::make_pair(choice.hash, choice.mapId);
		});
	// Pick up what the backend already queued (preview fixtures, early chat).
	onTimer(0);
}

RoomScreen::~RoomScreen()
{
	if (room->kind() == RoomBackend::Kind::Online)
		Online::setRoomMapHandler({});
	GAGCore::ApplicationHost::roomReady(false);
}

void RoomScreen::selectTab(int tab)
{
	currentTab = std::clamp(tab, 0, 3);
	if (currentTab == ChatTab)
		unread = 0;
	selectedSeat = -1;
	host().closePopup();
	invalidate();
}

void RoomScreen::openSeat(int seat)
{
	selectedSeat = selectedSeat == seat ? -1 : seat;
	invalidate();
}

void RoomScreen::sendChat(const std::string &text)
{
	if (text.empty())
		return;
	room->sendChat(text);
	chatDraft.clear();
	invalidate();
}

void RoomScreen::onEscape()
{
	if (selectedSeat >= 0)
	{
		openSeat(-1);
		return;
	}
	leave();
}

void RoomScreen::leave()
{
	room->leave();
	finish(RoomBackend::Cancelled, {});
}

void RoomScreen::finish(int code, const std::string &message)
{
	if (closing)
		return;
	closing = true;
	if (message.empty())
	{
		endExecute(code);
		return;
	}
	// A compact notice with a heading; online rooms go back to the Online hub.
	const bool online = room->kind() == RoomBackend::Kind::Online;
	const char *title = code == RoomBackend::Kicked ? "[room removed title]" : "[room closed title]";
	screens.push(std::make_unique<MessageScreen>(tr(title), message, std::vector<std::string>{tr(online ? "[room back to online]" : "[ok]")}),
				 [this, code](GAGGUI::Screen &, int) { endExecute(code); });
}

void RoomScreen::copy(const std::string &key, const std::string &text)
{
	if (text.empty())
		return;
	copyFailed = !GAGCore::ApplicationHost::copyText(text);
	copiedKey = key;
	copiedAt = now ? now : 1;
	invalidate();
}

std::string RoomScreen::copyLabel(const std::string &key, const std::string &label) const
{
	// Two seconds of "Copied" (or why not) where the button's word was.
	if (key != copiedKey || !copiedAt || now - copiedAt > COPIED_MS)
		return label;
	return tr(copyFailed ? "[room copy failed]" : "[room copied]");
}

void RoomScreen::handle(const RoomBackend::Event &event)
{
	switch (event.kind)
	{
	case RoomBackend::Event::Changed:
		break;
	case RoomBackend::Event::Chat:
	{
		ChatLine line{event.author, event.text, event.system};
		// LAN and legacy backends send "name: text" in one string.
		if (line.author.empty() && !line.system)
		{
			const auto colon = line.text.find(": ");
			if (colon != std::string::npos && colon < 40)
			{
				line.author = line.text.substr(0, colon);
				line.text = line.text.substr(colon + 2);
			}
			else
				line.system = true;
		}
		chat.push_back(std::move(line));
		while (chat.size() > CHAT_HISTORY)
			chat.pop_front();
		if (currentTab != ChatTab)
			++unread;
		lastChatAt = now;
		break;
	}
	case RoomBackend::Event::Launch:
		launch();
		break;
	case RoomBackend::Event::Message:
		screens.push(std::make_unique<MessageScreen>(event.text, std::vector<std::string>{tr("[ok]")}));
		break;
	case RoomBackend::Event::Finished:
		finish(event.code, event.text);
		break;
	}
	invalidate();
}

void RoomScreen::onTimer(Uint32 tick)
{
	now = tick;
	room->update();
	while (auto event = room->takeEvent())
		handle(*event);
	if (pendingCatalogMap && room->lobbyReady())
	{
		if (auto *online = dynamic_cast<Online::PlatformRoom *>(room.get()); online && online->canEditSetup())
			online->useCatalogMap(pendingCatalogMap->first, pendingCatalogMap->second);
		pendingCatalogMap.reset();
	}
	if (auto file = room->mapFile(); file && *file != previewFile)
	{
		previewFile = *file;
		preview->setMapThumbnail(previewFile);
		invalidate();
	}
	if (copiedAt && tick - copiedAt > COPIED_MS && tick - copiedAt < COPIED_MS + 200)
		invalidate();
	// The chat toast on phones fades after a few seconds.
	if (lastChatAt && tick - lastChatAt > CHAT_TOAST_MS && tick - lastChatAt < CHAT_TOAST_MS + 200)
		invalidate();
}

void RoomScreen::launch()
{
	if (launched)
		return;
	GAGCore::ApplicationHost::roomReady(false);
	if (auto match = room->takeMatch())
	{
		launched = true;
		screens.push(std::make_unique<MatchStartScreen>(screens, match), [this](GAGGUI::Screen &, int result) {
			launched = false;
			room->gameEnded(result == QUIT_APPLICATION);
			if (result == QUIT_APPLICATION)
				endExecute(QUIT_APPLICATION);
			invalidate();
		});
		return;
	}
	// LAN and legacy rooms initialize the engine themselves.
	launched = true;
	screens.push(std::make_unique<GameLoadScreen>([room = room](Engine &engine) { return room->initGame(engine); }),
				 [this](GAGGUI::Screen &load, int result) {
					 if (result != 1)
					 {
						 launched = false;
						 room->gameStarted(false);
						 if (result == 2)
							 screens.push(std::make_unique<MessageScreen>(static_cast<GameLoadScreen &>(load).failureMessage(),
																		  std::vector<std::string>{tr("[ok]")}));
						 return;
					 }
					 auto engine = static_cast<GameLoadScreen &>(load).takeEngine();
					 room->gameStarted(true);
					 screens.push(std::make_unique<GameSessionScreen>(screens, std::move(engine)),
								  [this](GAGGUI::Screen &, int result) {
									  launched = false;
									  room->gameEnded(result == QUIT_APPLICATION);
									  if (result == QUIT_APPLICATION)
										  endExecute(QUIT_APPLICATION);
								  });
				 });
}

void RoomScreen::editSetup(int customGameTab)
{
	CustomGameSetup draft;
	if (!room->canEditSetup() || !room->setupDraft(draft))
		return;
	auto editor = std::make_unique<CustomGameScreen>(screens);
	editor->useForRoom(draft, customGameTab);
	screens.push(std::move(editor), [this, customGameTab](GAGGUI::Screen &screen, int result) {
		if (result != CustomGameScreen::OK)
			return;
		auto &editor = static_cast<CustomGameScreen &>(screen);
		auto edited = editor.draft();
		auto *online = dynamic_cast<Online::PlatformRoom *>(room.get());
		// A premade (or own) map is uploaded for the room; a random one is generated
		// on the server from its descriptor.
		if (!edited.random)
		{
			if (online && !edited.premadeMap.empty())
				online->usePremadeMap(edited.premadeMap, editor.getMapHeader().getMapName(), edited);
			invalidate();
			return;
		}
		// From "Change map…" a random map replaces a premade one; from "Change rules…"
		// the room keeps its map.
		if (online && customGameTab == 0)
			online->useGeneratedMap(edited);
		else
			room->applySetup(edited);
		invalidate();
	});
}

std::string RoomScreen::chatText() const
{
	std::string text;
	for (const auto &line : chat)
	{
		if (!text.empty())
			text += "\n";
		text += line.system || line.author.empty() ? line.text : line.author + ": " + line.text;
	}
	return text;
}

Element RoomScreen::header(const Presentation &p, bool phone)
{
	const auto kind = room->kind();
	std::string badge;
	if (kind == RoomBackend::Kind::Lan)
		badge = tr("[room lan badge]");
	else if (kind == RoomBackend::Kind::Online)
		badge = tr(room->listed() ? "[room public]" : "[room invite only]");
	if (!room->isHost() && !room->hostName().empty())
		badge += (badge.empty() ? "" : " · ") + formatted("[room host %0]", room->hostName());
	std::string name = room->roomName();
	if (name.empty())
		name = tr("[room connecting]");
	if (phone)
		return column({heading(name), caption(badge), caption(room->setupSummary())}, {p.pt(2)});
	std::vector<Element> cells{expanded(title(name))};
	if (room->canChangeVisibility())
	{
		cells.push_back(caption(tr("[room who can find it]")));
		cells.push_back(segments("visibility", {tr("[room public]"), tr("[room invite only]")}, room->listed() ? 0 : 1,
								 [this](int v) { room->setListed(v == 0); invalidate(); }));
	}
	else
		cells.push_back(caption(badge));
	return row(std::move(cells), {p.pt(8), CrossAlign::Center});
}

Element RoomScreen::tabs(const Presentation &p)
{
	CustomGameSetup draft;
	const bool haveDraft = room->setupDraft(draft);
	int people = 0, ais = 0, open = 0;
	for (const auto &slot : room->slots())
		(slot.ai ? ais : slot.open || slot.locked ? open : people)++;
	people += int(room->unseatedMembers().size());
	const std::vector<std::string> titles = {tr("[Map]"), tr("[Players & Teams]"), tr("[Game Rules]")};
	const std::vector<std::string> details = {room->mapName(),
											  GAGCore::FormattableString(tr("[room seats summary %0 %1 %2]")).arg(people).arg(ais).arg(open),
											  haveDraft ? tr("[" + draft.ruleset + "]") : room->experimentsLabel()};
	std::vector<Element> row;
	for (int i = 0; i < 3; ++i)
	{
		ButtonOptions options;
		options.selected = currentTab == i;
		options.role = FontRole::Heading;
		row.push_back(expanded(column({button("tab/" + std::to_string(i), titles[std::size_t(i)], [this, i] { selectTab(i); }, options),
									   caption(details[std::size_t(i)])},
									  {p.pt(2)})));
	}
	return Glob2UI::row(std::move(row), {p.pt(6)});
}

Element RoomScreen::seatControls(const RoomBackend::Slot &slot, const Presentation &p, bool phone)
{
	const std::string id = "seat/" + std::to_string(slot.index);
	const int index = slot.index;
	std::vector<Element> controls;
	if (room->canSetOccupant(slot))
	{
		std::vector<std::string> options{tr("[room open for a person]"), tr("[room closed seat]")};
		std::vector<std::string> ids{"@open", "@closed"};
		for (int ai : AINames::selectionOrder())
		{
			if (ai == AI::NONE || ai >= AI::JAVASCRIPT)
				continue;
			options.push_back(formatted("[room ai %0]", AINames::getAIText(ai)));
			ids.push_back(AINames::getCLIName(ai));
		}
		int selected = slot.locked ? 1 : 0;
		if (slot.ai)
			for (std::size_t i = 2; i < ids.size(); ++i)
				if (ids[i] == slot.aiId)
					selected = int(i);
		if (!slot.ai && !slot.open && !slot.locked)
		{
			// A person: the host can only remove them (×) or open the seat.
			options.insert(options.begin(), tr("[room person]"));
			ids.insert(ids.begin(), "@person");
			selected = 0;
		}
		controls.push_back(width(p.pt(phone ? 220 : 190), choice(id + "/controller", options, selected, [this, index, ids](int v) {
										 const std::string &picked = ids[std::size_t(v)];
										 if (picked == "@person")
											 return;
										 if (picked == "@open")
											 room->setOccupant(index, RoomBackend::Occupant::Open);
										 else if (picked == "@closed")
											 room->setOccupant(index, RoomBackend::Occupant::Closed);
										 else
											 room->setOccupant(index, RoomBackend::Occupant::AI, picked);
										 invalidate();
									 })));
	}
	else if (!phone)
		controls.push_back(width(p.pt(190), caption(slot.ai ? formatted("[room ai %0]", slot.name)
															: slot.open ? tr("[room open for a person]")
															: slot.locked ? tr("[room closed seat]")
																		  : tr("[room person]"))));
	const int teams = std::max(1, room->teamChoices());
	std::vector<std::string> teamNames;
	for (int t = 0; t < teams; ++t)
		teamNames.push_back(GAGCore::FormattableString(tr("[room team %0]")).arg(t + 1));
	ChoiceOptions teamOptions;
	teamOptions.controlEnabled = room->canChangeTeam(slot);
	controls.push_back(width(p.pt(phone ? 140 : 110), choice(id + "/team", teamNames, std::clamp(slot.team, 0, teams - 1),
															 [this, index](int team) { room->changeTeam(index, team); invalidate(); }, teamOptions)));
	if (room->canTakeSeat(slot) && !room->isHost())
		controls.push_back(button(id + "/take", tr("[room take seat]"), [this, index] { room->takeSeat(index); }));
	if (room->canKick(slot))
	{
		ButtonOptions kick;
		kick.tooltip = tr("[room remove from room]");
		kick.accessibleLabel = kick.tooltip;
		kick.icon = uiIcon(UIIcon::Close);
		controls.push_back(width(p.pt(p.touch ? 48 : 34), button(id + "/kick", "", [this, index] { room->kick(index); }, kick)));
	}
	return phone ? wrap(std::move(controls), {p.pt(6), p.pt(110)}) : row(std::move(controls), {p.pt(6), CrossAlign::Center});
}

Element RoomScreen::seats(const Presentation &p, bool phone)
{
	const auto palette = theme().palette;
	std::vector<Element> rows;
	if (!phone && room->isHost() && room->kind() == RoomBackend::Kind::Online)
	{
		CustomGameSetup draft;
		room->setupDraft(draft);
		const std::string format = draft.format;
		rows.push_back(segments("preset", {tr("[FFA]"), tr("[2 vs 2]"), tr("[room humans vs ai]")},
								format == "FFA" ? 0 : format == "2 vs 2" ? 1 : -1, [this](int preset) {
									CustomGameSetup s;
									if (!room->setupDraft(s))
										return;
									if (preset == 2)
									{
										// People on one side, AIs on the other.
										const auto slots = room->slots();
										for (const auto &slot : slots)
											s.colonies[std::size_t(slot.index)].alliance = slot.ai ? 1 : 0;
										s.format = "Humans vs AI";
									}
									else
										s.presetTeams(preset);
									room->applySetup(s);
								}));
	}
	for (const auto &slot : room->slots())
	{
		const std::string id = "seat/" + std::to_string(slot.index);
		// Two lines that ellipsize instead of overflowing: name and badges, then state.
		std::string rest;
		auto add = [&rest](const std::string &part) {
			if (!part.empty())
				rest += (rest.empty() ? "" : " · ") + part;
		};
		Element readyLabel;
		if (!slot.ai && !slot.open && !slot.locked)
		{
			TextOptions readyText;
			readyText.role = FontRole::Support;
			readyText.color = slot.ready ? palette.success : palette.warning;
			readyLabel = label(slot.ready ? tr("[room ready]") : tr("[room not ready]"), readyText);
			if (slot.progress >= 0 && slot.progress < 100)
				add(formatted("[room downloading %0]", std::to_string(slot.progress)));
			if (phone)
				add(GAGCore::FormattableString(tr("[room team %0]")).arg(slot.team + 1));
			if (slot.latencyMs >= 0)
				add(std::to_string(slot.latencyMs) + " ms");
			if (slot.host)
				add(tr("[room host]"));
		}
		add(slot.detail);
		std::string nameText = slot.name;
		if (slot.guest)
			nameText += " · " + tr("[room guest]");
		if (slot.ai)
			nameText += " · " + tr("[match ai]");
		if (slot.local)
			nameText += " (" + tr("[room you]") + ")";
		std::vector<Element> nameLine;
		if (slot.host)
			nameLine.push_back(icon(uiIcon(UIIcon::Crown), {14, palette.accent}));
		nameLine.push_back(expanded(label(nameText, {FontRole::Body, slot.open || slot.locked})));
		auto who = column({row(std::move(nameLine), {p.pt(6), CrossAlign::Center}),
						   row({readyLabel, expanded(caption(rest))}, {p.pt(6), CrossAlign::Center})},
						  {p.pt(2)});
		auto number = sized({p.pt(26), p.pt(26)}, canvas("", {p.pt(26), p.pt(26)}, [color = colorOf(room->seatColor(slot)), n = slot.index + 1](Canvas &c, Rect r, const Frame &frame) {
								 c.fillRounded(r, 4, color);
								 const std::string text = std::to_string(n);
								 const int w = c.measurer().width(FontRole::Body, text);
								 c.text({r.x + (r.w - w) / 2, r.y + (r.h - c.measurer().lineHeight(FontRole::Body)) / 2}, FontRole::Body, text, GAGCore::Color(20, 20, 20));
							 }));
		if (phone)
		{
			const int index = slot.index;
			ButtonOptions more;
			more.icon = uiIcon(UIIcon::More);
			more.accessibleLabel = tr("[room seat options]");
			more.tooltip = more.accessibleLabel;
			std::vector<Element> line{number, expanded(who)};
			const bool editable = room->canSetOccupant(slot) || room->canChangeTeam(slot) || room->canKick(slot) || room->canTakeSeat(slot);
			if (slot.open && room->canTakeSeat(slot) && !room->canSetOccupant(slot))
				line.push_back(button(id + "/take", tr("[room take seat]"), [this, index] { room->takeSeat(index); }));
			else if (slot.open && room->canSetOccupant(slot))
				line.push_back(button(id + "/add-ai", tr("[Add AI]"), [this, index] { room->setOccupant(index, RoomBackend::Occupant::AI, "nicowar"); }));
			if (editable)
				line.push_back(width(p.pt(48), button(id + "/more", "", [this, index] { openSeat(index); }, more)));
			std::vector<Element> card{row(std::move(line), {p.pt(8), CrossAlign::Center})};
			if (selectedSeat == slot.index)
				card.push_back(seatControls(slot, p, true));
			CardOptions options;
			options.color = slot.local ? palette.selected.applyAlpha(80) : palette.field;
			options.padding = p.pt(8);
			options.shadow = false;
			options.border = palette.line;
			rows.push_back(Glob2UI::card(column(std::move(card), {p.pt(6)}), options));
		}
		else
			rows.push_back(row({number, expanded(who), seatControls(slot, p, false)}, {p.pt(10), CrossAlign::Center}));
		if (!phone)
			rows.push_back(divider());
	}
	// Everyone in the room is listed, also those without a seat (a full room, or a
	// member who left their seat), so nobody is invisible to the others.
	if (const auto unseated = room->unseatedMembers(); !unseated.empty())
	{
		rows.push_back(label(tr("[room not seated]"), {FontRole::Support, true}));
		for (const auto &text : unseated)
		{
			rows.push_back(row({sized({p.pt(26), p.pt(26)}, icon(uiIcon(UIIcon::Users), {16, palette.muted})), expanded(label(text, {FontRole::Body, true}))},
							   {p.pt(phone ? 8 : 10), CrossAlign::Center}));
		}
		if (const auto why = room->readyBlocker(); !why.empty())
			rows.push_back(paragraph(why, {FontRole::Support, true}));
	}
	if (rows.empty())
		rows.push_back(paragraph(tr("[room connecting]"), {FontRole::Body, true}));
	if (!phone && room->kind() == RoomBackend::Kind::Online)
		rows.push_back(paragraph(tr("[room seat hint]"), {FontRole::Support, true}));
	return column(std::move(rows), {p.pt(phone ? 6 : 6)});
}

Element RoomScreen::mapPanel(const Presentation &p, bool phone)
{
	std::vector<Element> facts;
	CustomGameSetup draft;
	const bool haveDraft = room->setupDraft(draft);
	facts.push_back(heading(room->mapName()));
	auto *online = dynamic_cast<Online::PlatformRoom *>(room.get());
	std::string status = room->mapStatus();
	if (online && online->uploadingMap())
		status = tr("[room uploading map]");
	if (!status.empty())
		facts.push_back(row({icon(uiIcon(UIIcon::Spinner), {16, theme().palette.muted}), expanded(paragraph(status, {FontRole::Support, true}))}, {p.pt(6), CrossAlign::Center}));
	// Size and seed describe a generated map; a premade map's file is the map.
	const bool generated = !online || online->generatedMap();
	if (haveDraft && generated)
	{
		facts.push_back(field(tr("[room colonies]"), label(std::to_string(draft.capacity))));
		const auto &request = draft.generator;
		facts.push_back(field(tr("[room size]"), label(std::to_string(1 << request.wDec) + " × " + std::to_string(1 << request.hDec))));
		if (request.seed)
			facts.push_back(field(tr("[room seed]"), label(std::to_string(request.seed))));
	}
	else if (room->teamCount() > 0)
		facts.push_back(field(tr("[room colonies]"), label(std::to_string(room->teamCount()))));
	if (online)
		facts.push_back(paragraph(tr(generated ? "[room generated on server]" : "[room premade map shared]"), {FontRole::Support, true}));
	if (room->canEditSetup())
		facts.push_back(button("map/change", tr("[room change map]"), [this] { editSetup(0); }, {.icon = uiIcon(UIIcon::Map)}));
	else if (!room->isHost())
		facts.push_back(caption(tr("[room set by host]")));
	// The preview appears once the map is on this device.
	Element map = previewFile.empty() ? nullptr : mapPreview("map/preview", *preview, phone ? 200 : 240);
	if (!map)
		return column(std::move(facts), {p.pt(6)});
	if (phone || p.compact())
		return column({center(map), column(std::move(facts), {p.pt(6)})}, {p.pt(10)});
	return row({map, expanded(column(std::move(facts), {p.pt(6)}))}, {p.pt(16), CrossAlign::Start});
}

Element RoomScreen::rulesPanel(const Presentation &p)
{
	std::vector<Element> lines;
	CustomGameSetup draft;
	if (room->setupDraft(draft))
	{
		lines.push_back(heading(tr("[" + draft.ruleset + "]")));
		for (const auto &[name, value] : ruleLines(draft))
			lines.push_back(row({expanded(label(name)), label(value)}, {p.pt(8), CrossAlign::Center}));
	}
	if (const auto experiments = room->experimentsLabel(); !experiments.empty())
		lines.push_back(paragraph(tr("[Experiments set by the host]") + ": " + experiments, {FontRole::Support}));
	if (room->canEditSetup())
		lines.push_back(button("rules/change", tr("[room change rules]"), [this] { editSetup(2); }, {.icon = uiIcon(UIIcon::Rules)}));
	else if (room->optionsHeader())
		lines.push_back(button("options", tr("[Other Options]"), [this] {
			GameHeader *header = room->optionsHeader();
			if (!header)
				return;
			screens.push(std::make_unique<CustomGameOtherOptions>(*header, *room->optionsMap(), room->optionsReadOnly()),
						 [this](GAGGUI::Screen &, int) { room->optionsChanged(); });
		}));
	else if (!room->isHost())
		lines.push_back(caption(tr("[room set by host]")));
	return column(std::move(lines), {p.pt(6)});
}

Element RoomScreen::invite(const Presentation &p, bool phone)
{
	std::vector<Element> parts;
	if (room->kind() == RoomBackend::Kind::Online)
	{
		const std::string link = room->inviteLink();
		const std::string code = room->inviteCode();
		parts.push_back(heading(tr("[room invite]")));
		parts.push_back(row({expanded(label(link.empty() ? tr("[room connecting]") : link)),
							 button("invite/copy", copyLabel("invite/copy", tr("[room copy]")), [this, link] { copy("invite/copy", link); }, {.enabled = !link.empty(), .icon = uiIcon(copiedKey == "invite/copy" && copiedAt && now - copiedAt <= COPIED_MS && !copyFailed ? UIIcon::Check : UIIcon::Copy), .iconSize = 16})},
							{p.pt(6), CrossAlign::Center}));
		const std::string share = formatted("[room share text %0]", link);
		parts.push_back(row({expanded(paragraph(formatted("[room invite code %0]", code), {FontRole::Support, true})),
							 button("invite/share", copyLabel("invite/share", tr("[room share]")), [this, share] { copy("invite/share", share); }, {.enabled = !link.empty(), .icon = uiIcon(UIIcon::Share), .iconSize = 16})},
							{p.pt(6), CrossAlign::Center}));
	}
	else
	{
		parts.push_back(heading(tr("[room joining on network]")));
		parts.push_back(paragraph(formatted("[room lan list hint %0]", room->roomName()), {FontRole::Support, true}));
		if (const auto address = room->localAddress(); !address.empty())
			parts.push_back(row({expanded(label(tr("[room address]") + "  " + address)),
								 button("invite/address", copyLabel("invite/address", tr("[room copy]")), [this, address] { copy("invite/address", address); })},
								{p.pt(6), CrossAlign::Center}));
		if (const auto pairing = room->shareText(); !pairing.empty())
			parts.push_back(row({expanded(paragraph(tr("[room browser pairing]") + "  " + pairing, {FontRole::Support})),
								 button("invite/pairing", copyLabel("invite/pairing", tr("[room copy]")), [this, pairing] { copy("invite/pairing", pairing); })},
								{p.pt(6), CrossAlign::Center}));
	}
	CardOptions options;
	options.color = theme().palette.field;
	options.shadow = false;
	options.border = theme().palette.line;
	options.padding = p.pt(10);
	return card(column(std::move(parts), {p.pt(6)}), options);
}

Element RoomScreen::chatPanel(const Presentation &p, bool phone)
{
	TextFieldOptions chatOptions;
	chatOptions.maxLength = ORDER_TEXT_MESSAGE_MAX_LEN;
	chatOptions.placeholder = tr("[room say something]");
	chatOptions.submit = [this](const std::string &) { sendChat(chatDraft); };
	ButtonOptions send;
	send.icon = uiIcon(UIIcon::Send);
	send.accessibleLabel = tr("[room send]");
	send.tooltip = send.accessibleLabel;
	auto input = row({expanded(textField("chat/input", chatDraft, [this](const std::string &v) { chatDraft = v; }, chatOptions)),
					  width(p.pt(p.touch ? 48 : 34), button("chat/send", "", [this] { sendChat(chatDraft); }, send))},
					 {p.pt(6), CrossAlign::Center});
	return column({phone ? nullptr : heading(tr("[room chat]")), expanded(textEditor("chat/log", chatText(), {}, {true, 8, false, true})), input}, {p.pt(6)});
}

Element RoomScreen::primaryActions(const Presentation &p, bool phone)
{
	const bool host = room->isHost();
	std::vector<Element> buttons;
	ButtonOptions leaveOptions;
	leaveOptions.shortcut = SDLK_ESCAPE;
	if (phone)
	{
		leaveOptions.icon = uiIcon(UIIcon::Leave);
		leaveOptions.accessibleLabel = tr("[room leave]");
		leaveOptions.tooltip = leaveOptions.accessibleLabel;
	}
	auto leaveButton = phone ? width(p.pt(52), button("cancel", "", [this] { leave(); }, leaveOptions))
							 : button("cancel", tr("[room leave]"), [this] { leave(); }, leaveOptions);
	ButtonOptions primary;
	primary.primary = true;
	primary.shortcut = SDLK_RETURN;
	Element main;
	if (host)
	{
		primary.enabled = room->canStart();
		primary.icon = uiIcon(UIIcon::Start);
		main = button(primary.enabled ? "start" : "start/waiting", tr("[Start]"), [this] { room->start(); invalidate(); }, primary);
		GAGCore::ApplicationHost::roomReady(primary.enabled);
	}
	else
	{
		// The tick is the icon; the label stays the plain word (no "✓ ✓ Ready").
		primary.selected = room->localReady();
		primary.icon = room->localReady() ? uiIcon(UIIcon::Check) : IconRef();
		const std::string blocker = room->readyBlocker();
		primary.enabled = room->lobbyReady() && !room->starting() && blocker.empty();
		if (!blocker.empty())
		{
			primary.tooltip = blocker;
			primary.accessibleLabel = tr("[room ready?]") + ". " + blocker;
		}
		main = button(blocker.empty() ? "ready" : "ready/blocked", tr("[room ready?]"), [this] { room->setReady(!room->localReady()); invalidate(); }, primary);
	}
	if (phone)
	{
		std::vector<Element> line;
		if (room->kind() == RoomBackend::Kind::Online)
			line.push_back(expanded(button("invite/copy-phone", copyLabel("invite/copy-phone", tr("[room invite]")), [this, link = room->inviteLink()] { copy("invite/copy-phone", link); }, {.icon = uiIcon(UIIcon::Share)})));
		line.push_back(expanded(main));
		line.insert(line.begin(), leaveButton);
		if (ThumbSide::left())
			std::reverse(line.begin(), line.end());
		return row(std::move(line), {p.pt(8), CrossAlign::Center});
	}
	return row({width(p.pt(150), leaveButton), width(p.pt(150), main)}, {p.pt(8), CrossAlign::Center});
}

Element RoomScreen::build(const Presentation &p)
{
	// Phones, and tablets in portrait, use the bar at the thumb.
	const bool phone = p.compact() || (p.touch && (p.shortLandscape() || p.safe.w < p.pt(820)));
	const std::string waiting = room->waitingFor();
	const auto palette = theme().palette;
	TextOptions waitingText;
	waitingText.role = FontRole::Support;
	waitingText.color = palette.warning;
	if (phone)
	{
		const int tab = currentTab == ChatTab ? ChatTab : currentTab;
		Element body;
		if (tab == PlayersTab)
		{
			std::vector<Element> parts{seats(p, true)};
			// The map in one line under the seats; tap to see it.
			ButtonOptions mapRow;
			mapRow.alignLeft = true;
			mapRow.icon = uiIcon(UIIcon::Map);
			parts.push_back(button("map/summary", room->mapName() + "  ›", [this] { selectTab(MapTab); }, mapRow));
			if (room->kind() != RoomBackend::Kind::Online)
				parts.push_back(invite(p, true));
			body = scroll("room/seats", column(std::move(parts), {p.pt(8)}));
		}
		else if (tab == MapTab)
			body = scroll("room/map", mapPanel(p, true));
		else if (tab == RulesTab)
			body = scroll("room/rules", rulesPanel(p));
		else
			body = chatPanel(p, true);
		std::vector<Element> bottom;
		if (tab != ChatTab && !chat.empty() && lastChatAt && now - lastChatAt < CHAT_TOAST_MS && !chat.back().system)
			bottom.push_back(label(chat.back().author + ": " + chat.back().text, {FontRole::Support}));
		if (!waiting.empty())
			bottom.push_back(label(waiting, waitingText));
		// Seats · Map · Rules · Chat at the thumb.
		struct Item
		{
			int tab;
			const char *key;
			UIIcon glyph;
		};
		const Item items[] = {{PlayersTab, "[room seats]", UIIcon::Users}, {MapTab, "[Map]", UIIcon::Map}, {RulesTab, "[room rules]", UIIcon::Rules}, {ChatTab, "[room chat]", UIIcon::Chat}};
		std::vector<Element> bar;
		for (const auto &item : items)
		{
			ButtonOptions options;
			options.selected = currentTab == item.tab;
			// Icons beside the words only where the bar has room for both.
			if (p.safe.w >= p.pt(480))
				options.icon = uiIcon(item.glyph);
			else
				options.role = FontRole::Support;
			std::string text = tr(item.key);
			if (item.tab == ChatTab && unread > 0)
				text += " " + std::to_string(unread);
			const int target = item.tab;
			bar.push_back(expanded(button("bar/" + std::to_string(item.tab), text, [this, target] { selectTab(target); }, options)));
		}
		bottom.push_back(row(std::move(bar), {p.pt(4)}));
		bottom.push_back(primaryActions(p, true));
		auto page = column({header(p, true), expanded(body), column(std::move(bottom), {p.pt(6)})}, {p.pt(8)});
		CardOptions options;
		options.padding = p.pt(10);
		return padding({p.pt(4), p.pt(4), p.pt(4), p.pt(4)}, card(page, options));
	}
	// Desktop and tablets: tabs and seats at the left, invite and chat at the right.
	Element content = currentTab == MapTab ? mapPanel(p, false) : currentTab == RulesTab ? rulesPanel(p) : seats(p, false);
	auto left = column({tabs(p), expanded(scroll("room/tab/" + std::to_string(currentTab), content))}, {p.pt(10)});
	auto right = column({invite(p, false), expanded(chatPanel(p, false))}, {p.pt(10)});
	auto body = adaptive([left, right](const LayoutContext &ctx, Size available) -> Element {
		if (available.w < ctx.presentation.pt(820))
			return column({expanded(left, 3), expanded(right, 2)}, {ctx.presentation.pt(10)});
		return row({expanded(left, 3), expanded(right, 2)}, {ctx.presentation.pt(16), CrossAlign::Stretch});
	});
	std::vector<Element> status{caption(room->setupSummary())};
	if (!waiting.empty())
		status.push_back(label(waiting, waitingText));
	auto footerRow = row({expanded(column(std::move(status), {p.pt(2)})), primaryActions(p, false)}, {p.pt(8), CrossAlign::Center});
	auto page = column({header(p, false), expanded(body), divider(), footerRow}, {p.pt(10)});
	CardOptions options;
	options.padding = p.pt(18);
	const int w = std::min(p.safe.w - p.pt(24), p.pt(1100));
	const int h = std::min(p.safe.h - p.pt(24), p.pt(740));
	return center(sized({w, h}, card(page, options)));
}
