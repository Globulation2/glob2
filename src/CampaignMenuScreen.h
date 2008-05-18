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

#include "Campaign.h"
#include "Glob2Screen.h"
#include "GUIButton.h"
#include "GUIList.h"
#include "GUIText.h"
#include "GUITextInput.h"
#include "GUITextArea.h"

class MapPreview;

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
	
	/// Map description
	TextArea* description;
	
	//! The widget that will show a preview of the selection map
	MapPreview *mapPreview;

};


#endif
