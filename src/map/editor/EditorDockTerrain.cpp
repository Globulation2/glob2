// SPDX-License-Identifier: GPL-3.0-or-later
// The editor dock's catalogue: one collapsible section per brush group, each a
// grid of labelled swatch cards; locked groups offer to enable their experiment
// for the map; the Areas section carries the script-area number and name.

#include "BuildingType.h"
#include "EditorDock.h"
#include "EditorDockInternal.h"
#include "ExperimentalFeatures.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "Unit.h"
#include "render/UnitAnimation.h"
#include "render/UnitSkin.h"
#include "resource/ResourceRegistry.h"
#include <FormatableString.h>
#include <algorithm>
#include <cctype>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

namespace
{
std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return text;
}

std::string translatedOr(const std::string &key, const std::string &fallback)
{
	const auto *strings = GAGCore::Toolkit::getStringTable();
	if (strings && strings->doesStringExist(key))
		return strings->getString(key);
	return fallback;
}

// The experiment's player-facing name, as the catalogue's tooltips show it.
std::string experimentName(MapEdit &editor, const std::string &key)
{
	if (const auto *strings = GAGCore::Toolkit::getStringTable(); strings && strings->doesStringExist("[experiment " + key + "]"))
		return strings->getString("[experiment " + key + "]");
	for (const auto &definition : registeredExperimentDefinitions())
		if (definition.key == key)
			return definition.label;
	for (const auto &definition : editor.view.scene->map.resourceRegistry().experiments())
		if (definition.key == key)
			return definition.label;
	return key;
}

std::string validOnLine(MapEdit &editor, const BrushEntry &entry)
{
	if (entry.validOn.empty())
		return translatedOr("[brush valid nowhere]", "");
	// The card names the first two terrains; the tooltip lists them all.
	std::string names;
	std::size_t shown = 0;
	for (const auto type : entry.validOn)
	{
		if (shown == 2)
		{
			names += " +" + std::to_string(entry.validOn.size() - shown);
			break;
		}
		++shown;
		if (!names.empty())
			names += ", ";
		const auto &presentation = editor.view.scene->map.terrainPresentation(type);
		names += unsigned(type) < TERRAIN_COUNT ? translatedOr(presentation.label, presentation.label)
												: std::string(presentation.label);
	}
	return GAGCore::FormattableString(fe::tr("[brush valid on %0]")).arg(names);
}
} // namespace

bool EditorDock::matchesSearch(const BrushEntry &entry) const
{
	if (query.empty())
		return true;
	const auto needle = lower(query);
	return lower(entry.label).find(needle) != std::string::npos || lower(entry.id).find(needle) != std::string::npos;
}

