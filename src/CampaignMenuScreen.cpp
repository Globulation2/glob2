// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "CampaignMenuScreen.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"

CampaignMenuScreen::CampaignMenuScreen(const std::string& name)
{
	if (!campaign.load(name))
		campaign.setName(name);
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", campaign.getName());
	addWidget(title);
	startMission = new TextButton(10, 430, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[start mission]"), START);
	addWidget(startMission);
	exit = new TextButton(330, 430, 300, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), EXIT);
	addWidget(exit);
	playerName = new TextInput(330, 225, 300, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", campaign.getPlayerName());
	addWidget(playerName);
	availableMissions = new CheckList(10, 50, 300, 200, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	repopulateAvailableMissions();
	addWidget(availableMissions);
	
	
	mapPreview = new MapPreview(330, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);
	
	description = new TextArea(10, 260, 620, 160, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", true);
	addWidget(description);
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
			CampaignMapEntry* selected = campaign.findUnlockedMap(availableMissions->get());
			if (selected)
			{
				Engine engine;
				int rc_e = engine.initCampaign(selected->getMapFileName(), campaign, selected->getMapName());
				if (rc_e == Engine::EE_NO_ERROR)
				{
	    			int rcr = engine.run();
	    			if(rcr == -1)
	    			    endExecute(-1);
				}
				else if(rc_e == -1)
				{
					endExecute(-1);
				}
				repopulateAvailableMissions();
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
		CampaignMapEntry* selected = campaign.findUnlockedMap(availableMissions->get());
		if (selected)
		{
			mapPreview->setMapThumbnail(selected->getMapFileName().c_str());
			description->setText(Toolkit::getStringTable()->getString(selected->getDescription()));
		}
	}
}



void CampaignMenuScreen::repopulateAvailableMissions()
{
	availableMissions->clear();
	for (unsigned i = 0; i < campaign.getMapCount(); ++i)
	{
		if (campaign.getMap(i).isUnlocked())
			availableMissions->addItem(campaign.getMap(i).getMapName(), campaign.getMap(i).isCompleted());
	}
}



void CampaignMenuScreen::setNewCampaign()
{
	campaign.setPlayerName(globalContainer->settings.getUsername());
	playerName->setText(globalContainer->settings.getUsername());
}

