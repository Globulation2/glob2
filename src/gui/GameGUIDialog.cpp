// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GameGUIDialog.h"
#include "FormatableString.h"
#include "ScrollTuning.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "SoundMixer.h"
#include "StringTable.h"
#include "Toolkit.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

namespace
{
std::string localized(const std::string &text)
{
	if (GAGCore::Toolkit::getStringTable()->doesStringExist(text.c_str()))
		return GAGCore::Toolkit::getStringTable()->getString(text.c_str());
	return text;
}
} // namespace

//! Main menu screen
InGameMainScreen::InGameMainScreen(bool isReplay, bool canSave, bool paused)
	: replay(isReplay), canSave(canSave), paused(paused)
{
}

Element InGameMainScreen::build(const Presentation &p)
{
	const std::string returnLabel = fe::tr(replay ? "[return to replay]" : "[return to game]");
	const std::string loadLabel = fe::tr(replay ? "[load replay]" : "[load game]");
	const std::string quitLabel = fe::tr(replay ? "[quit the replay]" : networked ? "[leave match]" : "[quit the game]");
	// A networked match cannot be loaded over or saved: the relay owns its turns.
	const bool files = !networked;
	if (classic())
	{
		// The classic desktop menu: a column of gold buttons, Return last.
		std::vector<Element> buttons;
		if (files)
			buttons.push_back(classicButton("load", loadLabel, [this] { finish(LOAD_GAME); }));
		if (files && !replay && canSave)
			buttons.push_back(classicButton("save", fe::tr("[save game]"), [this] { finish(SAVE_GAME); }));
		if(hiveMind)buttons.push_back(classicButton("hive", "Hive Mind", [this]{finish(HIVE_MIND);}));
		buttons.push_back(classicButton("options", fe::tr("[Options]"), [this] { finish(OPTIONS); }));
		buttons.push_back(classicButton("quit", quitLabel, [this] { finish(QUIT_GAME); }));
		buttons.push_back(classicButton("return", returnLabel, [this] { finish(RETURN_GAME); }, SDLK_ESCAPE));
		return fe::column(std::move(buttons), {p.pt(10)});
	}
	// The touch sheet: titled, Return highlighted at the bottom.
	auto item = [&](const char *key, const std::string &label, int code, bool selected = false, SDL_Keycode shortcut = SDLK_UNKNOWN)
	{
		fe::ButtonOptions options;
		options.selected = selected;
		options.shortcut = shortcut;
		options.minHeight = 44;
		return fe::button(key, label, [this, code] { finish(code); }, options);
	};
	std::vector<Element> buttons;
	if (files && !replay && canSave)
		buttons.push_back(item("save", fe::tr("[save game]"), SAVE_GAME));
	if (files)
		buttons.push_back(item("load", loadLabel, LOAD_GAME));
	if(hiveMind)buttons.push_back(item("hive","Hive Mind",HIVE_MIND));
	buttons.push_back(item("options", fe::tr("[Options]"), OPTIONS));
	buttons.push_back(item("quit", quitLabel, QUIT_GAME));
	buttons.push_back(item("pause", fe::tr(paused ? "[resume game]" : "[pause game]"), PAUSE_GAME));
	// Return stays pinned below the list so it is always in reach.
	return fe::column({fe::paragraph(fe::tr("[Menu]"), {fe::FontRole::Heading, false, fe::TextAlign::Center}),
					   fe::footer(fe::scroll("menu/scroll", fe::column(std::move(buttons), {p.pt(8)})),
								  item("return", returnLabel, RETURN_GAME, true, SDLK_ESCAPE))},
					  {p.pt(12)});
}

InGameConfirmScreen::InGameConfirmScreen(std::string title, std::string body, std::string confirmLabel,
										 std::string cancelLabel)
	: title(std::move(title)), body(std::move(body)), confirmLabel(std::move(confirmLabel)),
	  cancelLabel(std::move(cancelLabel))
{
}

