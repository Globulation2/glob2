/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "CustomGameScreen.h"
#include "Utilities.h"
#include "Game.h"
#include "GUIGlob2FileList.h"
#include "GUIMapPreview.h"
#include <GUIButton.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>

CustomGameScreen::CustomGameScreen()
{
	ok=new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	fileList=new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "maps", "map", true);
	mapPreview=new MapPreview(640-20-26-128, 70, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, NULL);

	addWidget(new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]")));
	mapName=new Text(440, 60+128+30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);

	addWidget(ok);
	addWidget(cancel);
	addWidget(mapPreview);

	for (int i=0; i<16; i++)
	{
		isAI[i]=new OnOffButton(230, 60+i*25, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, true, 100+i);
		addWidget(isAI[i]);
		color[i]=new ColorButton(265, 60+i*25, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 200+i);
		addWidget(color[i]);
		if (i==0)
		{
			closedText[i]=new Text(300, 60+i*25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[player]"));
			addWidget(closedText[i]);
			
			aiSelector[i]=NULL;
		}
		else
		{
			closedText[i]=new Text(300, 60+i*25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[closed]"));
			addWidget(closedText[i]);
			
			aiSelector[i]=new MultiTextButton(300, 60+i*25, 100, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[AI]"), 300+i);
			for (int aii=0; aii<AI::SIZE; aii++)
				aiSelector[i]->addText(Toolkit::getStringTable()->getString("[AI]", aii));
			addWidget(aiSelector[i]);
			aiSelector[i]->hide();
			aiSelector[i]->setFirstTextIndex(AI::NUMBI);
		}
	}

	addWidget(fileList);

	validSessionInfo=false;
}

CustomGameScreen::~CustomGameScreen()
{
}

void CustomGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==LIST_ELEMENT_SELECTED)
	{
		const char *mapSelectedName=fileList->getText(par1);
		if (mapSelectedName)
		{
			const char *mapFileName=fileList->listToFile(mapSelectedName);

			mapPreview->setMapThumbnail(mapFileName);
			printf("CGS : Loading map '%s' ...\n", mapFileName);
			SDL_RWops *stream=Toolkit::getFileManager()->open(mapFileName,"rb");
			if (stream==NULL)
				printf("Map '%s' not found!\n", mapFileName);
			else
			{
				validSessionInfo=sessionInfo.load(stream);
				SDL_RWclose(stream);
				if (validSessionInfo)
				{
					// update map name & info
					mapName->setText(sessionInfo.getMapName());
					char textTemp[256];
					snprintf(textTemp, 256, "%d%s", sessionInfo.numberOfTeam, Toolkit::getStringTable()->getString("[teams]"));
					mapInfo->setText(textTemp);
					snprintf(textTemp, 256, "%s %d.%d", Toolkit::getStringTable()->getString("[Version]"), sessionInfo.versionMajor, sessionInfo.versionMinor);
					mapVersion->setText(textTemp);
					snprintf(textTemp, 256, "%d x %d", mapPreview->getLastWidth(), mapPreview->getLastHeight());
					mapSize->setText(textTemp);
					
					int nbTeam=sessionInfo.numberOfTeam;
					int i;
					// set the correct number of colors
					for (i=0; i<16; i++)
					{
						color[i]->clearColors();
						for (int j=0; j<nbTeam; j++)
							color[i]->addColor(sessionInfo.teams[j].colorR, sessionInfo.teams[j].colorG, sessionInfo.teams[j].colorB);
						color[i]->setSelectedColor();
					}
					// find team for human player
					for (i=0; i<nbTeam; i++)
					{
						if (sessionInfo.teams[i].type==BaseTeam::T_HUMAN)
						{
							color[0]->setSelectedColor(i);
							break;
						}
					}
					// Fill the others
					int c=color[0]->getSelectedColor();
					for (i=1; i<nbTeam; i++)
					{
						c=(c+1)%nbTeam;
						color[i]->setSelectedColor(c);
						isAI[i]->setState(true);
						closedText[i]->hide();
						aiSelector[i]->show();
					}
					// Close the rest
					for (;i<16; i++)
					{
						isAI[i]->setState(false);
						aiSelector[i]->hide();
						closedText[i]->show();
					}
				}
				else
					printf("CGS : Warning, Error during map load\n");
			}
			delete[] mapFileName;
		}
		else
		{
			// Reset
			mapPreview->setMapThumbnail(NULL);
			mapName->setText("");
			mapInfo->setText("");
			mapVersion->setText("");
			mapSize->setText("");
			isAI[0]->setState(true);
			color[0]->setSelectedColor();
			
			for (int i=1;i<16; i++)
			{
				isAI[i]->setState(true);
				color[i]->setSelectedColor();
				closedText[i]->show();
				aiSelector[i]->hide();
			}
		}
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (source==ok)
		{
			if (validSessionInfo)
				endExecute(OK);
			else
				printf("CGS : This is not a valid map!\n");
		}
		else if (source==cancel)
		{
			endExecute(par1);
		}
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if (par1==100)
		{
			isAI[0]->setState(true);
		}
		else if ((par1>100) && (par1<200))
		{
			int n=par1-100;
			if (isAI[n]->getState())
			{
				closedText[n]->hide();
				aiSelector[n]->show();
			}
			else
			{
				closedText[n]->show();
				aiSelector[n]->hide();
			}
		}
	}
}

bool CustomGameScreen::isAIactive(int i)
{
	return isAI[i]->getState();
}

AI::ImplementitionID CustomGameScreen::getAiImplementation(int i)
{
	return (AI::ImplementitionID)aiSelector[i]->getTextIndex();
}

int CustomGameScreen::getSelectedColor(int i)
{
	return color[i]->getSelectedColor();
}

