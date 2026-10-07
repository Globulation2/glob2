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
						 {"resource/import", "[editor menu import resources]", IMPORT_RESOURCES}};
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

Element ResourcePaletteDialog::build(const Presentation& p)
{
	std::vector<Element> entries;
	std::vector<Element> controls;
	for (const auto& experiment : registry->experiments())
	{
		const auto key = experiment.key;
		controls.push_back(fe::toggle("resource-experiment/" + key, experimentLabel(experiment),
			enabled.has(key),
			[this, key](bool active) { enabled.set(key, active, registry->experimentKeys()); invalidate(); }));
	}
	for (unsigned id = 0; id < registry->size(); ++id)
	{
		const auto resource = static_cast<ResourceId>(id);
		const auto& experiment = registry->requiredExperiment(resource);
		if (!experiment.empty() && !enabled.has(experiment)) continue;
		fe::ButtonOptions options;
		options.minHeight = p.pt(48);
		entries.push_back(fe::button("resource/" + registry->key(resource), getResourceDisplayName(registry->presentation(resource).name),
			[this, id] { finish(int(id)); }, options));
	}
	fe::WrapOptions grid;
	grid.minChildWidth = p.pt(160);
	grid.maxColumns = 4;
	return fe::footer(fe::column({fe::paragraph(fe::tr("[Resource palette]"), {fe::FontRole::Heading}), fe::column(std::move(controls)),
		fe::scroll("resource/scroll", fe::wrap(std::move(entries), grid))}, {p.pt(8)}),
		dialogActions({{"cancel", fe::tr("[Cancel]"), [this] { finish(-1); }, false, SDLK_ESCAPE}}, p));
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

int TerrainPaletteDialog::groupFor(std::string_view key)
{
	for (unsigned g = 0; g < TERRAIN_GROUP_COUNT; ++g)
		if (key == terrainGroupDefinition(TerrainGroup(g)).key)
			return int(g);
	return -1;
}

TerrainPaletteDialog::TerrainPaletteDialog(std::vector<BrushGroup> terrainGroups, BrushSwatches &swatchCache,
										   std::string currentBrush, int focusGroup)
	: InGameDialog(Glob2UI::Surface::Editor), groups(std::move(terrainGroups)), swatches(swatchCache),
	  current(std::move(currentBrush)), focus(focusGroup)
{
}

TerrainPaletteDialog::~TerrainPaletteDialog() = default;

void TerrainPaletteDialog::focusOnOpen()
{
	// Headless editors never attach the dialog, so there is nothing to scroll.
	if (focus < 0 || globalContainer->runNoX)
		return;
	const std::string key = terrainGroupDefinition(TerrainGroup(focus)).key;
	const auto group = std::find_if(groups.begin(), groups.end(), [&](const BrushGroup &g) { return g.key == key; });
	if (group == groups.end() ||
		std::none_of(group->entries.begin(), group->entries.end(), [](const BrushEntry &e) { return !e.locked; }))
		return;
	// The group's heading is the row above its first brush: aligning that brush's
	// section column to the top shows the heading, the rules and the cards.
	host().layoutIfNeeded();
	host().scrollToTop("terrain/section/" + key);
}

Element TerrainPaletteDialog::build(const Presentation &p)
{
	const int tile = p.pt(56);
	// Uniform brush cards that never stretch, so a one-brush group reads the same
	// as a full row.
	const int cardWidth = p.pt(118);
	auto brush = [&](const BrushEntry &entry) -> Element
	{
		fe::ButtonOptions options;
		options.selected = entry.id == current;
		options.minHeight = 1;
		options.accessibleLabel = entry.label;
		fe::ImageOptions image;
		image.fit = true;
		image.size = fe::Size{tile, tile};
		std::vector<Element> body{fe::center(fe::image(swatches.get(entry, tile), image)),
								  fe::label(entry.label, {fe::FontRole::Support, false, fe::TextAlign::Center})};
		if (!entry.rules.empty())
			body.push_back(fe::paragraph(entry.rules, {fe::FontRole::Caption, true, fe::TextAlign::Center}));
		auto content = fe::padding(fe::Insets::all(p.pt(6)), fe::column(std::move(body), {p.pt(4)}));
		const int id = int(entry.swatch.terrain);
		return fe::width(cardWidth, fe::stack({fe::button(entry.id, "", [this, id] { finish(id); }, options),
											   std::move(content)}));
	};
	// Rows of equally sized cards packed from the left, so a one-brush group and a
	// full row share the same rhythm.
	const int gap = p.pt(6);
	const int available = std::min(p.pt(760), p.safe.w) - p.pt(44);
	const int columns = std::clamp(available / (cardWidth + gap), 2, 6);
	std::vector<Element> sections;
	// Locked (experiment-gated) brushes are not offered here; a group whose
	// members are all locked is hidden and the hint points at Settings.
	bool hidden = false;
	for (const auto &group : groups)
	{
		std::vector<Element> brushes;
		for (const auto &entry : group.entries)
			if (!entry.locked)
				brushes.push_back(brush(entry));
		if (brushes.empty())
		{
			hidden = true;
			continue;
		}
		// The keyed, invisible canvas lets a group brush scroll its heading to the top.
		std::vector<Element> parts{fe::canvas("terrain/section/" + group.key, {1, 1}, [](fe::Canvas &, fe::Rect, const fe::Frame &) {}),
								   fe::heading(group.title)};
		if (!group.rules.empty())
			parts.push_back(fe::caption(group.rules));
		for (std::size_t first = 0; first < brushes.size(); first += std::size_t(columns))
		{
			const auto last = std::min(brushes.size(), first + std::size_t(columns));
			std::vector<Element> cells(std::make_move_iterator(brushes.begin() + long(first)),
									   std::make_move_iterator(brushes.begin() + long(last)));
			parts.push_back(fe::row(std::move(cells), {gap}));
		}
		sections.push_back(fe::column(std::move(parts), {p.pt(4)}));
	}
	return fe::footer(
		fe::column({fe::paragraph(fe::tr("[Terrain palette]"), {fe::FontRole::Heading}),
					fe::caption(fe::tr(hidden ? "[terrain palette experiments hint]" : "[terrain palette hint]")),
					fe::scroll("terrain/scroll", fe::column(std::move(sections), {p.pt(14)}))},
				   {p.pt(8)}),
		dialogActions({{"cancel", fe::tr("[Cancel]"), [this] { finish(-1); }, false, SDLK_ESCAPE}},
					  p));
}