Element InGameConfirmScreen::build(const Presentation &p)
{
	std::vector<Element> parts;
	parts.push_back(fe::paragraph(title, {fe::FontRole::Heading, false, fe::TextAlign::Center}));
	parts.push_back(fe::paragraph(body, {fe::FontRole::Body}));
	std::vector<Element> buttons;
	if (classic())
	{
		buttons.push_back(classicButton("confirm", confirmLabel, [this] { finish(CONFIRM); }));
		buttons.push_back(classicButton("cancel", cancelLabel, [this] { finish(CANCEL); }, SDLK_ESCAPE));
	}
	else
	{
		// Staying is the highlighted choice; leaving needs a deliberate tap.
		fe::ButtonOptions stay;
		stay.primary = true;
		stay.shortcut = SDLK_ESCAPE;
		stay.minHeight = 44;
		fe::ButtonOptions go;
		go.minHeight = 44;
		buttons.push_back(fe::button("cancel", cancelLabel, [this] { finish(CANCEL); }, stay));
		buttons.push_back(fe::button("confirm", confirmLabel, [this] { finish(CONFIRM); }, go));
	}
	parts.push_back(fe::column(std::move(buttons), {p.pt(classic() ? 10 : 8)}));
	return fe::column(std::move(parts), {p.pt(12)});
}

InGameEndOfGameScreen::InGameEndOfGameScreen(std::string title, bool canContinue, std::optional<GAGCore::Color> teamColor,
											 bool won)
	: title(std::move(title)), canContinue(canContinue), teamColor(teamColor), won(won)
{
}

Element InGameEndOfGameScreen::build(const Presentation &p)
{
	std::vector<Element> parts;
	if (!classic() && teamColor && globalContainer->unitmini)
	{
		const GAGCore::Color color = *teamColor;
		const bool animate = won && globalContainer->settings.decorativeAnimations &&
							 !(globalContainer->reducedMotion);
		parts.push_back(fe::canvas("outcome/art", {p.pt(200), p.pt(72)},
								   [color, animate, unit = p.unit](fe::Canvas &c, fe::Rect r, const fe::Frame &frame)
								   {
									   c.fillRect({r.x, r.y, r.w, std::max(1, int(4 * unit))}, color);
									   auto *sprite = globalContainer->unitmini;
									   sprite->setBaseColor(color);
									   for (int i = 0; i < 3; ++i)
									   {
										   const double bob = animate ? std::sin(frame.tick / 220.0 + i) * 3 * unit : 0;
										   const int w = sprite->getW(i);
										   c.transformed(2 * unit, {r.x + r.w / 2 + int((i - 1) * 44 * unit) - int(w * unit), r.y + int(12 * unit + bob)}, r,
														 [&] { c.drawSprite({0, 0}, sprite, i); });
									   }
								   }));
	}
	parts.push_back(fe::paragraph(title, {classic() ? fe::FontRole::Heading : fe::FontRole::Title, false, fe::TextAlign::Center}));
	std::vector<fe::MenuAction> actions;
	// The classic order: Ok first, then the ways to stay.
	actions.push_back({"ok", fe::tr("[ok]"), [this] { finish(QUIT); }, true, SDLK_RETURN});
	if (canContinue)
	{
		if (globalContainer->replaying)
		{
			actions.push_back({"continue", fe::tr("[look around]"), [this] { finish(CONTINUE); }, false, SDLK_ESCAPE});
			actions.push_back({"watch", fe::tr("[watch again]"), [this] { finish(WATCH_AGAIN); }});
		}
		else
			actions.push_back({"continue", fe::tr("[Continue playing]"), [this] { finish(CONTINUE); }, false, SDLK_ESCAPE});
	}
	else if (globalContainer->replaying)
		actions.push_back({"watch", fe::tr("[watch again]"), [this] { finish(WATCH_AGAIN); }});
	if (!classic())
		std::rotate(actions.begin(), actions.begin() + 1, actions.end());
	std::vector<Element> buttons;
	for (const auto &item : actions)
	{
		if (classic())
		{
			buttons.push_back(classicButton(item.key, item.label, item.action, item.shortcut));
			continue;
		}
		fe::ButtonOptions options;
		options.primary = item.primary;
		options.shortcut = item.shortcut;
		options.minHeight = 44;
		buttons.push_back(fe::button(item.key, item.label, item.action, options));
	}
	parts.push_back(fe::column(std::move(buttons), {p.pt(classic() ? 10 : 8)}));
	return fe::column(std::move(parts), {p.pt(12)});
}

