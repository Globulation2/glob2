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
#include "TerrainExperiments.h"
#include "render/terrain/TerrainCompositor.h"
#include "GraphicContext.h"
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
		bool primary;
	};
	const Item items[] = {{"return", "[return to editor]", RETURN_EDITOR, true},
						  {"save", "[save map]", SAVE_MAP, false},
						  {"load", "[load map]", LOAD_MAP, false},
						  {"script", "[open scenario editor]", OPEN_SCRIPT_EDITOR, false},
						  {"teams", "[open teams editor]", OPEN_TEAMS_EDITOR, false},
						  {"share", "[maps share online]", SHARE_MAP, false},
						  {"terrain/import", "[Import Terrain Definitions]", IMPORT_TERRAIN, false},
						  {"terrain/palette", "[Terrain palette]", TERRAIN_PALETTE, false},
						  {"resource/import", "[Import Resource Definitions]", IMPORT_RESOURCES, false},
						  {"resource/palette", "[Resource palette]", RESOURCE_PALETTE, false},
						  {"terrain/reroll", "[Reroll terrain look]", REROLL_TERRAIN_LOOK, false},
						  {"quit", "[quit the editor]", QUIT_EDITOR, false}};
	std::vector<Element> buttons;
	for (const auto &item : items)
	{
		fe::ButtonOptions options;
		options.primary = item.primary;
		options.minHeight = 44;
		if (item.primary)
			options.shortcut = SDLK_ESCAPE;
		const int code = item.code;
		buttons.push_back(
			fe::button(item.key, fe::tr(item.label), [this, code] { finish(code); }, options));
	}
	fe::WrapOptions grid;
	grid.minChildWidth = p.pt(260);
	grid.maxColumns = 2;
	return fe::column({fe::paragraph(fe::tr("[Menu]"), {fe::FontRole::Heading, false, fe::TextAlign::Center}),
					   fe::scroll("menu/scroll", fe::wrap(std::move(buttons), grid))},
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

namespace
{
// One line of plain rules read from the properties, so the palette tells the
// author what a brush does without a per-type string.
std::string terrainRuleSummary(const TerrainProperties &p)
{
	std::vector<std::string> parts;
	if (p.walkable)
		parts.push_back(fe::tr("[terrain rule walkable]"));
	else if (p.swimmable)
		parts.push_back(fe::tr("[terrain rule swimmable]"));
	else
		parts.push_back(fe::tr("[terrain rule impassable]"));
	if (!p.flyable)
		parts.push_back(fe::tr("[terrain rule no flying]"));
	if (p.buildable)
		parts.push_back(fe::tr("[terrain rule buildable]"));
	if ((p.walkable || p.swimmable) && p.groundSpeedQ8 != 256)
		parts.push_back(FormattableString(fe::tr("[terrain rule speed %0]")).arg(int(p.groundSpeedQ8) * 100 / 256));
	if (p.resourcesGrow && p.allowedResources)
		parts.push_back(fe::tr(p.swimmable ? "[terrain rule algae grow]" : "[terrain rule crops grow]"));
	if (terrainProvidesFertility(p))
		parts.push_back(fe::tr("[terrain rule irrigates]"));
	if (p.inhibitionQ8)
		parts.push_back(fe::tr("[terrain rule stops nearby growth]"));
	if (p.projectileBlocks)
		parts.push_back(fe::tr("[terrain rule blocks shots]"));
	if (p.groundHealthQ8 < 0)
		parts.push_back(fe::tr("[terrain rule hurts walkers]"));
	if (p.airHealthQ8 < 0)
		parts.push_back(fe::tr("[terrain rule hurts fliers]"));
	std::string text;
	for (const auto &part : parts)
		text += (text.empty() ? "" : "  \u00b7  ") + part;
	return text;
}
// "Terrain 2" sorts before "Terrain 10": digit runs compare by value.
bool naturalLess(const std::string &a, const std::string &b)
{
	std::size_t i = 0, j = 0;
	while (i < a.size() && j < b.size())
	{
		if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j])))
		{
			std::size_t ei = i, ej = j;
			while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
			while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
			const auto da = a.substr(i, ei - i), db = b.substr(j, ej - j);
			const auto ta = da.substr(std::min(da.find_first_not_of('0'), da.size() - 1));
			const auto tb = db.substr(std::min(db.find_first_not_of('0'), db.size() - 1));
			if (ta.size() != tb.size()) return ta.size() < tb.size();
			if (ta != tb) return ta < tb;
			i = ei; j = ej;
			continue;
		}
		if (a[i] != b[j]) return a[i] < b[j];
		++i; ++j;
	}
	return a.size() - i < b.size() - j;
}
} // namespace

