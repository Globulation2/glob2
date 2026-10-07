// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The desktop and tablet editor's brush browser: a right-docked, always-open
// panel built with the shared UI framework (docs/development/ui-framework.md)
// from the brush catalogue (BrushCatalog.h). It replaces the fixed 160-pixel
// sprite sidebar and the terrain and resource palette dialogs.
//
//   header   menu button, minimap, segmented tabs (Terrain, Resources,
//            Buildings, Flags & units, Teams), optional search field
//   body     one collapsible section per catalogue group: a wrap grid of
//            labelled swatch cards; locked groups offer "Enable for this map";
//            areas, fertility, the team list and the inspector for a selected
//            unit or building
//   footer   brush shape and size, Add/Delete, the active team
//
// Control keys (for harnesses and publishControls diagnostics):
//   dock/menu, dock/minimap, dock/tabs, dock/search, dock/scroll
//   dock/section/<section>/<group>          section header (collapse toggle)
//   dock/enable/<section>/<group>           "Enable for this map" for locked groups
//   brush/<catalogue id>                    a brush card ("brush/terrain/grass")
//   dock/brush/shape/<i>, dock/brush/mode   brush footprint and Add/Delete
//   dock/team/<i>                           active team swatches
//   dock/area/..., dock/fertility, dock/teams/..., dock/inspect/<row>
//
// MapEdit owns at most one dock (MapEdit::createDock/destroyDock); the phone
// presentation (PhoneEditor) never has one. The dock never finishes: it is not
// part of MapEdit::activeDialog() and is drawn beneath any modal dialog.

#include "ui/FrontendUI.h"
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

class MapEdit;
struct BrushEntry;
struct BrushGroup;
enum class BrushSection : std::uint8_t;

class EditorDock : public Glob2UI::InGameDialog
{
  public:
	enum class Tab : int
	{
		Terrain,
		Resources,
		Buildings,
		FlagsAndUnits,
		Teams,
		Count
	};

	explicit EditorDock(MapEdit &editor);
	~EditorDock() override;
	const char *recordingId() const override { return "editor_dock"; }
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;

	// Dock width in logical pixels for a presentation: clamp(300pt, 240, 40% of
	// the viewport), so it follows the interface scale.
	static int widthFor(const Glob2UI::Presentation &p);
	// Current width (0 while not attached to a surface, as in headless runs).
	int width();
	// The dock's rectangle in the editor's logical coordinates.
	GAGGUI::ui::Rect rect();
	bool contains(int x, int y);
	// A press, drag, popup or scroll gesture owned by the dock is in progress, so
	// pointer events must keep going to it even outside its rectangle.
	bool interacting();
	// A text field (search) is editing: keys go to the dock.
	bool editingText();

	Tab tab() const { return currentTab; }
	// Switches tab through the editor's view actions, which drop the active
	// brush like the legacy sidebar did; `keepBrush` keeps it (navigation).
	void showTab(Tab tab, bool keepBrush = false);
	// Switches to the tab holding the group, expands it and scrolls it into view
	// ("open terrain palette <group>", "open resource palette").
	void revealGroup(BrushSection section, std::string_view groupKey);
	// Puts the keyboard in the search field.
	void focusSearch();
	const std::string &search() const { return query; }
	void setSearch(const std::string &text);
	// Bounds of the minimap canvas, for MapEdit's minimap placement.
	GAGGUI::ui::Rect minimapRect() const { return minimapBounds; }

	// Tab of a catalogue section.
	static Tab tabFor(BrushSection section);

  protected:
	void onEscape() override {}
	void onUpdate(Uint32 tick) override;
	GAGGUI::ui::Rect available(const Glob2UI::Presentation &p, const GAGGUI::ui::Metrics &m) override;
	GAGGUI::ui::Rect place(GAGGUI::ui::Size measured, GAGGUI::ui::Rect area) override;
	void paintPanel(GAGGUI::ui::Canvas &canvas, GAGGUI::ui::Rect panel) override;
	bool fillHeight() const override { return true; }

  private:
	MapEdit &editor;
	Tab currentTab = Tab::Buildings;
	std::string query;
	// What the last build saw, to rebuild when the model moves under it.
	std::uint64_t builtRevision = 0;
	std::string builtBrush, builtSignature, builtBody;
	GAGGUI::ui::Rect minimapBounds{};
	std::string pendingReveal;

	// Model signature beyond the catalogue: selection, panel mode, inspector
	// target, team count, area number, toggles. A change triggers a rebuild.
	std::string modelSignature() const;

	Glob2UI::Element header(const Glob2UI::Presentation &p);
	Glob2UI::Element body(const Glob2UI::Presentation &p);
	Glob2UI::Element footerControls(const Glob2UI::Presentation &p);

	// EditorDockTerrain.cpp: catalogue sections (terrain, resources, buildings,
	// flags, units, zones, areas, tools) as swatch-card grids.
	Glob2UI::Element catalogueSections(const Glob2UI::Presentation &p, std::initializer_list<BrushSection> sections);
	Glob2UI::Element section(const Glob2UI::Presentation &p, const BrushGroup &group, std::vector<Glob2UI::Element> extra = {});
	Glob2UI::Element card(const Glob2UI::Presentation &p, const BrushEntry &entry);
	bool matchesSearch(const BrushEntry &entry) const;
	Glob2UI::Element areaControls(const Glob2UI::Presentation &p);
	// EditorDockObjects.cpp: teams tab and the unit/building inspector.
	Glob2UI::Element teamsTab(const Glob2UI::Presentation &p);
	Glob2UI::Element inspector(const Glob2UI::Presentation &p);
	Glob2UI::Element teamPicker(const Glob2UI::Presentation &p);
	Glob2UI::Element brushControls(const Glob2UI::Presentation &p);
	void select(const BrushEntry &entry);
	bool inspecting() const;
	void syncTabFromEditor();
	// Tab that was active before the editor switched to an inspector.
	Tab lastTab = Tab::Buildings;
	bool attached = false;

  public:
	// Called by MapEdit after attach(); width() is 0 until then.
	void markAttached() { attached = true; }
};