//! Alliance screen
InGameAllianceScreen::InGameAllianceScreen(GameGUI *gameGUI) : gameGUI(gameGUI)
{
	Game &game = gameGUI->game;
	players = game.gameHeader.getNumberOfPlayers();
	editable = !globalContainer->replaying;
	const bool fixed = game.gameHeader.areAllyTeamsFixed() && !globalContainer->replaying;
	for (int i = 0; i < players; i++)
	{
		const int otherTeam = game.players[i]->teamNumber;
		const Uint32 otherTeamMask = Team::teamNumberToMask(otherTeam);
		teamOf[i] = otherTeam;
		ownAlliance[i] = (gameGUI->localTeam->allies & otherTeamMask) != 0;
		ownNormal[i] = (gameGUI->localTeam->sharedVisionOther & otherTeamMask) != 0;
		ownFood[i] = (gameGUI->localTeam->sharedVisionFood & otherTeamMask) != 0;
		ownMarket[i] = (gameGUI->localTeam->sharedVisionExchange & otherTeamMask) != 0;
		ownChat[i] = ((gameGUI->chatMask) & (1 << i)) != 0;
		if (otherTeam == gameGUI->localTeamNo)
			continue;
		Entry entry;
		entry.player = i;
		entry.team = otherTeam;
		const auto type = game.players[i]->type;
		if (type >= Player::P_AI || type == Player::P_IP || type == Player::P_LOCAL)
			entry.name = game.players[i]->name;
		else
			entry.name = "(" + game.players[i]->name + ")";
		entry.color = game.players[i]->team->color;
		entry.alliance = ownAlliance[i];
		entry.normalVision = ownNormal[i];
		entry.foodVision = ownFood[i];
		entry.marketVision = ownMarket[i];
		entry.chat = ownChat[i];
		entry.diplomacy = !fixed;
		rows.push_back(entry);
	}
}

bool &InGameAllianceScreen::field(Entry &entry, Setting setting) const
{
	switch (setting)
	{
	case Alliance:
		return entry.alliance;
	case NormalVision:
		return entry.normalVision;
	case FoodVision:
		return entry.foodVision;
	case MarketVision:
		return entry.marketVision;
	default:
		return entry.chat;
	}
}

void InGameAllianceScreen::mirror(int player, Setting setting)
{
	if (setting == Chat)
		return;
	Entry *source = nullptr;
	for (auto &entry : rows)
		if (entry.player == player)
			source = &entry;
	if (!source)
		return;
	// Two players of the same team must have the same alliance and vision.
	for (auto &entry : rows)
		if (entry.player != player && entry.team == source->team)
			field(entry, setting) = field(*source, setting);
}

void InGameAllianceScreen::set(int player, Setting setting, bool value)
{
	for (auto &entry : rows)
		if (entry.player == player)
		{
			field(entry, setting) = value;
			mirror(player, setting);
			invalidate();
			return;
		}
}

