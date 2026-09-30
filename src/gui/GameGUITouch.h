// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <TouchInput.h>
#include "TouchInteractionSession.h"
#include <SDL.h>
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
class GameGUI;
class Building;
class Minimap;
class GameGUITouch
{
  public:
	explicit GameGUITouch(GameGUI &gui);
	~GameGUITouch();
	bool process(SDL_Event &event);
	void cancel(bool preservePreview = false);
	void prepareDraw();
	void drawControls();
	void drawKeyboardFocus();
	bool active() const { return touchActive || usesHUD(); }
	bool usesHUD() const;
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
	Building *allocationBuilding() const;
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
	std::optional<PaletteItem> paletteItemAt(GAGCore::ViewPoint point) const;
	std::optional<TouchPlacementSession> placement;
	// Preview contact pans while held; releasing it never commits construction.
	std::optional<TouchPlacementSession> placementHold;
	std::optional<TouchAllocationSession> allocation;
	bool processAllocationPointer(const SDL_Event &event, GAGCore::ViewPoint point);
	TouchStrokeSession stroke;
	bool processPalettePointer(const SDL_Event &event, GAGCore::ViewPoint point);
	void advancePlacement();
	void updatePlacementPreview(GAGCore::ViewPoint point);
	bool commitPlacement();
	std::unique_ptr<Minimap> hudMinimap;
	GAGCore::ViewRect minimapRect() const;
	void drawMinimap();
	void navigateMinimap(GAGCore::ViewPoint point);
	void drawBuildPalette();
	std::vector<std::pair<std::string, int>> tacticalActions() const;
	void drawTacticalPanel();
	void tapBuildPalette(GAGCore::ViewPoint point);
	struct BuildingAction
	{
		std::string label;
		int kind, value = 0;
		bool selected = false;
	};
	Building *inspectedBuilding() const;
	std::vector<BuildingAction> buildingActions() const;
	// Shared geometry for painting, hit testing, sliders and accessibility.
	std::vector<GAGCore::ViewRect> buildingActionBoxes(double width) const;
	GAGCore::ViewRect buildingActionRect(size_t index) const;
	double buildingActionsHeight(double width) const;
	void drawBuildingActions();
	void tapBuildingAction(GAGCore::ViewPoint point);
	int heldActionKind = -1, heldActionValue = 0;
	std::string heldActionLabel;
	bool heldActionConfirmation = false;
	int heldBuildingState = -1, heldConstructionState = -1;
	std::optional<BuildingAction> actionAt(GAGCore::ViewPoint point) const;
	double actionScroll = 0;
	bool confirmDestroy = false;
	const void *lastInspectedBuilding = nullptr;
	void drawPointLabel(GAGCore::ViewRect rect, const std::string &text, double textScale = 1.15,
						bool leading = false);
	const void *ownerBuilding = nullptr;
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
	void clampScroll();
	GAGCore::TouchInput gesture;
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
	GAGCore::ViewPoint previewCursor() const;
	void actions(const std::vector<GAGCore::TouchAction> &actions);
	void interfaceTap(GAGCore::ViewPoint point);
	void select(GAGCore::ViewPoint point);
};
