/*
  Copyright (C) 2008 Bradley Arsenault

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

#include <FormatableString.h>
#include <GUIButton.h>
#include <GUIList.h>
#include "GUIMessageBox.h"
#include "GUITabScreen.h"
#include <GUITextArea.h>
#include "YOGClient.h"
#include <GUIText.h>
#include <GUITextInput.h>
#include "GUIMapPreview.h"
#include "NetMessage.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "YOGClientMapDownloadScreen.h"

using namespace GAGCore;

YOGClientMapDownloadScreen::YOGClientMapDownloadScreen(TabScreen* parent, boost::shared_ptr<YOGClient> client)
	: TabScreenWindow(parent, Toolkit::getStringTable()->getString("[Download Maps]")), client(client)
{
	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Download Maps]")));


	mapList = new List(10, 120, 220, 135, ALIGN_LEFT, ALIGN_FILL, "standard");
	addWidget(mapList);
	mapPreview = new MapPreview(72, 130, ALIGN_RIGHT, ALIGN_TOP);
	addWidget(mapPreview);
	mapName=new Text(72, 268+25, ALIGN_RIGHT, ALIGN_TOP, "standard", "1", 180);
	addWidget(mapName);
	mapInfo=new Text(72, 268+50, ALIGN_RIGHT, ALIGN_TOP, "standard", "2", 180);
	addWidget(mapInfo);
	mapVersion=new Text(72, 268+75, ALIGN_RIGHT, ALIGN_TOP, "standard", "3", 180);
	addWidget(mapVersion);
	mapSize=new Text(72, 268+100, ALIGN_RIGHT, ALIGN_TOP, "standard", "4", 180);
	addWidget(mapSize);
	mapDate=new Text(72, 268+125, ALIGN_RIGHT, ALIGN_TOP, "standard", "5", 180);
	addWidget(mapDate);
	addMap = new TextButton(20, 65, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[upload map]"), ADDMAP);
	addWidget(new TextButton(20, 15, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), QUIT, 27));
	addWidget(addMap);
	validMapSelected=false;
}



void YOGClientMapDownloadScreen::onTimer(Uint32 tick)
{
	
}



void YOGClientMapDownloadScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	TabScreenWindow::onAction(source, action, par1, par2);
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==QUIT)
		{
			endExecute(QUIT);
			parent->completeEndExecute(QUIT);
		}
	}
}



void YOGClientMapDownloadScreen::onActivated()
{
	requestMaps();
}



void YOGClientMapDownloadScreen::requestMaps()
{
	mapList->clear();
	boost::shared_ptr<NetRequestDownloadableMapList> request(new NetRequestDownloadableMapList);
	client->sendNetMessage(request);
}