Element InGameAllianceScreen::build(const Presentation &p)
{
	const bool compact = p.compact();
	const char *shortLabels[] = {"A", "V", "fV", "mV", "C"};
	const char *explanations[] = {"[abreaviation explanation A]", "[abreaviation explanation V]", "[abreaviation explanation fV]",
								  "[abreaviation explanation mV]", "[abreaviation explanation C]"};
	const char *longLabels[] = {"[Alliance]", "[Share vision]", "[Share food vision]", "[Share market vision]", "[Chat]"};
	auto notesColumn = [&]
	{
		std::vector<Element> notes;
		for (int s = 0; s < 5; ++s)
			notes.push_back(fe::paragraph(fe::tr(explanations[s]), {fe::FontRole::Caption, true}));
		notes.push_back(fe::paragraph(fe::tr("[shortcut explanation enter]"), {fe::FontRole::Caption, true}));
		notes.push_back(fe::paragraph(fe::tr("[shortcut explanation v]"), {fe::FontRole::Caption, true}));
		return fe::column(std::move(notes), {p.pt(2)});
	};
	if (classic())
	{
		// The classic box: names in team colours, five checkbox columns under
		// their abbreviations, the explanations below, Ok at the bottom.
		const int nameWidth = p.pt(160), cell = p.pt(24);
		std::vector<Element> header{fe::width(nameWidth, fe::empty())};
		for (int s = 0; s < 5; ++s)
			header.push_back(fe::width(cell, fe::label(shortLabels[s], {fe::FontRole::Body, false, fe::TextAlign::Center})));
		std::vector<Element> table{fe::row(std::move(header), {p.pt(4), fe::CrossAlign::Center})};
		for (auto &entry : rows)
		{
			fe::TextOptions nameStyle;
			nameStyle.color = entry.color;
			nameStyle.role = fe::FontRole::Heading;
			std::vector<Element> cells{fe::width(nameWidth, fe::label(entry.name, nameStyle))};
			for (int s = 0; s < 5; ++s)
			{
				const auto setting = Setting(s);
				const bool locked = !entry.diplomacy && (setting == Alliance || setting == NormalVision);
				const std::string key = "ally/" + std::to_string(entry.player) + "/" + shortLabels[s];
				const int player = entry.player;
				cells.push_back(fe::width(cell, locked ? fe::empty()
														: fe::toggle(key, "", field(entry, setting),
																	 [this, player, setting](bool value) { set(player, setting, value); }, editable)));
			}
			table.push_back(fe::row(std::move(cells), {p.pt(4), fe::CrossAlign::Center}));
		}
		std::vector<Element> parts{fe::column(std::move(table), {p.pt(2)})};
		if (rows.empty())
			parts.push_back(fe::paragraph(fe::tr("[No other players have editable diplomatic settings in this match.]"), {fe::FontRole::Body, true}));
		else if (!rows.front().diplomacy)
			parts.push_back(fe::paragraph(fe::tr("[Alliance and shared vision are fixed for this match.]"), {fe::FontRole::Support, true}));
		parts.push_back(fe::spacer(p.pt(40)));
		parts.push_back(notesColumn());
		parts.push_back(classicButton("ok", fe::tr("[ok]"), [this] { finish(OK); }, SDLK_RETURN));
		return fe::column(std::move(parts), {p.pt(10)});
	}
	std::vector<Element> list;
	for (auto &entry : rows)
	{
		std::vector<Element> toggles;
		for (int s = 0; s < 5; ++s)
		{
			const auto setting = Setting(s);
			if (!entry.diplomacy && (setting == Alliance || setting == NormalVision))
				continue;
			const std::string key = "ally/" + std::to_string(entry.player) + "/" + shortLabels[s];
			const int player = entry.player;
			toggles.push_back(fe::toggle(key, compact ? fe::tr(longLabels[s]) : std::string(shortLabels[s]), field(entry, setting),
										 [this, player, setting](bool value) { set(player, setting, value); }, editable));
		}
		fe::TextOptions nameStyle;
		nameStyle.color = entry.color;
		fe::WrapOptions grid;
		grid.minChildWidth = p.pt(compact ? 150 : 64);
		fe::CardOptions plain;
		plain.shadow = false;
		if (compact)
			list.push_back(fe::card(fe::column({fe::label(entry.name, nameStyle), fe::wrap(std::move(toggles), grid)}, {p.pt(6)}), plain));
		else
			list.push_back(fe::row({fe::width(p.pt(160), fe::label(entry.name, nameStyle)), fe::expanded(fe::wrap(std::move(toggles), grid))},
								   {p.pt(8), fe::CrossAlign::Center}));
	}
	std::vector<Element> parts;
	parts.push_back(fe::paragraph(fe::tr("[Teams]"), {fe::FontRole::Heading, false, fe::TextAlign::Center}));
	if (rows.empty())
		parts.push_back(fe::paragraph(fe::tr("[No other players have editable diplomatic settings in this match.]"), {fe::FontRole::Body, true}));
	else if (!rows.front().diplomacy)
		parts.push_back(fe::paragraph(fe::tr("[Alliance and shared vision are fixed for this match.]"), {fe::FontRole::Support, true}));
	std::vector<Element> body = {fe::column(std::move(list), {p.pt(compact ? 8 : 4)})};
	if (!compact)
	{
		body.push_back(fe::divider());
		body.push_back(notesColumn());
	}
	parts.push_back(fe::scroll("ally/scroll", fe::column(std::move(body), {p.pt(8)})));
	fe::ButtonOptions okOptions;
	okOptions.primary = true;
	okOptions.shortcut = SDLK_RETURN;
	okOptions.minHeight = 44;
	auto ok = fe::button("ok", fe::tr("[ok]"), [this] { finish(OK); }, okOptions);
	return fe::column({fe::footer(fe::column(std::move(parts), {p.pt(10)}), ok)});
}

