/*
  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef CAMPAIGN_MENU_SCREEN_H
#define CAMPAIGN_MENU_SCREEN_H

#include "Glob2Screen.h"
#include "Campaign.h"
#include "GUIText.h"
#include "GUIButton.h"
#include "GUIList.h"
#include "GUITextInput.h"
#include "Campaign.h"

///This is the main campaign screen
class CampaignMenuScreen : public Glob2Screen
{
public:
	CampaignMenuScreen(const std::string& name);
	void onAction(Widget *source, Action action, int par1, int par2);
	std::string getMissionName();
	void setNewCampaign();
	enum
	{
		EXIT,
		START,
	};
private:
	Campaign campaign;

	/// Title of the screen
	Text* title;
	/// The exit to menuscreen button
	Button* exit;
	/// The "start mission" buttion
	Button* startMission;

	/// The box where the players name is put
	TextInput* playerName;

	/// The list of missions that are currently unlocked
	List* availableMissions;

};

///This is the screen that provides the player with the choice of loading a campaign or starting a new one
class CampaignChoiceScreen : public Glob2Screen
{
public:
	CampaignChoiceScreen();
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
