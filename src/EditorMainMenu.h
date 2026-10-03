// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#pragma once
#include "ui/FrontendUI.h"
#include <ScreenStack.h>

//! Chooses how to make or open a map or campaign in the editor.
class EditorMainMenu : public Glob2UI::Screen
{
  public:
    const char* recordingId() const override { return "editor_main_menu"; }
	enum
	{
		NEWMAP = 1,
		LOADMAP = 2,
		CANCEL = 3,
		NEWCAMPAIGN = 4,
		LOADCAMPAIGN = 5,
	};
	explicit EditorMainMenu(GAGGUI::ScreenStack &screens);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;

  protected:
	void onEscape() override { endExecute(CANCEL); }

  private:
	GAGGUI::ScreenStack &screens;
	void newMap();
	void loadMap();
	void loadCampaign();
};