int InGameAllianceScreen::countNumberPlayersForLocalTeam(GameHeader &gameHeader, int localteam)
{
	int count = 0;
	for (int i = 0; i < gameHeader.getNumberOfPlayers(); i++)
		if (gameHeader.getBasePlayer(i).teamNumber == localteam)
			count += 1;
	return count;
}

namespace
{
template <class Own, class Pick>
Uint32 mask(int players, const std::vector<InGameAllianceScreen::Entry> &rows, const Own &own, Pick pick, bool invert = false)
{
	Uint32 result = 0;
	for (int i = 0; i < players; i++)
	{
		bool state = own[i];
		for (const auto &entry : rows)
			if (entry.player == i)
				state = pick(entry);
		if (state != invert)
			result |= 1 << i;
	}
	return result;
}
} // namespace

Uint32 InGameAllianceScreen::getAlliedMask() const
{
	return mask(players, rows, ownAlliance, [](const Entry &e) { return e.alliance; });
}

Uint32 InGameAllianceScreen::getEnemyMask() const
{
	return mask(players, rows, ownAlliance, [](const Entry &e) { return e.alliance; }, true);
}

Uint32 InGameAllianceScreen::getExchangeVisionMask() const
{
	return mask(players, rows, ownMarket, [](const Entry &e) { return e.marketVision; });
}

Uint32 InGameAllianceScreen::getFoodVisionMask() const
{
	return mask(players, rows, ownFood, [](const Entry &e) { return e.foodVision; });
}

Uint32 InGameAllianceScreen::getOtherVisionMask() const
{
	return mask(players, rows, ownNormal, [](const Entry &e) { return e.normalVision; });
}

Uint32 InGameAllianceScreen::getChatMask() const
{
	return mask(players, rows, ownChat, [](const Entry &e) { return e.chat; });
}

//! Option Screen
InGameOptionScreen::InGameOptionScreen(GameGUI *gameGUI) : gameGUI(gameGUI)
{
	adjustableGameSpeed = gameGUI->canChangeGameSpeed();
}

InGameOptionScreen::~InGameOptionScreen()
{
	globalContainer->settings.save();
}

void InGameOptionScreen::applyVolume()
{
	auto &settings = globalContainer->settings;
	globalContainer->mix->setVolume(settings.musicVolume, settings.voiceVolume, settings.mute);
}

void InGameOptionScreen::setMute(bool value)
{
	globalContainer->settings.mute = value;
	applyVolume();
	invalidate();
}

