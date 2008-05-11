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

#include "ChooseMapScreen.h"
#include <FormatableString.h>
#include "GlobalContainer.h"
#include <GUIButton.h>
#include <GUIList.h>
#include "GUIMapPreview.h"
#include "GUIMessageBox.h"
#include "GUINumber.h"
#include "GUITabScreen.h"
#include <GUITextArea.h>
#include <GUIText.h>
#include <GUITextInput.h>
#include "NetMessage.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "YOGClient.h"
#include "YOGClientMapDownloadScreen.h"
#include "YOGClientMapUploadScreen.h"
#include "YOGClientDownloadableMapList.h"
#include "YOGClientDownloadingMapScreen.h"
#include "YOGClientRatedMapList.h"

using namespace GAGCore;

YOGClientMapDownloadScreen::YOGClientMapDownloadScreen(TabScreen* parent, boost::shared_ptr<YOGClient> client)
	: TabScreenWindow(parent, Toolkit::getStringTable()->getString("[Download Maps]")), client(client)
{
	addWidget(new Text(0, 10, ALIGN_FILL, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[Download Maps]")));


	mapList = new List(20, 120, 220, 135, ALIGN_LEFT, ALIGN_FILL, "standard");
	addWidget(mapList);
	mapPreview = new MapPreview(72, 130, ALIGN_RIGHT, ALIGN_TOP);
	addWidget(mapPreview);
	mapName=new Text(72, 268+25, ALIGN_RIGHT, ALIGN_TOP, "standard", "", 180);
	addWidget(mapName);
	mapInfo=new Text(72, 268+50, ALIGN_RIGHT, ALIGN_TOP, "standard", "", 180);
	addWidget(mapInfo);
	mapSize=new Text(72, 268+75, ALIGN_RIGHT, ALIGN_TOP, "standard", "", 180);
	addWidget(mapSize);
	mapAuthor=new Text(72, 268+100, ALIGN_RIGHT, ALIGN_TOP, "standard", "", 180);
	addWidget(mapAuthor);
	mapRating = new Text(72, 268 + 125, ALIGN_RIGHT, ALIGN_TOP, "standard", "", 180);
	addWidget(mapRating);
	addMap = new TextButton(20, 65, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[upload map]"), ADDMAP);
	addWidget(new TextButton(20, 15, 180, 40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), QUIT, 27));
	addWidget(addMap);
	refresh = new TextButton(20, 65, 220, 40, ALIGN_LEFT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[refresh map list]"), REFRESHMAPLIST);
	addWidget(refresh);
	downloadMap = new TextButton(20, 15, 220, 40, ALIGN_LEFT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[Download Map]"), DOWNLOADMAP);
	addWidget(downloadMap);
	
	loadingMapList = new Text(280, 200, ALIGN_LEFT, ALIGN_TOP, "menu", Toolkit::getStringTable()->getString("[loading map list]"));
	addWidget(loadingMapList);
	
	submitRating = new TextButton(250, 65, 220, 40, ALIGN_LEFT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[submit rating]"), SUBMITRATING);
	addWidget(submitRating);
	rating = new Number(250, 35, 220, 20, ALIGN_LEFT, ALIGN_BOTTOM, 10, "standard");
	for(int i=1; i<=10; ++i)
	{
		rating->add(i);
	}
	addWidget(rating);
	mapRatedAlready = new Text(250, 65, ALIGN_LEFT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[map rated]"));
	addWidget(mapRatedAlready);
	submitRating->visible=false;
	rating->visible=false;
	mapRatedAlready->visible=false;

	
	validMapSelected=false;
	client->getDownloadableMapList()->addListener(this);
	mapValid=false;
	mapsRequested=false;
}


YOGClientMapDownloadScreen::~YOGClientMapDownloadScreen()
{
	client->getDownloadableMapList()->removeListener(this);
}

void YOGClientMapDownloadScreen::onTimer(Uint32 tick)
{
	updateVisibility();
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
		else if(par1==ADDMAP)
		{
			ChooseMapScreen cms("maps", "map", false);
			int rc = cms.execute(globalContainer->gfx, 40);
			if(rc == -1)
			{
				endExecute(-1);
				parent->completeEndExecute(-1);
			}
			else if(rc == ChooseMapScreen::OK)
			{
				YOGClientMapUploadScreen upload(client, cms.getMapHeader().getFileName());
				upload.execute(globalContainer->gfx, 40);
				requestMaps();
			}
		}
		else if (par1==REFRESHMAPLIST)
		{
			requestMaps();
		}
		else if (par1==DOWNLOADMAP)
		{
			if(mapValid)
			{
				YOGClientDownloadingMapScreen screen(client, client->getDownloadableMapList()->getMap(mapList->get()));
				int rc = screen.execute(globalContainer->gfx, 40);
				if(rc == -1)
				{
					endExecute(-1);
					parent->completeEndExecute(-1);
				}
				else if(rc == YOGClientDownloadingMapScreen::FINISHED)
				{
				
				}
			}
		}
		else if (par1==SUBMITRATING)
		{
			client->getDownloadableMapList()->submitRating(mapList->get(), rating->get());
			client->getRatedMapList()->addRatedMap(mapList->get());
		}
	}
	if(action == LIST_ELEMENT_SELECTED)
	{
		updateMapInfo();
	}
}



void YOGClientMapDownloadScreen::onActivated()
{
	if(!mapsRequested)
	{
		requestMaps();
	}
	updateVisibility();
}



void YOGClientMapDownloadScreen::mapListUpdated()
{
	mapList->clear();
	std::vector<YOGDownloadableMapInfo>& maps = client->getDownloadableMapList()->getDownloadableMapList();
	for(int i=0; i<maps.size(); ++i)
	{
		mapList->addText(maps[i].getMapHeader().getMapName());
	}
}


void YOGClientMapDownloadScreen::mapThumbnailsUpdated()
{
	updateMapInfo();
}



void YOGClientMapDownloadScreen::requestMaps()
{
	mapList->clear();
	mapList->setSelectionIndex(-1);
	updateMapInfo();
	
	client->getDownloadableMapList()->requestMapListUpdate();
	mapsRequested=true;
}



void YOGClientMapDownloadScreen::updateMapInfo()
{
	if(mapList->getSelectionIndex() != -1)
		mapValid=true;
	else
		mapValid=false;

	updateMapPreview();
	if(mapValid)
	{
		YOGDownloadableMapInfo info = client->getDownloadableMapList()->getMap(mapList->get());
		const MapHeader& mapHeader = info.getMapHeader();
		// update map name & info
		mapName->setText(mapHeader.getMapName());
		std::string textTemp;
		textTemp = FormatableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(Toolkit::getStringTable()->getString("[teams]"));
		mapInfo->setText(textTemp);
		textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
		mapSize->setText(textTemp);
		mapAuthor->setText(info.getAuthorName());
		if(info.getNumberOfRatings() > 5)
		{
			textTemp = FormatableString(Toolkit::getStringTable()->getString("[Rated %0]")).arg(info.getRatingTotal() / info.getNumberOfRatings());
		}
		else
		{
			textTemp = FormatableString(Toolkit::getStringTable()->getString("[Not Enough Ratings]"));
		}
		mapRating->setText(textTemp);
		if(!client->getDownloadableMapList()->getMapThumbnail(mapList->get()).isLoaded())
		{
			client->getDownloadableMapList()->requestThumbnail(mapList->get());
		}
	}
	else
	{
		mapAuthor->setText("");
		mapInfo->setText("");
		mapSize->setText("");
		mapName->setText("");
		mapRating->setText("");
	}
}



void YOGClientMapDownloadScreen::updateVisibility()
{
	if(client->getDownloadableMapList()->waitingForListFromServer())
	{
		loadingMapList->visible=isActivated();
	}
	else
	{
		loadingMapList->visible=false;
	}
	if(mapValid)
	{
		if(client->getRatedMapList()->isMapRated(mapList->get()))
		{
			submitRating->visible=false;
			rating->visible=false;
			mapRatedAlready->visible=isActivated();
		}
		else
		{
			submitRating->visible=isActivated();
			rating->visible=isActivated();
			mapRatedAlready->visible=false;
		}
	}
	else
	{
		submitRating->visible=false;
		rating->visible=false;
		mapRatedAlready->visible=false;
	}
}



void YOGClientMapDownloadScreen::updateMapPreview()
{
	if(mapValid)
	{
		MapThumbnail& thumbnail = client->getDownloadableMapList()->getMapThumbnail(mapList->get());
		if(thumbnail.isLoaded())
		{
			mapPreview->setMapThumbnail(thumbnail);
		}
		else
		{
			mapPreview->setMapThumbnail("");
		}
	}
	else
	{
	
		mapPreview->setMapThumbnail("");
	}
}

