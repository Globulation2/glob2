// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "CampaignMenuScreen.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "Engine.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"
#include "GUIMessageBox.h"

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
		if (par1==EXIT)
		{
			// Player is leaving the campaign menu; if the save fails (read-only
			// dir, full disk) we still proceed with the exit but the stderr log
			// in Campaign::save records why progress wasn't persisted.
			campaign.save(true);
			endExecute(par1);
		}
		else if(par1==START)
		{
			CampaignMapEntry* selected = getSelectedMission();
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
				// Post-mission save persists completion / unlock state. If it
				// silently dropped, the player would re-launch a "completed"
				// mission or find the next one still locked, so surface the
				// failure instead of swallowing it.
				if (!campaign.save(true))
					GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON,
						Toolkit::getStringTable()->getString("[ERROR_CANT_SAVE_CAMPAIGN]"),
						Toolkit::getStringTable()->getString("[ok]"));
			}
		}
	}
	else if(action==TEXT_MODIFIED)
	{
		if(source==playerName)
		{
			campaign.setPlayerName(playerName->getText());
		}
	}
	else if (action == LIST_ELEMENT_SELECTED)
	{
		CampaignMapEntry* selected = getSelectedMission();
		if (selected)
		{
			mapPreview->setMapThumbnail(selected->getMapFileName().c_str());
			description->setText(Toolkit::getStringTable()->getString(selected->getDescription()));
		}
	}
}



CampaignMapEntry* CampaignMenuScreen::getSelectedMission()
{
	// List::get() asserts (and is undefined behavior in release builds) when the
	// list has no selection. The list starts unselected and repopulateAvailableMissions()
	// clears the selection after every mission run, so guard before dereferencing it.
	if (availableMissions->getSelectionIndex() < 0)
		return nullptr;
	return campaign.findUnlockedMap(availableMissions->get());
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

