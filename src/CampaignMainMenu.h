// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault
#pragma once
#include "ui/FrontendUI.h"
#include <ScreenStack.h>

///Offers loading a campaign or starting a new one.
class CampaignMainMenu : public Glob2UI::Screen
{
  public:
	explicit CampaignMainMenu(GAGGUI::ScreenStack &screens);
	Glob2UI::Element build(const Glob2UI::Presentation &presentation) override;
	//! Values returned by execution. Callers may also receive
	//! Screen::QUIT_APPLICATION, produced when the application is being quit.
	enum ReturnCode
	{
		//! The player backed out to the main menu
		CANCELLED = 1,
	};

  protected:
	void onEscape() override { endExecute(CANCELLED); }

  private:
	GAGGUI::ScreenStack &screens;
	//! Pick a campaign with CampaignSelectorScreen, then run it in CampaignMenuScreen.
	void runCampaignSelection(bool newCampaign);
};
