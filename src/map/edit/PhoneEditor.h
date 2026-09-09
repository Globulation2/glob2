// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <TouchInput.h>
#include <ResponsiveDialog.h>
#include <memory>
class MapEdit;
class MapEditorWidget;
class PhoneForm;
namespace GAGGUI { class OverlayScreen; }
class PhoneEditor {
public:
    explicit PhoneEditor(MapEdit& editor);
    ~PhoneEditor();
    bool event(SDL_Event event);
    void draw();
    void cancel();
    void closeOverlay();
    void showFailure();
    bool hasOverlay() const;
private:
    friend class GameGUITouchHarness;
    struct Row { MapEditorWidget* widget; GAGCore::ViewRect rect; double scale; bool fixed=false; };
    MapEdit& editor;
    GAGCore::TouchInput touch;
    std::vector<Row> rows;
    GAGCore::ViewRect safe, content;
    GAGGUI::OverlayScreen* overlay=nullptr;
    std::unique_ptr<PhoneForm> form;
    bool tools=false, pan=true, onMap=false;
    double offset=0, maximum=0, panX=0, panY=0;
    int held=-1;
    void prepare();
    void syncOverlay();
    int hit(GAGCore::ViewPoint point) const;
    void act(const GAGCore::TouchAction& action);
};
