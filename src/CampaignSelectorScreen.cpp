/*
  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/


#include "CampaignSelectorScreen.h"
#include "StringTable.h"
#include "Toolkit.h"

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
	addWidget(title);
	addWidget(ok);
	addWidget(cancel);
	addWidget(fileList);
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
}



std::string CampaignSelectorScreen::getCampaignName()
{
	return fileList->fullName(fileList->getText(fileList->getSelectionIndex()).c_str())+".txt";
}

