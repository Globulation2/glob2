/*
  Copyright (C) 2006-2008 Bradley Arsenault

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
		NEW_CAMPAIGN,
		LOAD_CAMPAIGN,
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
