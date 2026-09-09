// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once

#include "Glob2Screen.h"
#include "Settings.h"
#include "KeyboardManager.h"
#include <GUIDropdown.h>
#include <array>
#include <functional>
#include <vector>

// Native, settings-local form. A single screen owns layout, clipping and focus,
// so a scrolled/disabled control cannot receive events through another widget.
class SettingsScreen : public Glob2Screen
{
public:
    enum class Category { Display, Audio, Gameplay, Buildings, Controls, Player };
    enum class Kind { Section, Info, Toggle, Choice, Slider, Number, Text, Button, Binding };
    struct Rect {
        int x=0,y=0,w=0,h=0;
        bool contains(int px,int py) const { return px>=x && py>=y && px<x+w && py<y+h; }
    };
    struct Row {
        std::string id, label, help, value, extraId;
        Kind kind=Kind::Info;
        int number=0, minimum=0, maximum=1;
        bool enabled=true, selected=false;
        std::vector<std::string> choices;
        std::function<void(int)> change;
        std::function<void()> action;
        Rect bounds, control;
        // Table entries share a line only when the viewport is wide enough.
        int columns=1, column=0;
    };
    SettingsScreen();
    ~SettingsScreen() override;
    void paint() override;
    void onTimer(Uint32 tick) override;
    void onSDLEvent(SDL_Event* event) override;
    void onAction(Widget*, Action action, int, int) override;
    static int menu();

    // Stable semantic interface, also used by native integration tests.
    void selectCategory(Category category);
    Category category() const { return current; }
    const std::vector<Row>& rows();
    bool changeSetting(const std::string& id,int value);
    void activateSetting(const std::string& id);
    void finishInteraction();
    bool saveFailed() const { return failed; }
    bool restartRequired() const;
    bool displayConfirmationPending() const;
    void confirmDisplay(bool keep);
    void done();

protected:
    virtual bool applyDisplayMode(int width,int height,Uint32 flags);

private:
    enum class Modal { None, Binding, Conflict, Restore, Display };
    Category current=Category::Display;
    Modal modal=Modal::None;
    std::vector<Row> form;
    std::array<int,6> scroll{};
    int modalScroll=0, contentHeight=0, buildingTab=0, scrollbarGrab=0;
    GAGGUI::Dropdown dropdown;
    std::function<void(int)> dropdownChange;
    Rect panel, viewport, footer, scrollbar, categoryControl;
    bool compactNavigation=false;
    int padding=24, sidebar=176;
    std::string focus, dragging, textDraft;
    bool editingText=false, selectAllText=false, scrollingBar=false;
    size_t textCursor=0;
    bool failed=false, settingsDirty=false;
    std::array<bool,2> keyboardDirty{};
    Uint32 saveAt=0, displayDeadline=0;
    bool displayError=false;
    Settings previousDisplay;
    KeyboardManager gameKeys, editorKeys;
    ShortcutMode shortcutMode=GameGUIShortcuts;
    Row picker;
    std::string returnFocus;
    int bindingIndex=-1, captureKey=-1;
    Uint32 bindingAction=0;
    std::vector<KeyPress> bindingKeys;
    std::vector<int> conflicts;
    bool bindingAdvanced=false;

    static std::string tr(const std::string& text);
    void buildRows();
    void buildGeneral();
    void buildBuildings();
    void buildKeyboard();
    void buildModal();
    void layout();
    void paintRow(const Row& row);
    void drawText(int x,int y,const std::string& text, bool muted=false, bool heading=false);
    int wrappedHeight(const std::string& text,int width,bool heading=false) const;
    void drawWrapped(int x,int y,int width,const std::string& text,bool muted=false,bool heading=false);
    std::vector<std::string> wrap(const std::string& text,int width,bool heading=false) const;
    Row& add(const std::string& id,Kind kind,const std::string& label,const std::string& help="");
    void button(const std::string& id,const std::string& label,std::function<void()> action,bool selected=false);
    void section(const std::string& label);
    void info(const std::string& label);
    void choice(const std::string& id,const std::string& label,const std::string& help,int value,
                std::vector<std::string> labels,std::function<void(int)> change);
    void toggle(const std::string& id,const std::string& label,const std::string& help,bool value,std::function<void(int)> change);
    void number(const std::string& id,const std::string& label,int value,int minimum,int maximum,std::function<void(int)> change);
    void commit(bool defer=false);
    bool persist();
    void commitText();
    void closeModal();
    void changeDisplay(std::function<void(Settings&)> change);
    int& scrollOffset();
    void focusNext(bool backward);
    void openCategoryPicker();
    void ensureFocusVisible();
    void invoke(Row row,int direction=0);
    void adjustSlider(const std::string& id,int x);
    KeyboardManager& keyboard();
    void editBinding(int index,Uint32 action);
    void saveBinding(bool replace=false);
    void deleteBinding();
    std::string bindingLabel(const KeyboardShortcut& shortcut) const;
};
