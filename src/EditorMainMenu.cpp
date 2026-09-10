// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "EditorMainMenu.h"
#include "CampaignEditor.h"
#include "CampaignSelectorScreen.h"
#include "ChooseMapScreen.h"
#include "FrontendTheme.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "NewMapScreen.h"
#include "Utilities.h"
#include <GUIButton.h>
#include <GUIMessageBox.h>
#include <GUIText.h>
#include <StringTable.h>
#include <Toolkit.h>

using namespace GAGGUI;

EditorMainMenu::EditorMainMenu()
{
	addWidget(new TextButton(0, 70, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							 Toolkit::getStringTable()->getString("[new map]"), NEWMAP, 13));
	addWidget(new TextButton(0, 130, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							 Toolkit::getStringTable()->getString("[load map]"), LOADMAP));
	addWidget(new TextButton(0, 190, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							 Toolkit::getStringTable()->getString("[new campaign]"), NEWCAMPAIGN));
	addWidget(new TextButton(0, 250, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							 Toolkit::getStringTable()->getString("[load campaign]"),
							 LOADCAMPAIGN));
	addWidget(new TextButton(0, 415, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu",
							 Toolkit::getStringTable()->getString("[goto main menu]"), CANCEL, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu",
					   Toolkit::getStringTable()->getString("[editor]")));
}

void EditorMainMenu::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (par1 == NEWMAP)
		{
			bool retryNewMapScreen = true;
			while (retryNewMapScreen)
			{
				NewMapScreen newMapScreen;
				int rc_nms = newMapScreen.execute(globalContainer->gfx, 40);
				if (rc_nms == NewMapScreen::OK)
				{
					MapEdit mapEdit;
					GenerationService generator;
					newMapScreen.descriptor.seed = GenerationContext::randomSeed();
					auto result = generator.generate(mapEdit.game, newMapScreen.descriptor);
					if (result)
					{
						mapEdit.mapHasBeenModified(); // make all map as modified by default
						mapEdit.regenerateGameHeader();
						if (mapEdit.run() == -1)
							endExecute(-1);
						retryNewMapScreen = false;
					}
					else
					{
						GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON,
										   result.diagnostic(),
										   Toolkit::getStringTable()->getString("[ok]"));
						retryNewMapScreen = true;
					}
				}
				else if (rc_nms == -1)
				{
					endExecute(-1);
					retryNewMapScreen = false;
				}
				else
				{
					retryNewMapScreen = false;
				}
			}
		}
		else if (par1 == LOADMAP)
		{
			ChooseMapScreen chooseMapScreen("maps", "map", false, "games", "game", false);
			int rc = chooseMapScreen.execute(globalContainer->gfx, 40);
			if (rc == ChooseMapScreen::OK)
			{
				MapEdit mapEdit;
				std::string filename = chooseMapScreen.getMapHeader().getFileName();
				mapEdit.load(filename.c_str());
				if (mapEdit.run() == -1)
					endExecute(-1);
			}
			else if (rc == -1)
				endExecute(-1);
		}
		else if (par1 == NEWCAMPAIGN)
		{
			FrontendScope editor(false);
			CampaignEditor ce("");
			int rc = ce.execute(globalContainer->gfx, 40);
			if (rc == -1)
				endExecute(-1);
		}
		else if (par1 == LOADCAMPAIGN)
		{
			CampaignSelectorScreen css;
			int rc_css = css.execute(globalContainer->gfx, 40);
			if (rc_css == CampaignSelectorScreen::OK)
			{
				FrontendScope editor(false);
				CampaignEditor ce(css.getCampaignName());
				int rc_ce = ce.execute(globalContainer->gfx, 40);
				if (rc_ce == -1)
				{
					endExecute(-1);
				}
			}
			else if (rc_css == CampaignSelectorScreen::CANCEL)
			{
			}
			else if (rc_css == -1)
			{
				endExecute(-1);
			}
		}
		else if (par1 == CANCEL)
		{
			endExecute(CANCEL);
		}
	}
}
