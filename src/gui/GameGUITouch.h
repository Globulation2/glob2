// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <TouchInput.h>
#include <SDL.h>
#include <string>
#include <optional>
#include <memory>
#include <vector>
namespace GAGGUI { class Widget; class OverlayScreen; }
namespace GAGCore { class DrawableSurface; }
class GameGUI;
class Building;
class GameGUITouch
{
public:
    explicit GameGUITouch(GameGUI& gui);
    ~GameGUITouch();
    bool process(SDL_Event& event);
    void cancel();
    void prepareDraw();
    void drawControls();
    bool active() const { return touchActive; }
    bool usesHUD() const;
    GAGCore::ViewRect worldBounds() const { return world(); }
    void drawHUD();
    void drawPanel();
    bool drawDialog();
    bool hasPreview() const { return preview.has_value(); }
private:
    friend class GameGUITouchHarness;
    GameGUI& gui;
    GAGGUI::OverlayScreen* activeDialog() const;
    void menuAction(int action);
    struct DialogRow { GAGGUI::Widget* widget; std::string text; int kind=0, index=0; bool selected=false, footer=false; GAGCore::ViewRect rect; };
    std::vector<DialogRow> dialogRows;
    GAGGUI::OverlayScreen* dialogOwner=nullptr;
    GAGGUI::Widget* heldDialogWidget=nullptr;
    int heldDialogIndex=0;
    double dialogScroll=0, dialogMaximum=0, lastDialogHeight=0;
    GAGGUI::Widget* editingDialogWidget=nullptr;
    GAGCore::ViewRect dialogContent;
    std::optional<GAGCore::ViewRect> labelClip;
    void prepareDialog();
    void tapDialog(GAGCore::ViewPoint point);
    std::vector<std::string> pointLines(const std::string& text, double width) const;
    Building* allocationBuilding() const;
    GAGCore::ViewRect allocationRect() const;
    GAGCore::ViewRect panelContent() const;
    void drawAllocation();
    struct BuildingAction { std::string label; int kind, value=0; bool selected=false; };
    Building* inspectedBuilding() const;
    std::vector<int> allocationTabs() const;
    std::vector<BuildingAction> buildingActions() const;
    void drawBuildingActions();
    void tapBuildingAction(GAGCore::ViewPoint point);
    int heldActionKind=-1, heldActionValue=0;
    bool heldActionConfirmation=false;
    int heldBuildingState=-1, heldConstructionState=-1;
    std::optional<BuildingAction> actionAt(GAGCore::ViewPoint point) const;
    double actionScroll=0;
    bool confirmDestroy=false;
    const void* lastInspectedBuilding=nullptr;
    int allocationTab=0;
    int activeAllocationTab() const;
    void drawPointLabel(GAGCore::ViewRect rect, const std::string& text);
    const void* ownerBuilding=nullptr;
    const void* ownerDialog=nullptr;
    bool panelOpen=false;
    bool dialogHUDDrawn=false, swallowMouseRelease=false, ignoreTouchSequence=false;
    double panelScroll=144;
    std::string tutorialText;
    std::vector<std::string> tutorialLines;
    double tutorialWidth=0, tutorialScroll=0;
    GAGCore::ViewRect tutorialRect() const;
    void prepareTutorial();
    void drawTutorial();
    GAGCore::MobileLayout layout() const;
    double panelScale() const;
    GAGCore::ViewPoint panelOrigin() const;
    void clampScroll();
    GAGCore::TouchInput gesture;
    std::vector<std::pair<SDL_TouchID, SDL_FingerID>> fingers;
    bool touchActive=false, interfaceGesture=false, dispatching=false;
    bool ownerOverlay=false;
    int ownerRegion=0, ownerSelection=0, ownerMenu=0;
    std::string ownerTool;
    int interfaceRegion(GAGCore::ViewPoint point) const;
    double scale=1, panX=0, panY=0;
    std::optional<GAGCore::ViewPoint> preview;
    std::string previewType;
    std::unique_ptr<GAGCore::DrawableSurface> confirmLabel, cancelLabel;
    GAGCore::ViewRect world() const;
    GAGCore::ViewRect controls() const;
    GAGCore::ViewPoint previewCursor() const;
    void actions(const std::vector<GAGCore::TouchAction>& actions);
    void interfaceTap(GAGCore::ViewPoint point);
    void select(GAGCore::ViewPoint point);
};
