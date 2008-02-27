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

#include "CampaignMenuScreen.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"

CampaignMenuScreen::CampaignMenuScreen(const std::string& name)
{
	campaign.load(name);
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", campaign.getName());
	addWidget(title);
	startMission = new TextButton(10, 430, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[start mission]"), START);
	addWidget(startMission);
	exit = new TextButton(330, 430, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), EXIT);
	addWidget(exit);
	playerName = new TextInput(330, 225, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", campaign.getPlayerName());
	addWidget(playerName);
	availableMissions = new List(10, 50, 300, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	for(unsigned i=0; i<campaign.getMapCount(); ++i)
	{
		if(campaign.getMap(i).isUnlocked())
			availableMissions->addText(campaign.getMap(i).getMapName());
	}
	addWidget(availableMissions);
	
	
	mapPreview = new MapPreview(330, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);
}

void CampaignMenuScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if ((par1==EXIT))
		{
			campaign.save(true);
			endExecute(par1);
		}
		else if((par1==START))
		{
			if (availableMissions->getSelectionIndex() >= 0)
			{
				Engine engine;
				int rc_e = engine.initCampaign(getMissionName(), campaign, availableMissions->get());
				if (rc_e == Engine::EE_NO_ERROR)
				{
					engine.run();
				}
				else if(rc_e == -1)
				{
					endExecute(-1);
				}
				availableMissions->clear();
				for(unsigned i=0; i<campaign.getMapCount(); ++i)
				{
					if(campaign.getMap(i).isUnlocked())
						availableMissions->addText(campaign.getMap(i).getMapName());
				}
				campaign.save(true);
			}
		}
	}
	else if((action==TEXT_MODIFIED))
	{
		if(source==playerName)
		{
			campaign.setPlayerName(playerName->getText());
		}
	}
	else if (action == LIST_ELEMENT_SELECTED)
	{
		std::string mapFileName = campaign.getMap(availableMissions->getSelectionIndex()).getMapFileName();
		mapPreview->setMapThumbnail(mapFileName.c_str());
	}
}




std::string CampaignMenuScreen::getMissionName()
{
	for(unsigned n=0; n<campaign.getMapCount(); ++n)
	{
		if(campaign.getMap(n).getMapName() == availableMissions->get())
		{
			return campaign.getMap(n).getMapFileName();
		}
	}
	assert(false);
	return "";
}



void CampaignMenuScreen::setNewCampaign()
{
	campaign.setPlayerName(globalContainer->settings.username);
	playerName->setText(globalContainer->settings.username);
}



CampaignChoiceScreen::CampaignChoiceScreen()
{
	newCampaign = new TextButton(0, 70, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[start new campaign]"), NEWCAMPAIGN);
	addWidget(newCampaign);
	loadCampaign = new TextButton(0,  130, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[load campaign]"), LOADCAMPAIGN, 13);
	addWidget(loadCampaign);
	cancel = new TextButton(0, 415, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), CANCEL, 27);
	addWidget(cancel);
}



void CampaignChoiceScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if ((par1==LOADCAMPAIGN))
		{
			endExecute(par1);
		}
		else if((par1==NEWCAMPAIGN))
		{
			endExecute(par1);
		}
		else if((par1==CANCEL))
		{
			endExecute(par1);
		}
	}
}

