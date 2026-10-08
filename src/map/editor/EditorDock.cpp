// SPDX-License-Identifier: GPL-3.0-or-later
// The editor dock's frame: placement at the right edge, header (menu, minimap,
// tabs, search), footer (brush shape, Add/Delete) and rebuild triggers. The
// catalogue sections are in EditorDockTerrain.cpp; teams and the inspector in
// EditorDockObjects.cpp.

#include "EditorDock.h"
#include "EditorDockInternal.h"
#include "BrushCatalog.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "MapEditInspector.h"
#include <FormatableString.h>
#include <algorithm>
#include <sstream>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

namespace
{
struct TabInfo
{
	const char *key, *label;
	Glob2UI::UIIcon icon;
};
constexpr TabInfo tabs[] = {{"terrain", "[Terrain]", Glob2UI::UIIcon::Terrain},
							{"resources", "[Resources]", Glob2UI::UIIcon::Resources},
							{"buildings", "[Buildings]", Glob2UI::UIIcon::Buildings},
							{"flags", "[dock flags and units]", Glob2UI::UIIcon::Campaign},
							{"teams", "[Teams]", Glob2UI::UIIcon::Users}};
static_assert(std::size(tabs) == std::size_t(EditorDock::Tab::Count));
} // namespace

EditorDock::EditorDock(MapEdit &owner) : InGameDialog(Glob2UI::Surface::Editor), editor(owner)
{
	syncTabFromEditor();
}

EditorDock::~EditorDock() = default;

int EditorDock::widthFor(const Presentation &p)
{
	const int viewport = p.viewport.w;
	if (viewport <= 0)
		return 0;
	const int limit = std::max(0, int(viewport * 0.4));
	const int lower = std::min(240, limit);
	// 300 points, growing with very wide windows so cards keep their size
	// relative to the map; never more than 40% of the window.
	const int preferred = std::max(p.pt(p.touch ? 320 : 300), int(viewport * 0.2));
	return std::clamp(preferred, lower, limit);
}

int EditorDock::width()
{
	if (!attached)
		return 0;
	refreshPresentation();
	return widthFor(presentation());
}

GAGGUI::ui::Rect EditorDock::rect()
{
	const int w = width();
	const auto &viewport = presentation().viewport;
	return {viewport.x + viewport.w - w, viewport.y, w, viewport.h};
}

bool EditorDock::contains(int x, int y)
{
	return attached && rect().contains(GAGGUI::ui::Point{x, y});
}

bool EditorDock::interacting()
{
	return attached && (host().interacting() || host().animating());
}

bool EditorDock::editingText()
{
	return attached && !host().editing().empty();
}

EditorDock::Tab EditorDock::tabFor(BrushSection section)
{
	switch (section)
	{
	case BrushSection::Terrain:
	case BrushSection::Areas:
		return Tab::Terrain;
	case BrushSection::Resources:
		return Tab::Resources;
	case BrushSection::Buildings:
		return Tab::Buildings;
	default:
		return Tab::FlagsAndUnits;
	}
}

bool EditorDock::inspecting() const
{
	return editor.panelMode == MapEdit::UnitEditor || editor.panelMode == MapEdit::BuildingEditor;
}

// Keeps the tab in step with the editor's panel mode, which keyboard shortcuts,
// right-click and actions change too. Terrain and Resources share Terrain mode.
void EditorDock::syncTabFromEditor()
{
	switch (editor.panelMode)
	{
	case MapEdit::AddBuildings:
		currentTab = Tab::Buildings;
		break;
	case MapEdit::AddFlagsAndZones:
		currentTab = Tab::FlagsAndUnits;
		break;
	case MapEdit::Terrain:
		if (currentTab != Tab::Resources)
			currentTab = Tab::Terrain;
		break;
	case MapEdit::Teams:
		currentTab = Tab::Teams;
		break;
	default:
		return;
	}
	lastTab = currentTab;
}

