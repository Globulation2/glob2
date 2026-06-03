// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "AIDescriptionScreen.h"
#include "GUIButton.h"
#include "GUIList.h"
#include "GUITextArea.h"
#include "GUIText.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "AI.h"
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
		if (auto sel = ailist->selection())
		{
			description->setText(AINames::getAIDescription(*sel).c_str());
		}
	}
}

