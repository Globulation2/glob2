// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Glob2Screen.h"
#include <InterfacePresentation.h>
#include <vector>
#include <memory>

class MainMenuButton;
class LobbyControls;

class MainMenuScreen:public Glob2Screen
{
	friend struct MobileGallerySetup;
    friend struct ResponsiveMenuHarness;

  public:
	enum
	{
		CAMPAIGN,
		TUTORIAL,
		LOAD_GAME,
		CUSTOM,
		MULTIPLAYERS_YOG,
		MULTIPLAYERS_LAN,
		GAME_SETUP,
		EDITOR,
		CREDITS,
		QUIT,
	};
	
public:
  bool usesResponsiveViewport() const override { return GAGCore::phonePresentationRequested(); }
  bool supportsCompactViewport() const override { return true; }
  MainMenuScreen();
  ~MainMenuScreen() override;
  void onAction(Widget *source, Action action, int par1, int par2) override;
  void paint(void) override;
  void cancelExecutionInput() override;
  void onSDLEvent(SDL_Event *event) override;
  void viewportResized(int oldWidth, int oldHeight, int width, int height) override;

private:
	std::vector<MainMenuButton*> buttons;
	std::unique_ptr<GAGCore::DrawableSurface> wordmark;
	int focusedButton = -1;
	int panelX, panelY, panelW, panelH;
	LobbyControls *mobileControls = nullptr;
	bool more = false;
	void renderMobile();
	bool compact = false;
	void layout(int width, int height);
};