int TerrainPaletteDialog::groupFor(std::string_view key)
{
	for (unsigned g = 0; g < TERRAIN_GROUP_COUNT; ++g)
		if (key == terrainGroupDefinition(TerrainGroup(g)).key)
			return int(g);
	return -1;
}

bool terrainBrushOffered(const TerrainRegistry &registry, TerrainType type)
{
	if (!registry.presentation(type).editorSelectable)
		return false;
	const auto experiment = terrainExperiment(type);
	return !experiment || globalContainer->settings.experiments.has(*experiment);
}

std::vector<TerrainType> offeredTerrainBrushes(const TerrainRegistry &registry, TerrainGroup group)
{
	std::vector<TerrainType> brushes;
	for (unsigned id = 0; id < TERRAIN_COUNT; ++id)
		if (terrainGroup(TerrainType(id)) == group && terrainBrushOffered(registry, TerrainType(id)))
			brushes.push_back(TerrainType(id));
	return brushes;
}

TerrainPaletteDialog::~TerrainPaletteDialog() = default;

void TerrainPaletteDialog::focusOnOpen()
{
	// Headless editors never attach the dialog, so there is nothing to scroll.
	if (focus < 0 || globalContainer->runNoX)
		return;
	const auto brushes = offeredTerrainBrushes(*registry, TerrainGroup(focus));
	if (brushes.empty())
		return;
	// The group's heading is the row above its first brush: aligning that brush's
	// section column to the top shows the heading, the rules and the cards.
	host().layoutIfNeeded();
	host().scrollToTop("terrain/section/" + std::string(terrainGroupDefinition(TerrainGroup(focus)).key));
}

GAGCore::DrawableSurface *TerrainPaletteDialog::preview(TerrainType type)
{
	auto found = previews.find(type);
	if (found != previews.end())
		return found->second.get();
	auto &compositor = globalContainer->terrainCompositor();
	const auto &catalog = compositor.catalog();
	const auto appearance = registry->appearance(type);
	const auto binding = catalog.bindings.find(terrainPresentation(appearance).name);
	auto surface = std::make_unique<GAGCore::DrawableSurface>(64, 64);
	// Over the material's preview colour so the transparent ocean and translucent
	// water still read; runtime types keep their saved colours.
	const auto &colours = registry->presentation(type);
	GAGCore::Color fill(colours.preview.r, colours.preview.g, colours.preview.b);
	if (binding != catalog.bindings.end() && unsigned(type) < TERRAIN_COUNT)
	{
		const auto &material = catalog.materials[binding->second];
		fill = GAGCore::Color(material.preview[0], material.preview[1], material.preview[2]);
	}
	surface->drawFilledRect(0, 0, 64, 64, fill);
	// Swimmable brushes sit on the shared ocean, as they do on the map.
	if (registry->properties(appearance).swimmable)
		if (auto *ocean = GAGCore::Toolkit::getSprite(TerrainOceanBackdrop.sprite))
			surface->drawSprite(0, 0, ocean, TerrainOceanBackdrop.firstFrame);
	if (binding != catalog.bindings.end() && !catalog.materials[binding->second].ocean)
	{
		GAGCore::DrawableSurface texture(64, 64);
		TerrainVisual::Recipe recipe;
		recipe.samples.fill(binding->second);
		recipe.width = recipe.height = 1;
		compositor.compose(recipe, texture.getSDLSurface(), 0, 0, 2);
		surface->drawSurface(0, 0, &texture);
	}
	auto *result = surface.get();
	previews.emplace(type, std::move(surface));
	return result;
}