Element EditorDock::card(const Presentation &p, const BrushEntry &entry)
{
	const int tile = p.pt(p.touch ? 52 : 44);
	fe::ButtonOptions options;
	options.selected = !entry.locked && entry.id == editor.currentBrushId();
	options.enabled = !entry.locked;
	options.minHeight = 1;
	options.accessibleLabel = entry.label;
	options.tooltip = entry.tooltip.empty() ? entry.label : entry.tooltip;
	Element picture;
	if (auto *surface = editor.brushSwatches().get(entry, tile))
	{
		fe::ImageOptions image;
		image.fit = true;
		image.size = fe::Size{tile, tile};
		if (entry.locked)
			image.tint = GAGCore::Color(150, 150, 150, 150);
		picture = fe::image(surface, image);
	}
	else
	{
		const auto kind = entry.swatch.kind;
		const std::string key = entry.swatch.key;
		const bool locked = entry.locked;
		picture = fe::canvas(
			"dock/picture/" + entry.id, {tile, tile},
			[this, kind, key, locked](fe::Canvas &canvas, fe::Rect r, const fe::Frame &)
			{
				const auto teamColor = editor.view.scene && editor.team < editor.view.scene->entities.teamCount
                    ? presentationColor(editor.view.scene->entities.teams[editor.team].color) : GAGCore::Color(255, 255, 255);
				switch (kind)
				{
				case BrushSwatch::Kind::Building:
				{
					const int id = editor.displayedBuildingSelectionType(key);
					const auto *type = id >= 0 ? &(*editor.view.scene->buildingTypes)[id] : nullptr;
					if (!type)
						return;
					auto *sprite = type->miniSpriteImage >= 0 ? type->miniSpritePtr : type->gameSpritePtr;
					const int frame = type->miniSpriteImage >= 0 ? type->miniSpriteImage : type->gameSpriteImage;
					if (!sprite)
						return;
					sprite->setBaseColor(teamColor);
					EditorDockPaint::spriteFit(canvas, r, sprite, frame, 1.5);
					break;
				}
				case BrushSwatch::Kind::Unit:
				{
					const int type = key == "worker" ? WORKER : key == "explorer" ? EXPLORER : WARRIOR;
					auto *sprite = globalContainer->units;
					sprite->setBaseColor(teamColor);
					EditorDockPaint::spriteFit(canvas, r, sprite,
											   unitAnimationFrame(g_unitSkins[type].startImage[STOP_WALK], 0, 0), 1.5);
					break;
				}
				case BrushSwatch::Kind::Zone:
				{
					const int frame = key == "forbidden" ? 13 : key == "guard" ? 14 : key == "clearing" ? 25 : 58;
					EditorDockPaint::spriteFit(canvas, r, globalContainer->gamegui, frame, 1.5);
					break;
				}
				default:
					break;
				}
				if (locked)
					canvas.fillRect(r, GAGCore::Color(30, 20, 40, 120));
			});
		if (kind == BrushSwatch::Kind::Tool)
		{
			const auto icon = key == "delete"	   ? fe::UIIcon::Close
							  : key == "no-growth" ? fe::UIIcon::Resources
												   : fe::UIIcon::Code;
			fe::IconOptions iconOptions;
			iconOptions.size = p.touch ? 34 : 30;
			if (key == "delete" || key == "no-growth")
				iconOptions.color = theme().palette.danger;
			picture = fe::sized({tile, tile}, fe::center(fe::icon(fe::uiIcon(icon), iconOptions)));
		}
	}
	std::vector<Element> body{fe::center(fe::sized({tile, tile}, picture)),
							  fe::label(entry.label, {fe::FontRole::Support, entry.locked, fe::TextAlign::Center})};
	if (entry.section == BrushSection::Resources)
		body.push_back(fe::paragraph(validOnLine(editor, entry), {fe::FontRole::Caption, true, fe::TextAlign::Center}));
	auto content = fe::padding(fe::Insets::all(p.pt(4)), fe::column(std::move(body), {p.pt(2)}));
	std::vector<Element> layers{
		fe::button("brush/" + entry.id, "", [this, entry] { select(entry); }, options), std::move(content)};
	if (entry.locked)
	{
		fe::IconOptions lockOptions;
		lockOptions.size = 16;
		lockOptions.color = theme().palette.muted;
		layers.push_back(fe::align(fe::Alignment::TopRight,
								   fe::padding(fe::Insets::all(p.pt(4)), fe::icon(fe::uiIcon(fe::UIIcon::Lock), lockOptions))));
	}
	return fe::stack(std::move(layers));
}

