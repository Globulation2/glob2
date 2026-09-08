// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GUIBase.h>
#include <GUITabScreen.h>
#include <ResponsiveMenu.h>
#include <TouchInput.h>
#include <GUIButton.h>
#include <map>

using namespace GAGCore;
using namespace GAGGUI;

class PhoneForm;

class Glob2Screen : public Screen
{
public:
	Glob2Screen();
	virtual ~Glob2Screen();
	virtual void paint(void);
    bool usesResponsiveViewport() const override;
    bool phoneVisible(GAGGUI::Widget* widget) const {auto it=phoneVisibility.find(widget);return it==phoneVisibility.end() || it->second;}
    std::string phoneLabel(GAGGUI::Widget* widget) const { auto it=phoneLabels.find(widget);return it==phoneLabels.end() ? std::string{} : it->second; }
    void beginExecution(GAGCore::DrawableSurface* surface) override;
    void updateExecution(Uint32 tick) override;
    void handleExecutionEvent(SDL_Event event) override;
    void drawExecution() override;
    void cancelExecutionInput() override;
    void viewportResized(int,int,int,int) override { cancelExecutionInput(); }
protected:
    void setPhoneVisible(GAGGUI::Widget* widget,bool visible) {phoneVisibility[widget]=visible;}
    void setPhoneLabel(GAGGUI::Widget* widget,const std::string& label) { phoneLabels[widget]=label; }
    void enablePhoneForm(bool enabled=true) { phoneFormEnabled=enabled; }
    void enableResponsiveMenu(const std::string& title = "") { responsiveMenu = true; menuTitle = title; }
private:
    friend class GameGUITouchHarness;
    bool responsiveMenu = false;
    bool phoneFormEnabled=false;
    std::map<GAGGUI::Widget*,std::string> phoneLabels;
    std::map<GAGGUI::Widget*,bool> phoneVisibility;
    std::unique_ptr<PhoneForm> phoneForm;
    bool phoneFormActive() const;
    std::string menuTitle;
    std::vector<GAGGUI::TextButton*> menuButtons;
    GAGCore::ResponsiveMenu menuLayout;
    GAGCore::TouchInput menuTouch;
    int layoutW = 0, layoutH = 0;
    bool responsiveActive() const;
    void layoutMenu(double offset);
    void menuActions(const std::vector<GAGCore::TouchAction>& actions);

	
private:
	unsigned getNextTerrain(void);
	Uint32 randomSeed; // Background LCG intentionally wraps modulo 2^32.
};

class Glob2TabScreen : public TabScreen
{
public:
	Glob2TabScreen(bool fullScreen, bool longerButtons=false);
	virtual ~Glob2TabScreen();
	virtual void paint(void);
	
private:
	unsigned getNextTerrain(void);
	Uint32 randomSeed; // Background LCG intentionally wraps modulo 2^32.
};


