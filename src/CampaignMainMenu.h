// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault

#ifndef CampaignMainMenu_h
#define CampaignMainMenu_h

#include "Glob2Screen.h"
#include "GUIButton.h"

///This is the screen that provides the player with the choice of loading a campaign or starting a new one
class CampaignMainMenu : public Glob2Screen
{
public:
	CampaignMainMenu();
	void onAction(Widget *source, Action action, int par1, int par2);
	enum
	{
		NEWCAMPAIGN,
		LOADCAMPAIGN,
		CANCEL,
	};
private:
	/// The new campaign button
	Button *newCampaign;
	/// The load campaign button
	Button *loadCampaign;
	/// The cancel button
	Button *cancel;
};

#endif
