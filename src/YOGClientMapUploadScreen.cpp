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
#include "YOGClientMapUploadScreen.h"

using namespace GAGCore;

YOGClientMapUploadScreen::YOGClientMapUploadScreen(boost::shared_ptr<YOGClient> client, const std::string mapFile)
	: client(client), uploader(client), mapFile(mapFile)
{
	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Upload Map]")));
	addWidget(new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27));
	addWidget(new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Upload Map]"), UPLOAD, 27));
	preview = new MapPreview(20, 60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(preview);
	
	mapName=new TextInput(173, 60, 150, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", false, 255);
	addWidget(mapName);
	mapInfo=new Text(173, 60+30, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapInfo);
	mapVersion=new Text(173, 60+60, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapVersion);
	mapSize=new Text(173, 60+90, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapSize);
	mapDate=new Text(173, 60+120, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", "", 180);
	addWidget(mapDate);
	authorNameText=new Text(20, 60+150,  ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Map Upload: Author Name]"), 180);
	addWidget(authorNameText);
	authorName=new TextInput(173, 60+150, 150, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", globalContainer->settings.getUsername(), false, 255);
	addWidget(authorName);
	
	//uploadStatus=new Text(248, 60+300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", "", 180);
	uploadStatus = new ProgressBar(20, 300, 600, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	uploadStatus->visible = false;
	addWidget(uploadStatus);
	uploadStatusText = new Text(0, 300, ALIGN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", ""); 
	addWidget(uploadStatusText);
	
	// update map name & info
	preview->setMapThumbnail(mapFile.c_str());
	Engine engine;
	MapHeader mapHeader = engine.loadMapHeader(mapFile);
	mapName->setText(mapHeader.getMapName());
	std::string textTemp;
	textTemp = FormatableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(Toolkit::getStringTable()->getString("[teams]"));
	mapInfo->setText(textTemp);
	textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(mapHeader.getVersionMajor()).arg(mapHeader.getVersionMinor());
	mapVersion->setText(textTemp);
	textTemp = FormatableString("%0 x %1").arg(preview->getLastWidth()).arg(preview->getLastHeight());
	mapSize->setText(textTemp);
	isUploading = false;
}





void YOGClientMapUploadScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
	{
		if (par1==CANCEL)
		{
			client->update();
			uploader.cancelUpload();
			endExecute(CANCEL);
		}
		else if (par1==UPLOAD)
		{
			if(!isUploading)
			{
				uploader.startUploading(mapFile.c_str(), mapName->getText(), authorName->getText(), preview->getLastWidth(), preview->getLastHeight());
				isUploading=true;
			}
		}
	}
	if (action==TEXT_ACTIVATED)
	{
		if (source==authorName)
			mapName->deactivate();
		else if (source==mapName)
			authorName->deactivate();
	}
}



void YOGClientMapUploadScreen::onTimer(Uint32 tick)
{
	client->update();
	uploader.update();
	if(!client->isConnected())
	{
		GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Map upload failure: connection lost]"), Toolkit::getStringTable()->getString("[ok]"));
		endExecute(CONNECTIONLOST);
	}
	
	uploadStatus->visible = false;
	if(isUploading)
	{
		if(uploader.getUploadingState() == YOGClientMapUploader::Nothing)
		{
			if(uploader.getRefusalReason() == YOGMapUploadReasonMapNameAlreadyExists)
			{
				GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Map upload failure: map name in use]"), Toolkit::getStringTable()->getString("[ok]"));
			}
			else
			{
				GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[Map upload failure: unknown reason]"), Toolkit::getStringTable()->getString("[ok]"));
			}
			endExecute(UPLOADFAILED);
		}
		else if(uploader.getUploadingState() == YOGClientMapUploader::WaitingForUploadReply)
		{
			uploadStatusText->setText(FormatableString(Toolkit::getStringTable()->getString("[Map Upload: Waiting for reply]")));
		}
		else if(uploader.getUploadingState() == YOGClientMapUploader::Finished)
		{
			endExecute(UPLOADFINISHED);
		}
		else
		{
			uploadStatus->visible = true;
			uploadStatus->setValue(uploader.getPercentUploaded());
			uploadStatusText->setText("");
			//uploadStatus->setText(FormatableString(Toolkit::getStringTable()->getString("[%0% Uploaded]")).arg(uploader.getPercentUploaded()));
		}
	}
}