void EditorDock::showTab(Tab tab, bool keepBrush)
{
	static constexpr const char *actions[] = {"switch to terrain view", "switch to terrain view",
											  "switch to building view", "switch to flag view",
											  "switch to teams view"};
	// Leaving an inspector, or changing tabs, goes through the editor's views so
	// the legacy widget groups and the phone presentation stay consistent.
	const Tab previous = currentTab;
	const bool wasInspecting = inspecting();
	currentTab = tab;
	lastTab = tab;
	const bool sameMode = !wasInspecting && (previous == tab || (int(previous) <= 1 && int(tab) <= 1));
	if (!sameMode && keepBrush && !wasInspecting)
	{
		static constexpr MapEdit::PanelMode modes[] = {MapEdit::Terrain, MapEdit::Terrain, MapEdit::AddBuildings,
													   MapEdit::AddFlagsAndZones, MapEdit::Teams};
		static constexpr const char *groups[] = {"terrain view", "terrain view", "building view", "flag view",
												 "teams view"};
		editor.panelMode = modes[int(tab)];
		editor.enableOnlyGroup(groups[int(tab)]);
	}
	else if (!sameMode)
		editor.performAction(actions[int(tab)]);
	invalidate();
}

void EditorDock::revealGroup(BrushSection section, std::string_view groupKey)
{
	const Tab tab = tabFor(section);
	if (currentTab != tab || inspecting())
		showTab(tab, true);
	query.clear();
	if (!groupKey.empty())
	{
		const std::string key = std::string(brushSectionKey(section)) + "/" + std::string(groupKey);
		editor.dockCollapsed.erase(key);
		pendingReveal = "dock/section/" + key;
	}
	invalidate();
	if (attached && !pendingReveal.empty())
	{
		host().layoutIfNeeded();
		if (host().find(pendingReveal))
			host().scrollToTop(pendingReveal);
		pendingReveal.clear();
	}
}

void EditorDock::focusSearch()
{
	if (currentTab == Tab::Teams || inspecting())
		showTab(Tab::Terrain);
	if (!attached)
		return;
	host().layoutIfNeeded();
	if (host().find("dock/search"))
	{
		host().focus("dock/search", true);
		host().beginEditing("dock/search");
	}
}

void EditorDock::setSearch(const std::string &text)
{
	if (text == query)
		return;
	query = text;
	invalidate();
}

std::string EditorDock::modelSignature() const
{
	std::ostringstream s;
	s << editor.currentBrushId() << '|' << int(editor.panelMode) << '|' << int(editor.selectionMode) << '|'
	  << editor.team << '|' << (editor.view.scene ? editor.view.scene->entities.teamCount : 0) << '|' << editor.buildingLevel << '|'
	  << editor.buildingLevelCount << '|' << editor.placingUnitLevel << '|' << editor.brush.getFigure() << '|'
	  << editor.brush.getType() << '|' << editor.brush.addRemoveIsEnabled() << '|' << editor.isFertilityOn << editor.fertilityOverlayStale() << '|'
	  << editor.selectedUnitGID << '|' << editor.selectedBuildingGID << '|' << editor.areaNumber->getIndex() << '|'
	  << (editor.view.scene ? editor.view.scene->map.getAreaName(editor.areaNumber->getIndex()) : std::string{}) << '|' << editor.dockCollapsed.size();
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		s << (editor.view.scene && i < editor.view.scene->entities.teamCount ? '1' : '0');
	return s.str();
}

void EditorDock::onUpdate(Uint32)
{
	syncTabFromEditor();
	const auto revision = editor.catalogRevision();
	auto signature = modelSignature();
	if (revision != builtRevision || signature != builtSignature)
	{
		builtRevision = revision;
		builtSignature = std::move(signature);
		invalidate();
	}
	if (!pendingReveal.empty() && attached)
	{
		host().layoutIfNeeded();
		if (host().find(pendingReveal))
			host().scrollToTop(pendingReveal);
		pendingReveal.clear();
	}
}

GAGGUI::ui::Rect EditorDock::available(const Presentation &p, const GAGGUI::ui::Metrics &)
{
	const int w = widthFor(p);
	const int pad = p.pt(p.touch ? 10 : 8);
	GAGGUI::ui::Rect area{p.viewport.x + p.viewport.w - w, p.viewport.y, w, p.viewport.h};
	// Content stays inside platform gutters; the panel itself reaches the edges.
	area = area.intersect(p.safe);
	return area.inset(pad);
}

GAGGUI::ui::Rect EditorDock::place(GAGGUI::ui::Size, GAGGUI::ui::Rect area) { return area; }

void EditorDock::paintPanel(GAGGUI::ui::Canvas &canvas, GAGGUI::ui::Rect)
{
	const auto &p = presentation();
	const int w = widthFor(p);
	const GAGGUI::ui::Rect panel{p.viewport.x + p.viewport.w - w, p.viewport.y, w, p.viewport.h};
	const auto &palette = theme().palette;
	canvas.fillRect(panel.translated(-3, 0).intersect({panel.x - 3, panel.y, 3, panel.h}), palette.shadow.applyAlpha(90));
	canvas.fillRect(panel, palette.panel.applyAlpha(250));
	canvas.fillRect({panel.x, panel.y, std::max(1, p.pt(1)), panel.h}, theme().hud.border);
}

