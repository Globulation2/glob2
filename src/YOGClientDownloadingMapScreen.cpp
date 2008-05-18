/*
  Copyright 2008 Bradley Arsenault

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

#include "YOGClientDownloadingMapScreen.h"

#include <algorithm>
#include "Engine.h"
#include "FormatableString.h"
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <GUIList.h>
#include "GUIMapPreview.h"
#include "GUIMessageBox.h"
#include "GUITabScreen.h"
#include <GUIText.h>
#include <GUITextInput.h>
#include <GUIProgressBar.h>
#include "MapHeader.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "YOGClientBlockedList.h"
#include "YOGClient.h"
#include "YOGClientDownloadingMapScreen.h"
#include "YOGClientDownloadableMapList.h"

using namespace GAGCore;

YOGClientDownloadingMapScreen::YOGClientDownloadingMapScreen(boost::shared_ptr<YOGClient> client, const YOGDownloadableMapInfo& info)
	: client(client), info(info), downloader(client)
{
	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[downloading map]")));
	addWidget(new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));
	preview = new MapPreview(20, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(preview);
	
	mapName=new Text(173, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(173, 60+30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapSize=new Text(173, 60+60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);
	authorName=new Text(173, 60+90,  ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(authorName);
	
	downloadStatus=new ProgressBar(20, 300, 600, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	downloadStatus->visible = false;
	addWidget(downloadStatus);
	
	// update map name & info
	MapHeader mapHeader = info.getMapHeader();
	mapName->setText(mapHeader.getMapName());
	std::string textTemp;
	textTemp = FormatableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(Toolkit::getStringTable()->getString("[teams]"));
	mapInfo->setText(textTemp);
	textTemp = FormatableString("%0 x %1").arg(preview->getLastWidth()).arg(preview->getLastHeight());
	mapSize->setText(textTemp);
	authorName->setText(info.getAuthorName());
	
	downloader.startDownloading(info);
}





void YOGClientDownloadingMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			downloader.cancelDownload();
			endExecute(CANCEL);
		}
	}
}



void YOGClientDownloadingMapScreen::onTimer(Uint32 tick)
{
	client->update();
	downloader.update();
	if(!client->isConnected())
	{
		GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Map download failure: connection lost]"), Toolkit::getStringTable()->getString("[ok]"));
		endExecute(CONNECTIONLOST);
	}
	
	downloadStatus->visible = false;
	if(downloader.getDownloadingState() == YOGClientMapDownloader::DownloadingMap)
	{
		downloadStatus->visible = true;
		int p = downloader.getPercentUploaded();
		if( p == 100)
			p = 0;
		downloadStatus->setValue(p);
	}
	else if(downloader.getDownloadingState() == YOGClientMapDownloader::Finished)
	{
		endExecute(FINISHED);
	}
	
	if(!preview->isThumbnailLoaded())
	{
		MapThumbnail& thumbnail = client->getDownloadableMapList()->getMapThumbnail(info.getMapHeader().getMapName());
		if(thumbnail.isLoaded())
		{
			preview->setMapThumbnail(thumbnail);
			std::string textTemp;
			textTemp = FormatableString("%0 x %1").arg(preview->getLastWidth()).arg(preview->getLastHeight());
			mapSize->setText(textTemp);
		}
	}
}

