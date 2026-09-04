// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "CampaignSelectorScreen.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "Campaign.h"

CampaignSelectorScreen::CampaignSelectorScreen(bool isSelectingSave)
{
	StringTable& table=*Toolkit::getStringTable();
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", table.getString("[choose campaign]"));
	ok = new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[ok]"), OK, 13);
	cancel = new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[Cancel]"), CANCEL, 27);
	if(isSelectingSave)
		fileList = new FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "games", "txt", false);
	else
		fileList = new FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "campaigns", "txt", false);
	fileList->generateList();
	description = new TextArea(420, 60, 200, 290, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", true);
	
	addWidget(title);
	addWidget(ok);
	addWidget(cancel);
	addWidget(fileList);
	addWidget(description);
}



void CampaignSelectorScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			// we accept only if a valid map is selected
			if (fileList->getSelectionIndex()!=-1)
				endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(par1);
		}
	}
	if (action == LIST_ELEMENT_SELECTED)
	{
		if (fileList->getSelectionIndex()!=-1)
		{
			Campaign toload;
			toload.load(getCampaignName());
			description->setText(Toolkit::getStringTable()->getString(toload.getDescription()));
		}
		else
		{
			description->setText("");	
		}
	}
}



std::string CampaignSelectorScreen::getCampaignName()
{
	return fileList->fullName(fileList->getText(fileList->getSelectionIndex()).c_str())+".txt";
}