void InGameOptionScreen::setGameSpeed(int speed)
{
	if (!adjustableGameSpeed)
		return;
	globalContainer->settings.gameSpeed = std::clamp(speed, int(Settings::GAME_SPEED_MINIMUM), int(Settings::GAME_SPEED_MAXIMUM));
	invalidate();
}

std::string InGameOptionScreen::gameSpeedText() const
{
	if (!adjustableGameSpeed)
		return fe::tr("[multiplayer game speed]");
	return GAGCore::FormattableString("%0: %1").arg(fe::tr("[game speed]")).arg(globalContainer->settings.getGameSpeedText());
}

Element InGameOptionScreen::build(const Presentation &p)
{
	auto &settings = globalContainer->settings;
	std::vector<Element> parts;
	if (!classic())
		parts.push_back(fe::paragraph(fe::tr("[Options]"), {fe::FontRole::Heading, false, fe::TextAlign::Center}));
	parts.push_back(fe::toggle("mute", fe::tr("[Mute]"), settings.mute, [this](bool value) { setMute(value); }));
	if (!settings.mute)
	{
		fe::SliderOptions music;
		music.caption = fe::tr("[Music volume]");
		parts.push_back(fe::slider("music", settings.musicVolume, 0, 256,
								   [this](int value)
								   {
									   globalContainer->settings.musicVolume = value;
									   applyVolume();
								   },
								   music));
		fe::SliderOptions voice;
		voice.caption = fe::tr("[Voice volume]");
		parts.push_back(fe::slider("voice", settings.voiceVolume, 0, 256,
								   [this](int value)
								   {
									   globalContainer->settings.voiceVolume = value;
									   applyVolume();
								   },
								   voice));
	}
	if (adjustableGameSpeed)
	{
		fe::SliderOptions speed;
		speed.caption = gameSpeedText();
		speed.valueText = settings.getGameSpeedText();
		parts.push_back(fe::slider("speed", settings.gameSpeed, Settings::GAME_SPEED_MINIMUM, Settings::GAME_SPEED_MAXIMUM,
								   [this](int value) { setGameSpeed(value); }, speed));
	}
	else
		parts.push_back(fe::paragraph(gameSpeedText(), {fe::FontRole::Body, true}));
	if (p.touch)
	{
		parts.push_back(fe::toggle("motion", fe::tr("[Reduced motion]"), globalContainer->reducedMotion,
								   [this](bool value)
								   {
									   globalContainer->reducedMotion = value;
									   applyScrollTuning(globalContainer->settings, value);
									   invalidate();
								   }));
		// Touch scroll feel, tunable mid-game like volume: 0 is off, 50 the default.
		struct ScrollSlider
		{
			const char *key, *caption;
			int Settings::*field;
		};
		for (const auto &item : {ScrollSlider{"momentum", "[List momentum]", &Settings::touchScrollMomentum},
								 ScrollSlider{"bounce", "[List bounce]", &Settings::touchScrollBounce},
								 ScrollSlider{"mapmomentum", "[Map momentum]", &Settings::mapScrollMomentum}})
		{
			fe::SliderOptions options;
			options.caption = fe::tr(item.caption);
			options.valueText = std::to_string(settings.*item.field) + "%";
			parts.push_back(fe::slider(item.key, settings.*item.field, 0, 100,
									   [this, field = item.field](int value)
									   {
										   auto &s = globalContainer->settings;
										   s.*field = std::clamp(value, 0, 100);
										   applyScrollTuning(s, globalContainer->reducedMotion);
										   invalidate();
									   },
									   options));
		}
		// The same preference as Settings > Display; every touch text surface follows it.
		const int percent = settings.textSizePercent;
		const int selected = percent >= 150 ? 2 : percent >= 125 ? 1 : 0;
		std::vector<std::string> sizes;
		for (int i = 0; i < 3; ++i)
			sizes.push_back(std::to_string(100 + i * 25) + " %");
		parts.push_back(fe::label(fe::tr("[settings Text size]"), {fe::FontRole::Support, true}));
		parts.push_back(fe::segments("text-size", sizes, selected,
									 [](int index) { globalContainer->settings.setTextSizePercent(100 + index * 25); }));
	}
	std::ostringstream oss;
	oss << globalContainer->gfx->getW() << "x" << globalContainer->gfx->getH();
	if (globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU)
		oss << " GL";
	else
		oss << " SDL";
	parts.push_back(fe::paragraph(oss.str(), {fe::FontRole::Caption, true, fe::TextAlign::Center}));
	if (classic())
	{
		parts.push_back(fe::spacer(p.pt(10)));
		parts.push_back(classicButton("ok", fe::tr("[ok]"), [this] { finish(OK); }, SDLK_RETURN));
		return fe::column(std::move(parts), {p.pt(10)});
	}
	fe::ButtonOptions okOptions;
	okOptions.primary = true;
	okOptions.shortcut = SDLK_RETURN;
	okOptions.minHeight = 44;
	auto ok = fe::button("ok", fe::tr("[ok]"), [this] { finish(OK); }, okOptions);
	return fe::column({fe::footer(fe::scroll("options/scroll", fe::column(std::move(parts), {p.pt(10)})), ok)});
}

