// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GUIBase.h>
#include <ResponsiveDialog.h>
#include <TouchInput.h>
#include <functional>
#include <optional>
class Glob2Screen;
class PhoneForm {
public:
    PhoneForm(GAGGUI::Screen& screen, std::function<std::string(GAGGUI::Widget*)> label,
        std::function<bool(GAGGUI::Widget*)> visible, std::function<bool(GAGGUI::Widget*)> footer = [](auto*){return true;},
        std::function<std::optional<GAGCore::Color>(GAGGUI::Widget*)> color = [](auto*){return std::optional<GAGCore::Color>{};},
        std::function<bool(GAGGUI::Widget*)> labelIncludesValue = [](auto*){return false;})
        : screen(screen), label(label), visible(visible), footer(footer), color(color), labelIncludesValue(labelIncludesValue) {}
    ~PhoneForm();
    void draw();
    bool event(SDL_Event event);
    void cancel();
    void scrollToTop() { offset=0; cancel(); }
private:
    friend class GameGUITouchHarness;
    struct Row { GAGGUI::Widget* widget; std::string text; int kind=0,index=0;bool selected=false,footer=false; GAGCore::ViewRect rect; };
    GAGGUI::Screen& screen;
    std::function<std::string(GAGGUI::Widget*)> label;
    std::function<bool(GAGGUI::Widget*)> visible, footer;
    std::function<std::optional<GAGCore::Color>(GAGGUI::Widget*)> color;
    std::function<bool(GAGGUI::Widget*)> labelIncludesValue;
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
