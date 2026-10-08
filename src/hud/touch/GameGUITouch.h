// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <TouchInput.h>
#include <ScrollPhysics.h>
#include <GestureScroll.h>
#include "TouchInteractionSession.h"
#include "TouchDial.h"
#include "BrushHUD.h"
#include "MetricCatalog.h"
#include <SDL3/SDL.h>
#include <string>
#include <optional>
#include <memory>
#include <vector>
namespace GAGGUI::ui
{
class UIDialog;
}
namespace GAGCore
{
class DrawableSurface;
}
#include "sim/EntityRef.h"
class GameGUI;
struct SceneBuildingPanel;
#include "render/scene/SceneEntities.h"
class Building;
class Unit;
class Minimap;
class Order;
class GameGUITouch
{
  public:
    bool needsStatisticsHistory() const { return statsOpen; }
	explicit GameGUITouch(GameGUI &gui);
	~GameGUITouch();
	bool process(SDL_Event &event);
	void cancel(bool preservePreview = false);
	// Per-frame momentum for the map and the HUD panels; `now` is the frame clock.
	void advanceScroll(Uint64 now);
	// Stop the map coasting (a deliberate jump elsewhere is about to move it).
	void stopMapMotion() { mapMotion.interrupt(); }
	// Stop every coasting or bouncing surface where it is.
	void stopScrolling();
	void dismissMapPanels();
	bool scrollAnimating() const;
	void prepareDraw();
	void drawControls();
	void drawKeyboardFocus();
	bool active() const { return touchActive || usesHUD(); }
	bool usesHUD() const;
	//! The phone HUD's regions in window points (status strip, world, actions).
	GAGCore::MobileLayout hudLayout() const { return layout(); }
	GAGCore::ViewRect worldBounds() const { return world(); }
	void drawHUD();
	void drawPanel();
	bool hasPreview() const { return preview.has_value(); }

