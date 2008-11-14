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

#ifndef CAMPAIGN_SELECTOR_SCREEN_H
#define CAMPAIGN_SELECTOR_SCREEN_H

#include "Glob2Screen.h"
#include "GUIText.h"
#include "GUIButton.h"
#include "GUIFileList.h"
#include "GUITextArea.h"

#include <string>

class CampaignSelectorScreen : public Glob2Screen
{
public:
	CampaignSelectorScreen(bool isSelectingSave=false);
	void onAction(Widget *source, Action action, int par1, int par2);
	std::string getCampaignName();

	enum
	{
		//! Value returned upon screen execution completion when a valid campaign is selected
		OK = 1,
		//! Value returned upon screen execution completion when the campaign selection is canceled
		CANCEL = 2,
	};
private:
	//! Title of the screen, depends on the directory given in parameter
	Text *title;
	//! The ok button
	Button *ok;
	//! The cancel button
	Button *cancel;
	/// The list of campaigns
	FileList *fileList;
	/// The description
	TextArea* description;
};


#endif

