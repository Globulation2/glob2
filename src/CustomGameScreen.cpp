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
#include <Stream.h>

CustomGameScreen::CustomGameScreen() :
	ChooseMapScreen("maps", "map", true)
{
	for (int i=0; i<16; i++)
	{
		isAI[i]=new OnOffButton(230, 60+i*25, 21, 21, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, i == 0, 100+i);
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
}

CustomGameScreen::~CustomGameScreen()
{
}

void CustomGameScreen::validMapSelectedhandler(void)
{
	int i;
	// set the correct number of colors
	for (i = 0; i<16; i++)
	{
		color[i]->clearColors();
		for (int j = 0; j<sessionInfo.numberOfTeam; j++)
			color[i]->addColor(sessionInfo.teams[j].colorR, sessionInfo.teams[j].colorG, sessionInfo.teams[j].colorB);
		color[i]->setSelectedColor();
	}
	// find team for human player
	for (i = 0; i<sessionInfo.numberOfTeam; i++)
	{
		if (sessionInfo.teams[i].type == BaseTeam::T_HUMAN)
		{
			color[0]->setSelectedColor(i);
			break;
		}
	}
	// Fill the others
	int c = color[0]->getSelectedColor();
	for (i = 1; i<sessionInfo.numberOfTeam; i++)
	{
		c = (c+1)%sessionInfo.numberOfTeam;
		color[i]->setSelectedColor(c);
		isAI[i]->setState(true);
		closedText[i]->hide();
		aiSelector[i]->show();
	}
	// Close the rest
	for (; i<16; i++)
	{
		isAI[i]->setState(false);
		aiSelector[i]->hide();
		closedText[i]->show();
	}
}

void CustomGameScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	// call parent
	ChooseMapScreen::onAction(source, action, par1, par2);
/*		{
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
	}*/
	if (action==BUTTON_STATE_CHANGED)
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

