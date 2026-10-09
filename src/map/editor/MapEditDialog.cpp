// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MapEditDialog.h"
#include "AINames.h"
#include "AllyTeamWidgetIndex.h"
#include "FormatableString.h"
#include "Game.h"
#include "GameHeader.h"
#include "GlobalContainer.h"
#include "MapHeader.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "BrushSwatches.h"
#include <cctype>
#include <iterator>
#include <algorithm>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

MapEditMenuScreen::MapEditMenuScreen() : InGameDialog(Glob2UI::Surface::Editor) {}

Element MapEditMenuScreen::build(const Presentation &p)
{
	struct Item
	{
		const char *key, *label;
		int code;
	};
	// File actions, then actions on this map. Palettes are reached from the
	// editor's side panel, not from this menu.
	const Item file[] = {{"save", "[editor menu save]", SAVE_MAP},
						 {"load", "[editor menu load]", LOAD_MAP},
						 {"share", "[maps share online]", SHARE_MAP},
						 {"terrain/import", "[editor menu import terrain]", IMPORT_TERRAIN},
						 {"resource/import", "[editor menu import resources]", IMPORT_RESOURCES},
                         {"set/import", "[editor menu import set]", IMPORT_SET},
                         {"set/library", "[editor menu set library]", SET_LIBRARY}};
	const Item map[] = {{"teams", "[editor menu teams]", OPEN_TEAMS_EDITOR},
						{"script", "[editor menu scenario]", OPEN_SCRIPT_EDITOR},
						{"terrain/reroll", "[editor menu reroll]", REROLL_TERRAIN_LOOK}};
	auto section = [&](const char *title, const auto &items)
	{
		std::vector<Element> buttons;
		for (const auto &item : items)
		{
			fe::ButtonOptions options;
			options.minHeight = 44;
			const int code = item.code;
			buttons.push_back(fe::button(item.key, fe::tr(item.label), [this, code] { finish(code); }, options));
		}
		fe::WrapOptions grid;
		grid.minChildWidth = p.pt(240);
		grid.maxColumns = 2;
		return fe::column({fe::heading(fe::tr(title)), fe::wrap(std::move(buttons), grid)}, {p.pt(6)});
	};
	std::vector<fe::MenuAction> actions;
	actions.push_back({"quit", fe::tr("[quit the editor]"), [this] { finish(QUIT_EDITOR); }});
	actions.push_back({"return", fe::tr("[return to editor]"), [this] { finish(RETURN_EDITOR); }, true, SDLK_ESCAPE});
	return fe::column({fe::paragraph(fe::tr("[Menu]"), {fe::FontRole::Heading, false, fe::TextAlign::Center}),
					   fe::footer(fe::scroll("menu/scroll", fe::column({section("[editor menu file]", file), section("[editor menu map]", map)}, {p.pt(14)})),
								  dialogActions(std::move(actions), p))},
					  {p.pt(12)});
}

AskForTextInput::AskForTextInput(const std::string &aLabel, const std::string &aCurrent)
	: InGameDialog(Glob2UI::Surface::Editor), labelText(aLabel), originalText(aCurrent), currentText(aCurrent)
{
}

std::string AskForTextInput::getText() const
{
	return finished() && result() == OK ? currentText : originalText;
}

void AskForTextInput::setText(const std::string &value)
{
	currentText = value;
	invalidate();
}

void AskForTextInput::confirm()
{
	finish(OK);
}

Element AskForTextInput::build(const Presentation &p)
{
	fe::TextFieldOptions entry;
	entry.autoFocus = true;
	entry.submit = [this](const std::string &) { confirm(); };
	auto field = fe::textField("text", currentText, [this](const std::string &value) { currentText = value; }, entry);
	std::vector<fe::MenuAction> actions;
	actions.push_back({"ok", fe::tr("[ok]"), [this] { confirm(); }, true});
	actions.push_back({"cancel", fe::tr("[Cancel]"), [this] { finish(CANCEL); }, false, SDLK_ESCAPE});
	return fe::footer(fe::column({fe::paragraph(fe::tr(labelText), {fe::FontRole::Heading}), field}, {p.pt(8)}), dialogActions(std::move(actions), p));
}

TeamsEditor::TeamsEditor(Game *game) : InGameDialog(Glob2UI::Surface::Editor), game(game)
{
	GameHeader &gameHeader = game->gameHeader;
	MapHeader &mapHeader = game->mapHeader;
	for (int i = 0; i < Team::MAX_COUNT; ++i)
	{
		auto &s = slots[i];
		s.active = i == 0 || gameHeader.getBasePlayer(i).type != BasePlayer::P_NONE;
		s.color = std::clamp(int(gameHeader.getBasePlayer(i).teamNumber), 0, std::max(0, mapHeader.getNumberOfTeams() - 1));
		if (i > 0)
			s.ai = gameHeader.getBasePlayer(i).type >= BasePlayer::P_AI ? gameHeader.getBasePlayer(i).type - BasePlayer::P_AI : AI::NONE;
		s.ally = allyTeamNumberToWidgetIndex(gameHeader.getAllyTeamNumber(gameHeader.getBasePlayer(i).teamNumber), mapHeader.getNumberOfTeams());
	}
}

void TeamsEditor::setActive(int index, bool active)
{
	if (index <= 0 || index >= Team::MAX_COUNT)
		return;
	slots[index].active = active;
	invalidate();
}

void TeamsEditor::setColor(int index, int color)
{
	if (index < 0 || index >= Team::MAX_COUNT)
		return;
	slots[index].color = color;
	// A slot joining a team takes that team's alliance group.
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		if (i != index && slots[i].color == color)
			slots[index].ally = slots[i].ally;
	invalidate();
}