InGameObjectivesScreen::InGameObjectivesScreen(GameGUI *gui, bool showBriefing)
{
	briefing = localized(gui->game.missionBriefing);
	for (int i = 0; i < gui->game.objectives.getNumberOfObjectives(); ++i)
	{
		if (!gui->game.objectives.isObjectiveVisible(i))
			continue;
		Line line;
		line.text = localized(gui->game.objectives.getGameObjectiveText(i));
		line.state = gui->game.objectives.isObjectiveComplete(i) ? 1 : gui->game.objectives.isObjectiveFailed(i) ? 2 : 0;
		if (gui->game.objectives.getObjectiveType(i) == GameObjectives::Primary)
			primary.push_back(line);
		else
			secondary.push_back(line);
	}
	for (int i = 0; i < gui->game.objectives.getNumberOfObjectives(); ++i)
		if (gui->game.objectives.getObjectiveType(i) == GameObjectives::Secondary)
			hasSecondary = true;
	int n = 0;
	for (int i = 0; i < gui->game.gameHints.getNumberOfHints(); ++i)
		if (gui->game.gameHints.isHintVisible(i))
		{
			Line line;
			line.text = std::to_string(++n) + ") " + localized(gui->game.gameHints.getGameHintText(i));
			hints.push_back(line);
		}
	page = showBriefing && !briefing.empty() ? BRIEFING : OBJECTIVES;
}

void InGameObjectivesScreen::showTab(int tab)
{
	page = tab;
	invalidate();
}