  private:
	friend class GameGUITouchHarness;
	friend class MobileGalleryGameplay;
	GameGUI &gui;
	// The dialog GameGUI is showing (menu, chat composer or history); the HUD
	// yields to it and never synthesizes its input.
	GAGGUI::ui::UIDialog *activeDialog() const;
	void menuAction(int action);
	std::optional<GAGCore::ViewRect> labelClip;
	std::vector<std::string> pointLines(const std::string &text, double width,
										double textScale = 1.15) const;
	const SceneBuildingPanel *allocationBuilding() const;
	GAGCore::ViewRect allocationRect() const;
	GAGCore::ViewRect panelContent() const;
	void drawAllocation();
	bool showsBuildPalette() const;
	struct PaletteItem
	{
		std::string name;
		bool enabled;
	};
	std::vector<PaletteItem> paletteItems() const;
	GAGCore::ViewRect paletteItemRect(size_t index) const;
	// Compact phones show the palette as a rail rising from the thumb corner;
	// the persistent (Spacious) panel keeps its top-down grid.
	bool paletteRail(const GAGCore::MobileLayout &ui) const;
	int paletteColumns(const GAGCore::MobileLayout &ui) const;
	double paletteScrollSign() const;
	std::optional<PaletteItem> paletteItemAt(GAGCore::ViewPoint point) const;
	std::optional<TouchPlacementSession> placement;
	// Preview contact pans while held; releasing it never commits construction.
	std::optional<TouchPlacementSession> placementHold;
	std::optional<TouchAllocationSession> allocation;
	bool processAllocationPointer(const SDL_Event &event, GAGCore::ViewPoint point);
	TouchStrokeSession stroke;
	std::optional<TouchDeferredStroke> deferredStroke;
	// Zone painting with one thumb: the brush rail, a Pan mode, panning while a
	// held stroke touches a map edge, and undo of the last stroke's changes.
	BrushHUD::Layout brushHUD() const;
	std::vector<GAGCore::ViewRect> brushBarButtons() const; // Forbid, Guard, Clear, [Farm,] Done.
	bool brushPan = false;
	int railTouched = -1;
	std::optional<TouchPlacementSession> strokeHold;
	bool edgePan(GAGCore::ViewPoint point, std::uint64_t &lastUpdate);
	struct ZoneUndo
	{
		std::vector<std::shared_ptr<Order>> orders; // Inverse orders for the changed cells only.
		std::vector<std::pair<size_t, bool>> displayed; // Displayed-view bits to restore.
		int zone = 0;
		std::uint64_t expires = 0;
	};
	std::optional<ZoneUndo> zoneUndo;
	void applyZoneUndo();
	void replayStroke(const TouchStrokeSession &completed);
	bool strokeMatchesTool(const TouchStrokeSession &candidate) const;
	void commitDeferredStroke();
	bool processPalettePointer(const SDL_Event &event, GAGCore::ViewPoint point);
	// Flags move by dragging them; a drag anywhere else still pans the map.
	std::optional<TouchFlagSession> flagDrag;
	const SnapshotBuilding* grabbableFlag(GAGCore::ViewPoint point);
	const SnapshotBuilding* draggedFlag() const;
	void beginFlagDrag(const SnapshotBuilding &flag, TouchPlacementSession::Pointer pointer, GAGCore::ViewPoint point);
	void advanceFlagDrag();
	void releaseFlagDrag(bool restore);
	UnitRef unitAt(GAGCore::ViewPoint point, double reachPoints = 0) const;
	void advancePlacement();
	void updatePlacementPreview(GAGCore::ViewPoint point);
	bool commitPlacement();
	std::unique_ptr<Minimap> hudMinimap;
	struct HudLayout
	{
		GAGCore::ViewRect minimap, stats, identity;
		int columns;
	};
	HudLayout hudLayout(const GAGCore::MobileLayout &ui) const;
	GAGCore::ViewRect statRect(const HudLayout &hud, int index) const;
	// The speed chevrons' tap target (the last stat cell); empty where speed is fixed.
	GAGCore::ViewRect speedRect() const;
	GAGCore::ViewRect minimapRect() const;
	void drawMinimap();
	void navigateMinimap(GAGCore::ViewPoint point);
	void navigateMinimapIn(Minimap &minimap, GAGCore::ViewRect rect, int size, GAGCore::ViewPoint point);
	// Compact tactical tools: a lens strip in the thumb corner (overlays, health
	// bars, statistics, the map peek, history, marks, chat), a legend for the
	// active overlay, and a large map peek for one-thumb navigation.
	struct Lens
	{
		std::string label;
		int action;
		bool selected;
	};
	bool lensOpen = false;
	bool lensVisible() const;
	std::vector<Lens> lenses() const;
	std::vector<GAGCore::ViewRect> lensRects(const GAGCore::MobileLayout &ui) const;
	void drawLenses();
	GAGCore::ViewRect overlayLegendRect() const;
	void drawOverlayLegend();
	bool peekOpen = false;
	std::unique_ptr<Minimap> peekMinimap;
	std::optional<std::uint64_t> minimapPress;
	GAGCore::ViewRect peekRect() const; // The map square.
	std::vector<GAGCore::ViewRect> peekButtons() const; // Done, zoom out, zoom in (thumb side last).
	void navigatePeek(GAGCore::ViewPoint point);
	void drawPeek();
	// Compact statistics: a bottom sheet with the team's history chart (the
	// end-game chart, own team only), a metric switcher and current counters.
	bool statsOpen = false;
	int statsMetric = 0;
	std::vector<Stats::Metric> statsCatalog;
	double statsDrag = 0;
	struct StatsLayout
	{
		GAGCore::ViewRect sheet, close, previous, next, title, counters, chart;
	};
	StatsLayout statsLayout() const;
	void drawStats();
	void drawBuildPalette();
	bool inspectingResource() const;
	struct ResourceInfo { std::string name, amount; int sprite = 0; unsigned resource = 0; };
	std::optional<ResourceInfo> resourceInfo() const;
	void drawResourceInfo();
	std::vector<std::pair<std::string, int>> tacticalActions() const;
	void drawTacticalPanel();
	std::vector<std::string> unitInfoRows() const;
	void drawUnitPanel();
	bool inspectingReadOnly() const;
	GAGCore::ViewRect readOnlyCloseRect() const;
	// Survives selection invalidation before prepareDraw in threaded clients.
	bool readOnlyPanelShown = false;
	void tapBuildPalette(GAGCore::ViewPoint point);
	struct BuildingAction
	{
		std::string label;
		int kind, value = 0;
		bool selected = false;
	};
	//! Whether a building is selected for inspection (GUI state; for layout and input).
	bool inspecting() const;
	//! The inspected building as last drawn (the frame's PresentationFrame); null when none or not
	//! extracted yet. For drawing its fields.
	const SceneBuildingPanel *inspectedBuilding() const;
	std::vector<BuildingAction> buildingActions() const;
	// Shared geometry for painting, hit testing, sliders and accessibility.
	std::vector<GAGCore::ViewRect> buildingActionBoxes(double width) const;
	GAGCore::ViewRect buildingActionRect(size_t index) const;
	double buildingActionsHeight(double width) const;
	void drawBuildingActions();
	void tapBuildingAction(GAGCore::ViewPoint point);
	void applyDiscreteAction(const SceneBuildingPanel &building, const BuildingAction &row);
	void setRatio(const SceneBuildingPanel &building, int type, int value);
	// Compact (phone) inspectors are a thumb dial: concentric quarter rings in
	// the thumb corner for workers, priority and a ratio or range, with chips
	// for discrete choices and actions. Spacious panels keep the row list.
	struct DialLayout
	{
		TouchDial::Geometry geometry;
		GAGCore::ViewRect header, chips, bounds;
		bool portrait = true;
	};
	struct DialRegion
	{
		enum Part { Arc, Minus, Plus, Segment, Proportions, Chip } part = Chip;
		BuildingAction action;
		int ring = -1, maximum = 0;
		double from = 0, to = 0; // Sweep angles covered by the region.
		double sliderFrom = 0, sliderTo = 0; // Sweep of the whole slider (Arc only).
		GAGCore::ViewRect box;	// Chips; a thumb-sized box around ring regions.
	};
	struct DialChips
	{
		std::vector<BuildingAction> actions;
		std::vector<GAGCore::ViewRect> boxes;
		GAGCore::ViewRect legend;
		bool fits = true;
	};
	DialChips dialChips(const DialLayout &dial) const;
	bool usesDial() const;
	bool usesDial(const GAGCore::MobileLayout &ui) const;
	DialLayout dialLayout(const GAGCore::MobileLayout &ui) const;
	std::vector<DialRegion> dialRegions() const;
	std::optional<DialRegion> dialRegionAt(GAGCore::ViewPoint point) const;
	GAGCore::ViewPoint dialActionPoint(int kind, int value, int side) const;
	void tapDial(const SceneBuildingPanel &building, const DialRegion &region, GAGCore::ViewPoint point);
	void drawDial();
	std::array<int, 3> dialRatios() const;
	void commitRatios(const SceneBuildingPanel &building, const std::array<int, 3> &ratios);
	int heldActionKind = -1, heldActionValue = 0;
	std::string heldActionLabel;
	bool heldActionConfirmation = false;
	int heldBuildingState = -1, heldConstructionState = -1;
	std::optional<BuildingAction> actionAt(GAGCore::ViewPoint point) const;
	double actionScroll = 0;
	bool confirmDestroy = false;
	BuildingRef lastInspectedBuilding;
	void drawPointLabel(GAGCore::ViewRect rect, const std::string &text, double textScale = 1.15,
						bool leading = false);
	BuildingRef ownerBuilding;
	const void *ownerDialog = nullptr;
	bool panelOpen = false;
	bool showStatistics = false;
	bool restorePalette = false, previousPanelOpen = false;
	int previousDisplayMode = 0;
	bool swallowMouseRelease = false, ignoreTouchSequence = false;
	double panelScroll = 144;
	bool tutorialCollapsed = false;
	std::string tutorialText;
	std::vector<std::string> tutorialLines;
	double tutorialWidth = 0, tutorialScroll = 0;
	GAGCore::ViewRect tutorialRect() const;
	void prepareTutorial();
	void drawTutorial();
	GAGCore::MobileLayout layout() const;
	// Edge-hugging controls the host is asked to keep free of system gestures;
	// synchronised only when the set changes.
	std::vector<GAGCore::ViewRect> gestureExclusion;
	int gestureExclusionUpdates = 0;
	void syncGestureExclusion();
	void clampScroll();
	// HUD panel offsets with momentum and bounce; clampScroll() keeps them and
	// the plain scroll variables in step.
	GAGCore::TrackedScrollAxis panelAxis, actionAxis, tutorialAxis;
	GAGCore::GestureScrollController nativeScroll;
	Uint64 nativeSequence = 0;
	int nativePanel = 0; // 1 palette, 2 inspector actions, 3 tutorial; retained for cancelled tails
	bool nativeScrolling = false;
	GAGCore::TrackedScrollAxis &nativeAxis() { return nativePanel == 3 ? tutorialAxis : nativePanel == 2 ? actionAxis : panelAxis; }
	double &nativeOffset() { return nativePanel == 3 ? tutorialScroll : nativePanel == 2 ? actionScroll : panelScroll; }
	GAGCore::ScrollMotion mapMotion;
	// Screen-point displacement, separate from the smaller pan/tap slop.
	GAGCore::ViewPoint mapDragTravel{};
	bool mapFlingArmed = false;
	double tutorialMaximum() const;
	Uint64 lastStepTime = 0;
	// Momentum only follows real fingers; the synthetic mouse finger drags.
	bool fingerIsTouch = false;
	Uint64 eventTime(const SDL_Event &event) const;
	GAGCore::TouchInput gesture;
	std::optional<Uint32> lastMapTapTicks;
	GAGCore::ViewPoint lastMapTapPoint{};
	// First contact of the current touch sequence, for tap/drag decisions
	// that the recogniser does not expose (paint taps, zoom feedback).
	GAGCore::ViewPoint touchStart{}, touchPoint{};
	bool touchTravelled = false;
	bool zoomTapArmed(Uint32 ticks, GAGCore::ViewPoint point) const;
	bool zoomIn(GAGCore::ViewPoint point);
	std::string zoomReadout() const;
	std::vector<std::pair<SDL_TouchID, SDL_FingerID>> fingers;
	bool touchActive = false, interfaceGesture = false, dispatching = false;
	bool ownerOverlay = false;
	int ownerRegion = 0, ownerSelection = 0, ownerMenu = 0;
	std::string ownerTool;
	int interfaceRegion(GAGCore::ViewPoint point) const;
	double scale = 1, panX = 0, panY = 0;
	int keyboardFocus = -1;
	std::vector<GAGCore::ViewRect> keyboardTargets();
	std::optional<GAGCore::ViewPoint> preview;
	std::string previewType;
	std::unique_ptr<GAGCore::DrawableSurface> confirmLabel, cancelLabel;
	GAGCore::ViewRect world() const;
	GAGCore::ViewRect controls() const;
	// Placement confirmation halves of controls(); OK sits under the thumb.
	GAGCore::ViewRect confirmRect() const;
	GAGCore::ViewRect cancelRect() const;
	GAGCore::ViewPoint previewCursor() const;
	void actions(const std::vector<GAGCore::TouchAction> &actions);
	void interfaceTap(GAGCore::ViewPoint point);
	void select(GAGCore::ViewPoint point);
};