Element TerrainPaletteDialog::build(const Presentation &p)
{
	// Prepare the same texture set the map renderer uses, so the swatches match
	// the map and the renderer is not forced to reload its sources afterwards.
	auto &compositor = globalContainer->terrainCompositor();
	const bool gpu = globalContainer->gfx->getOptionFlags() &
					 (GAGCore::GraphicContext::USEGPU | GAGCore::GraphicContext::PORTABLEGPU);
	compositor.prepare(gpu, 0);
	const int tile = p.pt(56);
	// Uniform brush cards that never stretch, so a one-brush group reads the same
	// as a full row.
	const int cardWidth = p.pt(118);
	auto swatch = [&](TerrainType type) -> Element
	{
		fe::ImageOptions options;
		options.fit = true;
		options.size = fe::Size{tile, tile};
		return fe::image(preview(type), options);
	};
	auto brush = [&](TerrainType type, const std::string &label, const std::string &caption) -> Element
	{
		fe::ButtonOptions options;
		options.selected = current && *current == type;
		options.minHeight = 1;
		options.accessibleLabel = label;
		std::vector<Element> body{fe::center(swatch(type)),
								  fe::label(label, {fe::FontRole::Support, false, fe::TextAlign::Center})};
		if (!caption.empty())
			body.push_back(fe::paragraph(caption, {fe::FontRole::Caption, true, fe::TextAlign::Center}));
		auto content = fe::padding(fe::Insets::all(p.pt(6)), fe::column(std::move(body), {p.pt(4)}));
		const auto id = unsigned(type);
		return fe::width(cardWidth, fe::stack({fe::button("terrain/" + registry->key(type), "", [this, id] { finish(int(id)); }, options),
											   std::move(content)}));
	};
	// Rows of equally sized cards packed from the left, so a one-brush group and a
	// full row share the same rhythm.
	const int gap = p.pt(6);
	const int available = std::min(p.pt(760), p.safe.w) - p.pt(44);
	const int columns = std::clamp(available / (cardWidth + gap), 2, 6);
	std::vector<Element> sections;
	auto section = [&](const std::string &key, const std::string &title, const std::string &rules,
					   std::vector<Element> brushes)
	{
		if (brushes.empty())
			return;
		// The keyed, invisible canvas lets a group brush scroll its heading to the top.
		std::vector<Element> parts{fe::canvas("terrain/section/" + key, {1, 1}, [](fe::Canvas &, fe::Rect, const fe::Frame &) {}),
								   fe::heading(title)};
		if (!rules.empty())
			parts.push_back(fe::caption(rules));
		for (std::size_t first = 0; first < brushes.size(); first += std::size_t(columns))
		{
			const auto last = std::min(brushes.size(), first + std::size_t(columns));
			std::vector<Element> cells(std::make_move_iterator(brushes.begin() + long(first)),
									   std::make_move_iterator(brushes.begin() + long(last)));
			parts.push_back(fe::row(std::move(cells), {gap}));
		}
		sections.push_back(fe::column(std::move(parts), {p.pt(4)}));
	};
	// Classic ground first: water, sand and grass each carry their own rules.
	{
		std::vector<Element> brushes;
		for (auto type : {WATER, SAND, GRASS})
			if (terrainBrushOffered(*registry, type))
				brushes.push_back(brush(type, fe::tr(registry->presentation(type).label),
										terrainRuleSummary(registry->properties(type))));
		section("classic", fe::tr("[terrain group classic]"), "", std::move(brushes));
	}
	// Catalogue groups in table order; every member shares the group's rules.
	for (unsigned g = 0; g < TERRAIN_GROUP_COUNT; ++g)
	{
		const auto group = TerrainGroup(g);
		const auto &definition = terrainGroupDefinition(group);
		if (!definition.paletteVisible || terrainGroupIsClassic(group))
			continue;
		std::vector<Element> brushes;
		for (auto type : offeredTerrainBrushes(*registry, group))
			brushes.push_back(brush(type, fe::tr(registry->presentation(type).label), ""));
		section(definition.key, fe::tr(definition.label), terrainRuleSummary(definition.properties), std::move(brushes));
	}
	// The map's imported definitions, in natural name order with their own rules.
	{
		std::vector<TerrainType> custom;
		for (unsigned id = TERRAIN_COUNT; id < registry->size(); ++id)
			if (terrainBrushOffered(*registry, TerrainType(id)))
				custom.push_back(TerrainType(id));
		// Natural name order; equal names fall back to the stable key order.
		std::stable_sort(custom.begin(), custom.end(), [&](TerrainType a, TerrainType b)
						 {
							 const std::string &la = registry->presentation(a).label, &lb = registry->presentation(b).label;
							 if (naturalLess(la, lb)) return true;
							 if (naturalLess(lb, la)) return false;
							 return registry->key(a) < registry->key(b);
						 });
		std::vector<Element> brushes;
		for (auto type : custom)
			brushes.push_back(brush(type, registry->presentation(type).label, terrainRuleSummary(registry->properties(type))));
		section("custom", fe::tr("[terrain group custom]"), "", std::move(brushes));
	}
	bool hidden = false;
	for (unsigned g = 0; g < TERRAIN_GROUP_COUNT && !hidden; ++g)
	{
		const auto group = TerrainGroup(g);
		if (terrainGroupDefinition(group).paletteVisible && !terrainGroupIsClassic(group))
		{
			bool any = false;
			for (unsigned id = 0; id < TERRAIN_COUNT && !any; ++id)
				any = terrainGroup(TerrainType(id)) == group;
			hidden = any && offeredTerrainBrushes(*registry, group).empty();
		}
	}
	return fe::footer(
		fe::column({fe::paragraph(fe::tr("[Terrain palette]"), {fe::FontRole::Heading}),
					fe::caption(fe::tr(hidden ? "[terrain palette experiments hint]" : "[terrain palette hint]")),
					fe::scroll("terrain/scroll", fe::column(std::move(sections), {p.pt(14)}))},
				   {p.pt(8)}),
		dialogActions({{"cancel", fe::tr("[Cancel]"), [this] { finish(-1); }, false, SDLK_ESCAPE}},
					  p));
}
