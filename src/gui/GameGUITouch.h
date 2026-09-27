// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <TouchInput.h>
#include "TouchInteractionSession.h"
#include <SDL.h>
#include <string>
#include <optional>
#include <memory>
#include <vector>
namespace GAGGUI
{
class Widget;
class OverlayScreen;
} // namespace GAGGUI
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
	bool drawDialog();
	bool hasPreview() const { return preview.has_value(); }

  private:
	friend class GameGUITouchHarness;
	friend class MobileGalleryGameplay;
	GameGUI &gui;
	GAGGUI::OverlayScreen *activeDialog() const;
	void menuAction(int action);
	struct DialogRow
	{
		GAGGUI::Widget *widget;
		std::string text;
		int kind = 0, index = 0;
		bool selected = false, footer = false;
		GAGCore::ViewRect rect;
		int team = -1;
		enum class Role
		{
			Body,
			Title,
			Section,
			Player
		} role = Role::Body;
	};
	std::vector<DialogRow> dialogRows;
	GAGGUI::OverlayScreen *dialogOwner = nullptr;
	GAGGUI::Widget *heldDialogWidget = nullptr;
	int heldDialogIndex = 0, heldDialogKind = -1;
	double dialogScroll = 0, dialogMaximum = 0, lastDialogHeight = 0;
	GAGGUI::Widget *editingDialogWidget = nullptr;
	std::string chatComposition;
	GAGCore::ViewRect dialogContent, dialogBounds;
	std::optional<GAGCore::ViewRect> labelClip;
	int objectivePage = 1;
	void prepareDialog();
	void tapDialog(GAGCore::ViewPoint point);
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
	bool reducedMotion = false;
	bool restorePalette = false, previousPanelOpen = false;
	int previousDisplayMode = 0;
	bool dialogHUDDrawn = false, swallowMouseRelease = false, ignoreTouchSequence = false;
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
