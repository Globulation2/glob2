/*
  Copyright (C) 2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "CampaignEditor.h"
#include "CampaignSelectorScreen.h"
#include "ChooseMapScreen.h"
#include "EditorMainMenu.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <GUIText.h>
#include "MapEdit.h"
#include "MapGenerator.h"
#include "NewMapScreen.h"
#include <StringTable.h>
#include <Toolkit.h>
#include "Utilities.h"



using namespace GAGGUI;

EditorMainMenu::EditorMainMenu()
{
	addWidget(new TextButton(0,  70, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[new map]"), NEW_MAP, 13));
	addWidget(new TextButton(0,  130, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[load map]"), LOAD_MAP));
	addWidget(new TextButton(0, 190, 300, 40,  ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[new campaign]"), NEW_CAMPAIGN));
	addWidget(new TextButton(0, 250, 300, 40,  ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[load campaign]"), LOAD_CAMPAIGN));
	addWidget(new TextButton(0, 415, 300, 40, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[goto main menu]"), CANCEL, 27));
	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[editor]")));
}

void EditorMainMenu::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
	    if (par1==NEW_MAP)
		{
			bool retryNewMapScreen=true;
			while (retryNewMapScreen)
			{
				NewMapScreen newMapScreen;
				int rc_nms = newMapScreen.execute(globalContainer->gfx, 40);
				if (rc_nms==NewMapScreen::OK)
				{
					MapEdit mapEdit;
					MapGenerator generator;
					setRandomSyncRandSeed();
					if (generator.generateMap(mapEdit.game, newMapScreen.descriptor))
					{
						mapEdit.mapHasBeenModified(); // make all map as modified by default
						mapEdit.regenerateGameHeader();
						if (mapEdit.run()==-1)
							endExecute(-1);
						retryNewMapScreen=false;
					}
					else
					{
						//TODO: popup a widow to explain that the generateMap() has failed.
						retryNewMapScreen=true;
					}
				}
				else if(rc_nms == -1)
				{
					endExecute(-1);
					retryNewMapScreen=false;
				}
				else
				{
					retryNewMapScreen=false;
				}
			}
		}
		else if (par1==LOAD_MAP)
		{
			ChooseMapScreen chooseMapScreen("maps", "map", false, "games", "game", NULL);
			int rc=chooseMapScreen.execute(globalContainer->gfx, 40);
			if (rc==ChooseMapScreen::OK)
			{
				MapEdit mapEdit;
				std::string filename = chooseMapScreen.getMapHeader().getFileName();
				mapEdit.load(filename.c_str());
				if (mapEdit.run()==-1)
					endExecute(-1);
			}
			else if (rc==-1)
				endExecute(-1);
		}
		else if (par1==NEW_CAMPAIGN)
		{
			CampaignEditor ce("");
			int rc=ce.execute(globalContainer->gfx, 40);
			if(rc == -1)
				endExecute(-1);

		}
		else if (par1==LOAD_CAMPAIGN)
		{
			CampaignSelectorScreen css;
			int rc_css=css.execute(globalContainer->gfx, 40);
			if(rc_css==CampaignSelectorScreen::OK)
			{
				CampaignEditor ce(css.getCampaignName());
				int rc_ce=ce.execute(globalContainer->gfx, 40);
				if(rc_ce == -1)
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
	    else if(par1 == CANCEL)
	    {
	        endExecute(CANCEL);
	    }
	}
}