Element InGameObjectivesScreen::build(const Presentation &p)
{
	std::vector<std::string> tabs;
	std::vector<int> ids;
	if (!briefing.empty())
	{
		tabs.push_back(fe::tr("[briefing]"));
		ids.push_back(BRIEFING);
	}
	tabs.push_back(fe::tr("[objectives]"));
	ids.push_back(OBJECTIVES);
	tabs.push_back(fe::tr("[hints]"));
	ids.push_back(HINTS);
	int selected = 0;
	for (std::size_t i = 0; i < ids.size(); ++i)
		if (ids[i] == page)
			selected = int(i);
	auto lines = [&](const std::vector<Line> &items, const char *emptyKey)
	{
		std::vector<Element> result;
		if (items.empty())
			result.push_back(fe::paragraph(fe::tr(emptyKey), {fe::FontRole::Body, true}));
		for (const auto &line : items)
		{
			if (line.state < 0)
			{
				result.push_back(fe::paragraph(line.text));
				continue;
			}
			const int state = line.state;
			auto mark = fe::canvas("", {p.pt(20), p.pt(20)},
								   [state](fe::Canvas &c, fe::Rect r, const fe::Frame &frame)
								   {
									   const auto &palette = frame.layout.theme.palette;
									   c.strokeRect(r, palette.line);
									   if (state == 1)
										   c.fillRect(r.inset(std::max(2, r.w / 5)), palette.accent);
									   else if (state == 2)
									   {
										   c.line({r.x + 3, r.y + 3}, {r.right() - 4, r.bottom() - 4}, palette.danger);
										   c.line({r.right() - 4, r.y + 3}, {r.x + 3, r.bottom() - 4}, palette.danger);
									   }
								   });
			result.push_back(fe::row({mark, fe::expanded(fe::paragraph(line.text))}, {p.pt(8), fe::CrossAlign::Start}));
		}
		return result;
	};
	std::vector<Element> content;
	if (page == BRIEFING)
		content.push_back(fe::paragraph(briefing));
	else if (page == HINTS)
		content = lines(hints, "[No Hints]");
	else
	{
		content.push_back(fe::paragraph(fe::tr("[Primary Objectives]"), {fe::FontRole::Heading}));
		for (auto &element : lines(primary, "[No Objectives]"))
			content.push_back(element);
		if (hasSecondary)
		{
			content.push_back(fe::paragraph(fe::tr("[Secondary Objectives]"), {fe::FontRole::Heading}));
			for (auto &element : lines(secondary, "[No Objectives]"))
				content.push_back(element);
		}
	}
	auto header = fe::segments("objectives/tab", tabs, selected, [this, ids](int index) { showTab(ids[std::size_t(index)]); });
	auto body = fe::scroll("objectives/scroll", fe::column(std::move(content), {p.pt(8)}));
	if (classic())
	{
		auto ok = classicButton("ok", fe::tr("[ok]"), [this] { finish(OK); }, SDLK_RETURN);
		return fe::column({header, fe::height(p.pt(300), body), ok}, {p.pt(10)});
	}
	fe::ButtonOptions okOptions;
	okOptions.primary = true;
	okOptions.shortcut = SDLK_RETURN;
	okOptions.minHeight = 44;
	auto ok = fe::button("ok", fe::tr("[ok]"), [this] { finish(OK); }, okOptions);
	return fe::column({header, fe::expanded(fe::footer(body, ok))}, {p.pt(10)});
}

InGameTextInput::InGameTextInput() = default;

GAGGUI::ui::Rect InGameTextInput::available(const GAGGUI::ui::Presentation &presentation, const GAGGUI::ui::Metrics &metrics)
{
	if (!classic())
		return InGameDialog::available(presentation, metrics);
	// The classic composer sits at the bottom left of the map.
	GAGGUI::ui::Rect area = presentation.dialog.inset(metrics.padding);
	area.w = std::min(area.w, presentation.pt(maxWidth()));
	return area;
}

GAGGUI::ui::Rect InGameTextInput::place(GAGGUI::ui::Size measured, GAGGUI::ui::Rect area)
{
	const int w = area.w;
	const int h = std::min(area.h, measured.h);
	return {area.x, area.bottom() - h, w, h};
}

Element InGameTextInput::build(const Presentation &p)
{
	fe::TextFieldOptions options;
	options.maxLength = 256;
	options.autoFocus = true;
	options.placeholder = fe::tr("[Chat · recipients selected in Teams]");
	options.submit = [this](const std::string &) { finish(0); };
	auto entry = fe::textField("chat", text, [this](const std::string &value) { text = value; }, options);
	if (classic())
		return entry;
	fe::ButtonOptions sendOptions;
	sendOptions.primary = true;
	auto send = fe::compactButton(
		"send", fe::tr("[Send]"), fe::UIIcon::Send, [this] { finish(0); }, p, sendOptions);
	auto close =
		fe::compactButton("close", fe::tr("[Close]"), fe::UIIcon::Close, [this] { finish(1); }, p);
	return fe::row({fe::expanded(entry), send, close}, {p.pt(6), fe::CrossAlign::Center});
}
