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
    bool hasPreview() const { return preview.has_value(); }
private:
    GameGUI& gui;
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
