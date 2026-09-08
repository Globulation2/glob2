// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006-2008 Bradley Arsenault

#include "CampaignMainMenu.h"

#include "Toolkit.h"
#include "StringTable.h"

#include "CampaignSelectorScreen.h"
#include "CampaignMenuScreen.h"
#include "GlobalContainer.h"

CampaignMainMenu::CampaignMainMenu(GAGGUI::ScreenStack& screens) : screens(screens)
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
		if (par1==LOADCAMPAIGN)
		{
			runCampaignSelection(false);
		}
		else if(par1==NEWCAMPAIGN)
		{
			runCampaignSelection(true);
		}
		else if(par1==CANCEL)
		{
			endExecute(CANCELLED);
		}
	}
}

void CampaignMainMenu::runCampaignSelection(bool newCampaign)
{
	screens.push(std::make_unique<CampaignSelectorScreen>(!newCampaign),
        [this, newCampaign](Screen& selected, int result) {
            if (result != CampaignSelectorScreen::OK) return;
            auto menu = std::make_unique<CampaignMenuScreen>(
                static_cast<CampaignSelectorScreen&>(selected).getCampaignName(), screens);
            if (newCampaign) menu->setNewCampaign();
            screens.push(std::move(menu));
        });
}