void TeamsEditor::setAI(int index, int ai)
{
	if (index <= 0 || index >= Team::MAX_COUNT)
		return;
	slots[index].ai = ai;
	invalidate();
}

void TeamsEditor::setAlly(int index, int ally)
{
	if (index < 0 || index >= Team::MAX_COUNT)
		return;
	// Every slot of the same team shares the alliance group.
	const int team = slots[index].color;
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		if (slots[i].color == team)
			slots[i].ally = ally;
	invalidate();
}

void TeamsEditor::confirm()
{
	generateGameHeader();
	finish(OK);
}

void TeamsEditor::generateGameHeader()
{
	GameHeader gameHeader;
	int count = 0;
	for (int i = 0; i < Team::MAX_COUNT; i++)
	{
		if (slots[i].active)
		{
			int teamColor = slots[i].color;
			if (i == 0)
			{
				gameHeader.getBasePlayer(count) = BasePlayer(0, Toolkit::getStringTable()->getString("[Human]"), teamColor, BasePlayer::P_LOCAL);
			}
			else
			{
				AI::ImplementationID iid = static_cast<AI::ImplementationID>(slots[i].ai);
				FormattableString name("%0 %1");
				name.arg(AINames::getAIText(iid)).arg(i - 1);
				gameHeader.getBasePlayer(count) = BasePlayer(i, name.c_str(), teamColor, BasePlayer::playerTypeFromImplementationID(iid));
			}
			gameHeader.setAllyTeamNumber(teamColor, slots[i].ally + 1);
			count += 1;
		}
		else
		{
			gameHeader.getBasePlayer(i) = BasePlayer();
		}
	}
	gameHeader.setNumberOfPlayers(count);
	game->setGameHeader(gameHeader);
}

Element TeamsEditor::slotRow(int i, const Presentation &p, bool compact)
{
	const MapHeader &mapHeader = game->mapHeader;
	const auto &s = slots[i];
	const std::string id = std::to_string(i);
	std::vector<std::string> teams, groups, ais;
	for (int j = 0; j < mapHeader.getNumberOfTeams(); ++j)
	{
		teams.push_back(std::to_string(j + 1));
		groups.push_back(std::to_string(j + 1));
	}
	for (int aii = 0; aii < AI::JAVASCRIPT; aii++)
		ais.push_back(AINames::getAIText(aii));
	const GAGCore::Color color = mapHeader.getNumberOfTeams() > 0 ? mapHeader.getBaseTeam(std::min(s.color, mapHeader.getNumberOfTeams() - 1)).color
																   : GAGCore::Color::white;
	Element slotControl = i == 0 ? fe::label(fe::tr("[Human]")) : fe::toggle("slot/" + id, std::to_string(i + 1), s.active, [this, i](bool v) { setActive(i, v); });
	if (!s.active)
		return compact ? fe::row({slotControl, fe::expanded(fe::label(fe::tr("[Inactive]"), {fe::FontRole::Body, true}))}, {p.pt(8), fe::CrossAlign::Center})
					   : fe::row({fe::width(p.pt(110), slotControl), fe::expanded(fe::label(fe::tr("[Inactive]"), {fe::FontRole::Body, true}))},
								 {p.pt(8), fe::CrossAlign::Center});
	auto colorControl = fe::row({fe::swatch(color, 16), fe::expanded(fe::choice("color/" + id, teams, s.color, [this, i](int v) { setColor(i, v); }))},
								{p.pt(4), fe::CrossAlign::Center});
	auto player = i == 0 ? fe::label(fe::tr("[Human]")) : fe::choice("ai/" + id, ais, s.ai, [this, i](int v) { setAI(i, v); });
	auto group = fe::choice("group/" + id, groups, s.ally, [this, i](int v) { setAlly(i, v); });
	if (compact)
	{
		fe::CardOptions plain;
		plain.shadow = false;
		return fe::card(fe::column({slotControl, fe::field(fe::tr("[Color]"), colorControl, {"", 160, true}), fe::field(fe::tr("[Player / AI]"), player, {"", 160, true}),
									fe::field(fe::tr("[Group]"), group, {"", 160, true})},
								   {p.pt(6)}),
						plain);
	}
	return fe::row({fe::width(p.pt(110), slotControl), fe::width(p.pt(120), colorControl), fe::expanded(player), fe::width(p.pt(90), group)},
				   {p.pt(8), fe::CrossAlign::Center});
}

Element TeamsEditor::build(const Presentation &p)
{
	const bool compact = p.compact();
	std::vector<Element> rows;
	if (!compact)
		rows.push_back(fe::row({fe::width(p.pt(110), fe::caption(fe::tr("[Slot]"))), fe::width(p.pt(120), fe::caption(fe::tr("[Color]"))),
								fe::expanded(fe::caption(fe::tr("[Player / AI]"))), fe::width(p.pt(90), fe::caption(fe::tr("[Group]")))},
							   {p.pt(8)}));
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		rows.push_back(slotRow(i, p, compact));
	std::vector<fe::MenuAction> actions;
	actions.push_back({"ok", fe::tr("[ok]"), [this] { confirm(); }, true});
	actions.push_back({"cancel", fe::tr("[Cancel]"), [this] { finish(CANCEL); }, false, SDLK_ESCAPE});
	return fe::column({fe::paragraph(fe::tr("[teams editor]"), {fe::FontRole::Heading, false, fe::TextAlign::Center}),
					   fe::expanded(fe::footer(fe::scroll("teams/scroll", fe::column(std::move(rows), {p.pt(compact ? 8 : 4)})), dialogActions(std::move(actions), p)))},
					  {p.pt(10)});
}
