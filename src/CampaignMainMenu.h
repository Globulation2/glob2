// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault

#pragma once

#include "Glob2Screen.h"
#include "GUIButton.h"

///This is the screen that provides the player with the choice of loading a campaign or starting a new one
class CampaignMainMenu : public Glob2Screen
{
public:
	CampaignMainMenu();
	void onAction(Widget *source, Action action, int par1, int par2);
	//! Widget return codes, delivered to onAction as par1. These identify
	//! buttons only; they are never used as execute() return values.
	enum
	{
		NEWCAMPAIGN,
		LOADCAMPAIGN,
		CANCEL,
	};
	//! Values returned by execute(). Callers may also receive
	//! Screen::QUIT_APPLICATION (-1), produced by the event loop when the
	//! application is being quit; it is propagated from nested screens too.
	enum ReturnCode
	{
		//! The player backed out to the main menu
		CANCELLED = 1,
	};
private:
	//! Shared flow for the "new campaign" and "load campaign" buttons:
	//! pick a campaign with CampaignSelectorScreen, then run it in
	//! CampaignMenuScreen. Propagates application quit to our caller.
	void runCampaignSelection(bool newCampaign);
	/// The new campaign button
	Button *newCampaign;
	/// The load campaign button
	Button *loadCampaign;
	/// The cancel button
	Button *cancel;
};