Element EditorDock::section(const Presentation &p, const BrushGroup &group, std::vector<Element> extra)
{
	const std::string id = std::string(brushSectionKey(group.section)) + "/" + group.key;
	std::vector<Element> cards;
	std::vector<std::string> lockedExperiments;
	for (const auto &entry : group.entries)
	{
		if (!matchesSearch(entry))
			continue;
		cards.push_back(card(p, entry));
		if (entry.locked && !entry.experiment.empty() &&
			std::find(lockedExperiments.begin(), lockedExperiments.end(), entry.experiment) == lockedExperiments.end())
			lockedExperiments.push_back(entry.experiment);
	}
	if (cards.empty() && extra.empty())
		return nullptr;
	// Searching shows every match; collapsing applies to browsing only.
	const bool collapsed = query.empty() && editor.dockCollapsed.count(id);
	fe::ButtonOptions headerOptions;
	headerOptions.flat = true;
	headerOptions.alignLeft = true;
	headerOptions.icon = fe::uiIcon(collapsed ? fe::UIIcon::ChevronRight : fe::UIIcon::ChevronDown);
	headerOptions.iconSize = 16;
	headerOptions.role = fe::FontRole::Heading;
	headerOptions.minHeight = p.touch ? 44 : 28;
	headerOptions.tooltip = group.rules;
	std::string title = group.title.empty() ? std::string(brushSectionKey(group.section)) : group.title;
	// The Resources tab already says "Resources": name the stock group instead.
	if (group.section == BrushSection::Resources && group.key == "base")
		title = fe::tr("[dock resources base]");
	std::vector<Element> parts{fe::button("dock/section/" + id, title,
										  [this, id]
										  {
											  if (!editor.dockCollapsed.erase(id))
												  editor.dockCollapsed.insert(id);
											  invalidate();
										  },
										  headerOptions)};
	if (!collapsed)
	{
		if (!group.rules.empty())
			parts.push_back(fe::paragraph(group.rules, {fe::FontRole::Caption, true}));
		std::vector<Element> enables;
		for (const auto &experiment : lockedExperiments)
		{
			fe::ButtonOptions enable;
			enable.icon = fe::uiIcon(fe::UIIcon::Experiments);
			enable.tooltip = GAGCore::FormattableString(fe::tr("[dock enable experiment hint %0]")).arg(experimentName(editor, experiment));
			enable.minHeight = p.touch ? 48 : 30;
			enables.push_back(fe::button("dock/enable/" + id + (lockedExperiments.size() > 1 ? "/" + experiment : ""),
									   lockedExperiments.size() > 1
										   ? std::string(GAGCore::FormattableString(fe::tr("[dock enable experiment %0]"))
															 .arg(experimentName(editor, experiment)))
										   : fe::tr("[dock enable for map]"),
									   [this, experiment]
									   {
										   editor.enableExperimentForMap(experiment);
										   invalidate();
									   },
									   enable));
		}
		// A wholly locked group leads with its switch; otherwise it follows the
		// usable cards.
		const bool allLocked = std::all_of(group.entries.begin(), group.entries.end(),
										   [](const BrushEntry &entry) { return entry.locked; });
		if (allLocked)
			for (auto &element : enables)
				parts.push_back(std::move(element));
		if (!cards.empty())
		{
			// Resource cards carry a placement line and get wider columns.
			const bool wide = group.section == BrushSection::Resources;
			fe::WrapOptions grid;
			grid.gap = p.pt(4);
			grid.minChildWidth = p.pt(wide ? (p.touch ? 140 : 124) : (p.touch ? 96 : 82));
			// The grid gives a short row its whole width; fillers keep a lone card
			// the size of its neighbours in other sections.
			const int inner = widthFor(p) - 2 * p.pt(p.touch ? 10 : 8) - p.pt(10);
			const int columns = std::max(1, (inner + grid.gap) / (grid.minChildWidth + grid.gap));
			grid.maxColumns = columns;
			while (int(cards.size()) < columns)
				cards.push_back(fe::spacer());
			parts.push_back(fe::wrap(std::move(cards), grid));
		}
		if (!allLocked)
			for (auto &element : enables)
				parts.push_back(std::move(element));
		for (auto &element : extra)
			if (element)
				parts.push_back(std::move(element));
	}
	return fe::column(std::move(parts), {p.pt(4)});
}

