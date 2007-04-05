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

#include "MultiplayersChooseMapScreen.h"
#include "Utilities.h"
#include "YOG.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "GUIMapPreview.h"

#include <FormatableString.h>
#include <GUIButton.h>
#include <GUIText.h>
#include <GUIList.h>
#include <Toolkit.h>
#include <StringTable.h>
#include "GUIGlob2FileList.h"

MultiplayersChooseMapScreen::MultiplayersChooseMapScreen(bool shareOnYOG)
{
	this->shareOnYOG=shareOnYOG;

	ok=new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	cancel=new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	toogleButton=new TextButton(240, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "", -1, -1, "menu", Toolkit::getStringTable()->getString("[the games]"), TOOGLE);
	mapPreview=new MapPreview(640-20-26-128, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	title=new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]"));

	addWidget(ok);
	addWidget(cancel);
	addWidget(toogleButton);
	addWidget(mapPreview);
	addWidget(title);

	mapName=new Text(440, 60+128+25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(440, 60+128+50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(440, 60+128+75, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapVersion);
	mapSize=new Text(440, 60+128+100, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);
	mapDate=new Text(440, 60+128+125, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapDate);
	/*methode=new Text(440, 60+128+150, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(methode);*/


	mapFileList=new Glob2FileList(20, 60, 200, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "maps", "map", true);
	addWidget(mapFileList);

	gameFileList=new Glob2FileList(20, 60, 200, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "games", "game", true);
	addWidget(gameFileList);
	
	mapMode=true;
	mapFileList->visible=mapMode;
	gameFileList->visible=!mapMode;
	validSessionInfo=false;
	
	globalContainer->settings.tempVarPrestige = 0;
	useNewPrestige = false;
	/*useVarPrestige=new OnOffButton(466, 37, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, useNewPrestige, useNewPrestige);
	addWidget(useVarPrestige);
	varPrestigeText=new Text(460, 37, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "standard", "Custom Prestige");
	addWidget(varPrestigeText);*/
	
	prestigeRatio=new Number(466, 20, 100, 18, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 18, "menu");
	prestigeRatio->add(0);
	prestigeRatio->add(0);
	prestigeRatio->add(100);
	prestigeRatio->add(200);
	prestigeRatio->add(300);
	prestigeRatio->add(400);
	prestigeRatio->add(500);
	prestigeRatio->add(600);
	prestigeRatio->add(700);
	prestigeRatio->add(800);
	prestigeRatio->add(900);
	prestigeRatio->add(1000);
	prestigeRatio->add(1100);
	prestigeRatio->add(1200);
	prestigeRatio->add(1300);
	prestigeRatio->add(1400);
	prestigeRatio->add(1500);
	prestigeRatio->add(1600);
	prestigeRatio->add(1700);
	prestigeRatio->add(1800);
	prestigeRatio->add(1900);
	prestigeRatio->add(2000);
	prestigeRatio->setNth(1);
	prestigeRatio->visible=false; 
	addWidget(prestigeRatio);
	/*due to an inability by me to synchronize varPrestige in multiplayer games this
	 * feature has been disabled completely in multiplayer games. In order to 
	 * activate custom prestige for multiplayer remember to remove line
	 * globalContainer->settings.tempVarPrestige = 3000
	 * within both MultiplayersChooseMapScreen.cpp and MultiplayersJoinMapScreen.cpp
	 * custom prestige settings currently have 3 range of value. A value of 0 results
	 * in infinite prestige (essentially turning it off). A value > 2000 (I use 3000)
	 * results in game reverting to original prestige calculations. Finally, any
	 * value > 0 and <= 2000 is a set custom prestige setting. 
	 */
}

MultiplayersChooseMapScreen::~MultiplayersChooseMapScreen()
{
}

void MultiplayersChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==LIST_ELEMENT_SELECTED)
	{
		const char *mapSelectedName=static_cast<Glob2FileList *>(source)->getText(par1).c_str();
		std::string mapFileName;
		if (mapMode)
		{
			mapFileName = glob2NameToFilename("maps", mapSelectedName, "map");
			if (verbose)
				std::cout << "MultiplayersChooseMapScreen::onAction : loading map " << mapFileName << std::endl;
		}
		else
		{
			mapFileName = glob2NameToFilename("games", mapSelectedName, "game");
			if (verbose)
				std::cout << "MultiplayersChooseMapScreen::onAction : loading game " << mapFileName << std::endl;
		}
		mapPreview->setMapThumbnail(mapFileName.c_str());

		InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapFileName));
		if (stream->isEndOfStream())
		{
			std::cerr << "MultiplayersChooseMapScreen::onAction() : error, can't open file " << mapFileName<< std::endl;
		}
		else
		{
			if (verbose)
				std::cout << "MultiplayersChooseMapScreen::onAction : loading map " << mapFileName << std::endl;
			validSessionInfo = sessionInfo.load(stream);
			
			if (validSessionInfo)
			{
				// update map name & info
				mapName->setText(sessionInfo.getMapName());
				std::string textTemp;
				textTemp = FormatableString("%0%1").arg(sessionInfo.numberOfTeam).arg(Toolkit::getStringTable()->getString("[teams]"));
				mapInfo->setText(textTemp);
				textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(sessionInfo.versionMajor).arg(sessionInfo.versionMinor);
				mapVersion->setText(textTemp);
				textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
				mapSize->setText(textTemp);
				std::time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
				mapDate->setText(std::ctime(&mtime));
				//methode->setText(mapPreview->getMethode());
			}
			else
				std::cerr << "MultiplayersChooseMapScreen::onAction : invalid Session info for map " << mapFileName << std::endl;
		}
		delete stream;
	}
	else if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (source==ok)
		{
			if (validSessionInfo)
			{
				//remove following when reactivating custom prestige settings
				globalContainer->settings.tempVarPrestige = 3000;
				endExecute(OK);
			}
			else
				std::cerr << "MultiplayersChooseMapScreen::onAction : No valid game selected" << std::endl;
		}
		else if (source==cancel)
		{
			endExecute(par1);
		}
		else if (source==toogleButton)
		{
			mapMode=!mapMode;
			if (mapMode)
			{
				gameFileList->hide();
				mapFileList->show();
				title->setText(Toolkit::getStringTable()->getString("[choose map]"));
				toogleButton->setText(Toolkit::getStringTable()->getString("[the games]"));
			}
			else
			{
				mapFileList->hide();
				gameFileList->show();
				title->setText(Toolkit::getStringTable()->getString("[choose game]"));
				toogleButton->setText(Toolkit::getStringTable()->getString("[the maps]"));
			}
		}
	}
	else if (action==NUMBER_ELEMENT_SELECTED)
	{
		if (prestigeRatio->getNth() == 0)
		{
			prestigeRatio->setNth(1);
		}
		globalContainer->settings.tempVarPrestige=(prestigeRatio->getNth() - 1) * 100;
	}
	else if (action==BUTTON_STATE_CHANGED)
	{
		if (useVarPrestige->getState() == false)
		{
			prestigeRatio->visible=false;
			globalContainer->settings.tempVarPrestige = 3000;
		}
		else if (useVarPrestige->getState() == true)
		{
			prestigeRatio->visible=true;
			globalContainer->settings.tempVarPrestige=(prestigeRatio->getNth() - 1) * 100;
		}
	}
}

void MultiplayersChooseMapScreen::onTimer(Uint32 tick)
{
	if (shareOnYOG)
		yog->step();
}
