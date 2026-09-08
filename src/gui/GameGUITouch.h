// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <TouchInput.h>
#include <SDL.h>
#include <string>
#include <optional>
#include <memory>
#include <vector>
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
    bool drawPauseMenu();
    bool hasPreview() const { return preview.has_value(); }
private:
    GameGUI& gui;
    Building* allocationBuilding() const;
    GAGCore::ViewRect allocationRect() const;
    GAGCore::ViewRect panelContent() const;
    std::vector<GAGCore::ViewRect> pauseButtons() const;
    void drawAllocation();
    int allocationTab=0;
    int activeAllocationTab() const;
    void drawPointLabel(GAGCore::ViewRect rect, const std::string& text);
    const void* ownerBuilding=nullptr;
    const void* ownerDialog=nullptr;
    bool panelOpen=false;
    bool pauseHUDDrawn=false, swallowMouseRelease=false, ignoreTouchSequence=false;
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
