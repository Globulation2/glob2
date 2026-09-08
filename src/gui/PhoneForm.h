// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <ResponsiveDialog.h>
#include <TouchInput.h>
class Glob2Screen;
class PhoneForm {
public:
    explicit PhoneForm(Glob2Screen& screen): screen(screen) {}
    ~PhoneForm();
    void draw();
    bool event(SDL_Event event);
    void cancel();
private:
    friend class GameGUITouchHarness;
    struct Row { GAGGUI::Widget* widget; std::string text; int kind=0,index=0;bool selected=false,footer=false; GAGCore::ViewRect rect; };
    Glob2Screen& screen;
    std::vector<Row> rows;
    GAGCore::ResponsiveDialog placement;
    GAGCore::TouchInput touch;
    GAGGUI::Widget* editing=nullptr;
    GAGGUI::Widget* held=nullptr;
    int heldKind=-1,heldIndex=0;
    std::string heldText;
    double offset=0,lastHeight=0;
    void prepare();
    void act(const std::vector<GAGCore::TouchAction>& actions);
    Row* hit(GAGCore::ViewPoint point);
};
