// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault

#include "CampaignMainMenu.h"

#include "Toolkit.h"
#include "StringTable.h"

#include "CampaignSelectorScreen.h"
#include "CampaignMenuScreen.h"
#include "GlobalContainer.h"

CampaignMainMenu::CampaignMainMenu()
{
	newCampaign = new TextButton(0, 70, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[start new campaign]"), NEWCAMPAIGN);
	addWidget(newCampaign);
	loadCampaign = new TextButton(0,  130, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[load campaign]"), LOADCAMPAIGN, 13);
	addWidget(loadCampaign);
	cancel = new TextButton(0, 415, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), CANCEL, 27);
	addWidget(cancel);
}


void CampaignMainMenu::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if ((par1==LOADCAMPAIGN))
		{
			CampaignSelectorScreen css(true);
			int rc_css=css.execute(globalContainer->gfx, 40);
			if(rc_css==CampaignSelectorScreen::OK)
			{
				CampaignMenuScreen cms(css.getCampaignName());
				int rc_cms=cms.execute(globalContainer->gfx, 40);
				if(rc_cms==CampaignMenuScreen::EXIT)
				{
				}
				else if(rc_cms == -1)
				{
					endExecute(-1);
				}
			}
			else if(rc_css==CampaignSelectorScreen::CANCEL)
			{
			}
			else if(rc_css == -1)
			{
				endExecute(-1);
			}
		}
		else if((par1==NEWCAMPAIGN))
		{
			CampaignSelectorScreen css;
			int rc_css=css.execute(globalContainer->gfx, 40);
			if(rc_css==CampaignSelectorScreen::OK)
			{
				CampaignMenuScreen cms(css.getCampaignName());
				cms.setNewCampaign();
				int rc_cms=cms.execute(globalContainer->gfx, 40);
				if(rc_cms==CampaignMenuScreen::EXIT)
				{
				}
				else if(rc_cms == -1)
				{
					endExecute(-1);
				}
			}
			else if(rc_css==CampaignSelectorScreen::CANCEL)
			{
			}
			else if(rc_css == -1)
			{
				endExecute(-1);
			}
		}
		else if((par1==CANCEL))
		{
			endExecute(CANCEL);
		}
	}
}

