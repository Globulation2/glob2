/*
  Copyright (C) 2008 Bradley Arenault

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

#include "AIDescriptionScreen.h"
#include "GUIButton.h"
#include "GUIList.h"
#include "GUITextArea.h"
#include "GUIText.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "AINames.h"

AIDescriptionScreen::AIDescriptionScreen()
{
	ok = new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);

	ailist = new List(60, 50, 200, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	for (int aii=0; aii<AI::SIZE; aii++)
		ailist->addText(AINames::getAIText(aii));
	addWidget(ailist);

	description = new TextArea(310, 50, 250, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", true);
	addWidget(description);

	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[AI Descriptions]"));
	addWidget(title);
}



void AIDescriptionScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if(par1 == OK)
		{
			endExecute(OK);
		}
	}
	if (action == LIST_ELEMENT_SELECTED)
	{
		if(ailist->getSelectionIndex() != -1)
		{
			description->setText(AINames::getAIDescription(ailist->getSelectionIndex()).c_str());
		}
	}
}