Element EditorDock::header(const Presentation &p)
{
	const int inner = std::max(0, widthFor(p) - 2 * p.pt(p.touch ? 10 : 8));
	const int gap = p.pt(8);
	// The minimap is the largest square that leaves a button column beside it and
	// keeps most of a short window for the brushes.
	const int buttons = p.pt(p.touch ? 104 : 96);
	const int side = std::clamp(std::min({inner - buttons - gap, p.pt(150), int(p.viewport.h * 0.22)}), p.pt(56), p.pt(150));
	auto minimap = fe::canvas(
		"dock/minimap", {side, side},
		[this](fe::Canvas &canvas, fe::Rect r, const fe::Frame &)
		{
			minimapBounds = r;
			canvas.fillRect(r, GAGCore::Color(0, 0, 0));
			if (globalContainer->runNoX)
				return;
			editor.minimap.setPlacement(r.x, r.y, std::min(r.w, r.h));
			editor.drawMiniMap();
		},
		{.pointer =
			 [this](fe::PointerPhase phase, fe::Point local, fe::Host &)
			 {
				 if (phase == fe::PointerPhase::Cancel || phase == fe::PointerPhase::Up)
					 return;
				 editor.centerViewOnMinimap(minimapBounds.x + local.x, minimapBounds.y + local.y);
			 },
		 .accessibleText = fe::tr("[dock minimap]")});
	fe::ButtonOptions menuOptions;
	menuOptions.tooltip = fe::tr("[Menu]");
	menuOptions.icon = fe::uiIcon(fe::UIIcon::More);
	auto menu = fe::button("dock/menu", fe::tr("[Menu]"), [this] { editor.performAction("open menu screen"); }, menuOptions);
	std::ostringstream size;
	size << (editor.view.scene ? editor.view.scene->map.getW() : 0) << " x " << (editor.view.scene ? editor.view.scene->map.getH() : 0);
	auto side_column = fe::column({menu, fe::caption(size.str())}, {p.pt(6)});
	auto top = fe::row({fe::sized({side, side}, minimap), fe::expanded(side_column)}, {gap, fe::CrossAlign::Start});

	std::vector<Element> tabButtons;
	const Tab shown = inspecting() ? lastTab : currentTab;
	for (int i = 0; i < int(Tab::Count); ++i)
	{
		fe::ButtonOptions options;
		options.selected = !inspecting() && Tab(i) == shown;
		options.icon = fe::uiIcon(tabs[i].icon);
		options.iconSize = p.touch ? 24 : 20;
		options.accessibleLabel = fe::tr(tabs[i].label);
		options.tooltip = options.accessibleLabel;
		options.minHeight = p.touch ? 48 : 34;
		tabButtons.push_back(fe::expanded(
			fe::button(std::string("dock/tab/") + tabs[i].key, "", [this, i] { showTab(Tab(i)); }, options)));
	}
	std::vector<Element> parts{top, fe::row(std::move(tabButtons), {p.pt(4)})};
	const bool roomy = p.viewport.h >= p.pt(640);
	if (!inspecting())
	{
		// The tab's name under the icon row; short windows rely on the tooltips
		// and section headings instead.
		if (roomy)
			parts.push_back(fe::heading(fe::tr(tabs[int(currentTab)].label)));
		if (currentTab != Tab::Teams)
		{
			fe::TextFieldOptions search;
			search.placeholder = fe::tr("[dock search brushes]");
			parts.push_back(fe::textField("dock/search", query, [this](const std::string &v) { setSearch(v); }, search));
		}
	}
	return fe::column(std::move(parts), {p.pt(6)});
}