Element EditorDock::areaControls(const Presentation &p)
{
	const int area = editor.areaNumber->getIndex();
	fe::StepperOptions number;
	auto stepper = fe::stepper("dock/area/number", area + 1, 1, editor.areaNumber->maximum(),
							   [this](int value)
							   {
								   editor.areaNumber->setIndex(value - 1);
								   editor.areaNameLabel->setLabel(editor.game.map.getAreaName(editor.areaNumber->getIndex()));
								   invalidate();
							   },
							   number);
	std::string name = editor.view.scene ? editor.view.scene->map.getAreaName(area) : std::string{};
	if (name.empty())
		name = fe::tr("[Unnamed Area]");
	fe::ButtonOptions rename;
	rename.alignLeft = true;
	rename.tooltip = fe::tr("[Change Area Name]");
	return fe::column({fe::field(fe::tr("[dock area number]"), stepper, {.controlWidth = 120, .stacked = false}),
					   fe::button("dock/area/name", name, [this] { editor.performAction("open area name"); }, rename)},
					  {p.pt(4)});
}

Element EditorDock::catalogueSections(const Presentation &p, std::initializer_list<BrushSection> sections)
{
	std::vector<Element> parts;
	const auto &catalog = editor.brushCatalog();
	const bool searching = !query.empty();
	for (const auto sectionKind : sections)
	{
		if (!searching && sectionKind == BrushSection::Buildings)
		{
			parts.push_back(teamPicker(p));
			if (editor.buildingLevelCount > 1)
				parts.push_back(fe::field(fe::tr("[dock building level]"),
										  fe::stepper("dock/building/level", editor.buildingLevel + 1, 1, editor.buildingLevelCount,
													  [this](int value)
													  {
														  editor.performAction("switch to building level " + std::to_string(value));
														  invalidate();
													  }),
										  {.controlWidth = 120, .stacked = false}));
		}
		if (!searching && sectionKind == BrushSection::Flags)
			parts.push_back(teamPicker(p));
		for (const auto &group : catalog)
		{
			if (group.section != sectionKind)
				continue;
			std::vector<Element> extra;
			if (!searching && sectionKind == BrushSection::Areas)
				extra.push_back(areaControls(p));
			if (!searching && sectionKind == BrushSection::Units)
				extra.push_back(fe::field(fe::tr("[dock unit level]"),
										  fe::stepper("dock/unit/level", editor.placingUnitLevel + 1, 1, 4,
													  [this](int value)
													  {
														  editor.placingUnitLevel = std::clamp(value - 1, 0, 3);
														  invalidate();
													  }),
										  {.controlWidth = 120, .stacked = false}));
			if (auto element = section(p, group, std::move(extra)))
				parts.push_back(std::move(element));
		}
		if (!searching && sectionKind == BrushSection::Areas)
		{
			std::vector<Element> fertility{fe::toggle("dock/fertility", fe::tr("[Fertility Map]"), editor.isFertilityOn,
													  [this](bool on)
													  {
														  editor.isFertilityOn = on;
														  editor.performAction("compute fertility");
														  invalidate();
													  })};
			if (editor.fertilityOverlayStale())
			{
				// Strokes since the last computation: offer the recomputation here
				// rather than over the map.
				fe::ButtonOptions refresh;
				refresh.icon = fe::uiIcon(fe::UIIcon::Refresh);
				refresh.tooltip = fe::tr("[editor fertility stale]");
				fertility.push_back(fe::hint(fe::tr("[dock fertility stale]")));
				fertility.push_back(fe::button("dock/fertility/refresh", fe::tr("[dock fertility refresh]"),
											   [this]
											   {
												   editor.performAction("refresh fertility");
												   invalidate();
											   },
											   refresh));
			}
			parts.push_back(fe::column(std::move(fertility), {p.pt(4)}));
		}
	}
	if (parts.empty())
		parts.push_back(fe::hint(fe::tr("[dock no brushes match]")));
	return fe::column(std::move(parts), {p.pt(10)});
}