Element EditorDock::brushControls(const Presentation &p)
{
	auto &brush = editor.brush;
	const bool active = brush.getType() != BrushTool::MODE_NONE;
	std::vector<Element> shapes;
	const int cell = p.pt(p.touch ? 48 : 30);
	for (unsigned i = 0; i < BrushTool::BRUSH_COUNT; ++i)
	{
		fe::ButtonOptions options;
		options.selected = active && brush.getFigure() == i;
		options.minHeight = p.touch ? 48 : 30;
		options.accessibleLabel = GAGCore::FormattableString(fe::tr("[dock brush shape %0]")).arg(i + 1);
		options.tooltip = options.accessibleLabel;
		const int frame = int(2 + i);
		auto picture = fe::canvas("dock/brush/picture/" + std::to_string(i), {p.pt(24), p.pt(24)},
								  [frame](fe::Canvas &canvas, fe::Rect r, const fe::Frame &)
								  {
									  const int side = std::min(r.w, r.h) - 4;
									  EditorDockPaint::spriteFit(canvas, {r.x + (r.w - side) / 2, r.y + (r.h - side) / 2, side, side},
																 globalContainer->brush, frame, 1.0);
								  });
		shapes.push_back(fe::stack({fe::button("dock/brush/shape/" + std::to_string(i), "",
											   [this, i]
											   {
												   editor.brush.setFigure(i);
												   invalidate();
											   },
											   options),
									picture}));
	}
	fe::WrapOptions grid;
	grid.gap = p.pt(3);
	grid.minChildWidth = cell;
	grid.maxColumns = 8;
	std::vector<Element> parts;
	if (p.viewport.h >= p.pt(560))
		parts.push_back(fe::caption(fe::tr("[dock brush]")));
	if (brush.addRemoveIsEnabled())
	{
		const int mode = brush.getType() == BrushTool::MODE_DEL ? 1 : 0;
		parts.push_back(fe::segments("dock/brush/mode", {fe::tr("[dock brush paint]"), fe::tr("[dock brush erase]")},
									 active ? mode : -1,
									 [this](int choice)
									 {
										 editor.brush.setType(choice == 1 ? BrushTool::MODE_DEL : BrushTool::MODE_ADD);
										 invalidate();
									 },
									 {active, active}));
	}
	parts.push_back(fe::wrap(std::move(shapes), grid));
	return fe::column(std::move(parts), {p.pt(4)});
}

Element EditorDock::footerControls(const Presentation &p)
{
	if (inspecting() || !query.empty())
		return nullptr;
	if (currentTab == Tab::Terrain || currentTab == Tab::Resources || currentTab == Tab::FlagsAndUnits)
		return fe::column({fe::divider(), brushControls(p)}, {p.pt(6)});
	return nullptr;
}

Element EditorDock::body(const Presentation &p)
{
	if (inspecting())
		return inspector(p);
	if (!query.empty())
		return catalogueSections(p, {BrushSection::Terrain, BrushSection::Resources, BrushSection::Buildings,
									 BrushSection::Flags, BrushSection::Units, BrushSection::Zones,
									 BrushSection::Areas, BrushSection::Tools});
	switch (currentTab)
	{
	case Tab::Terrain:
		return catalogueSections(p, {BrushSection::Terrain, BrushSection::Areas});
	case Tab::Resources:
		return catalogueSections(p, {BrushSection::Resources});
	case Tab::Buildings:
		return catalogueSections(p, {BrushSection::Buildings});
	case Tab::FlagsAndUnits:
		return catalogueSections(p, {BrushSection::Flags, BrushSection::Zones, BrushSection::Units, BrushSection::Tools});
	case Tab::Teams:
	default:
		return teamsTab(p);
	}
}

Element EditorDock::build(const Presentation &p)
{
	builtRevision = editor.catalogRevision();
	builtSignature = modelSignature();
	builtBrush = editor.currentBrushId();
	// A different tab or inspected object starts at the top of the list.
	std::string bodyIdentity = std::to_string(int(currentTab)) + "/" + std::to_string(int(editor.panelMode)) + "/" +
							   std::to_string(editor.selectedBuildingGID) + "/" + std::to_string(editor.selectedUnitGID) +
							   "/" + (query.empty() ? "" : "search");
	if (bodyIdentity != builtBody)
	{
		host().state("dock/scroll").scroll = 0;
		builtBody = std::move(bodyIdentity);
	}
	auto content = fe::scroll("dock/scroll", body(p), {.shrinkToContent = false});
	auto footer = footerControls(p);
	auto main = footer ? fe::footer(std::move(content), std::move(footer)) : std::move(content);
	return fe::column({header(p), fe::expanded(std::move(main))}, {p.pt(8)});
}

void EditorDock::select(const BrushEntry &entry)
{
	if (entry.locked)
		return;
	// Selecting from a search result lands on the entry's tab, like a palette.
	const Tab tab = tabFor(entry.section);
	if (tab != currentTab && !(int(tab) <= 1 && int(currentTab) <= 1))
		showTab(tab);
	else if (tab != currentTab)
		currentTab = lastTab = tab;
	editor.performAction(entry.action);
	invalidate();
}
