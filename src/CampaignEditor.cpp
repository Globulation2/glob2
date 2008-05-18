/*
  Copyright (C) 2006 Bradley Arsenault

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
#include "Toolkit.h"
#include "StringTable.h"
#include "ChooseMapScreen.h"
#include "Game.h"
#include <set>
#include <algorithm>


CampaignEditor::CampaignEditor(const std::string& name)
{
	if(name!="")
		campaign.load(name);
	StringTable& table=*Toolkit::getStringTable();
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", table.getString("[campaign editor]"));
	mapList = new List(10, 50, 300, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	addMap = new TextButton(10, 360, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[add map]"), ADDMAP);
	editMap = new TextButton(165, 360, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[edit map]"), EDITMAP);
	removeMap = new TextButton(10, 410, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[remove map]"), REMOVEMAP);
	nameEditor = new TextInput(320, 60, 310, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", campaign.getName());
	ok = new TextButton(260, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[ok]"), OK);
	cancel = new TextButton(450, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[Cancel]"), CANCEL);
	addWidget(title);
	addWidget(mapList);
	addWidget(addMap);
	addWidget(editMap);
	addWidget(removeMap);
	addWidget(nameEditor);
	addWidget(ok);
	addWidget(cancel);
	syncMapList();
}




void CampaignEditor::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			campaign.save();
			endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(CANCEL);
		}
		else if (source == addMap)
		{
			ChooseMapScreen cms("campaigns", "map", false);
			int rcms=cms.execute(gfx, 40);
			if(rcms==ChooseMapScreen::OK)
			{
				MapHeader& mapHeader = cms.getMapHeader();
				CampaignMapEntry cme(mapHeader.getMapName(), glob2NameToFilename("campaigns", mapHeader.getMapName(), "map"));
				CampaignMapEntryEditor cmee(campaign, cme);
				int rcmee = cmee.execute(gfx, 40);
				if(rcmee==CampaignMapEntryEditor::OK)
				{
					campaign.appendMap(cme);
					mapList->addText(mapHeader.getMapName());
				}
				else if(rcmee==CampaignMapEntryEditor::CANCEL)
				{

				}
				else if(rcmee == -1)
				{
					endExecute(-1);
				}
			}
			else if(rcms==ChooseMapScreen::CANCEL)
			{
			}
			else if(rcms==-1)
			{
				endExecute(-1);
			}
		}
		else if (source == editMap)
		{
			for(unsigned i=0; i<campaign.getMapCount(); ++i)
			{
				if(mapList->getSelectionIndex()!=-1 && campaign.getMap(i).getMapName()==mapList->get())
				{
					CampaignMapEntryEditor cmee(campaign, campaign.getMap(i));
					int rcmee = cmee.execute(gfx, 40);
					if(rcmee==CampaignMapEntryEditor::OK)
					{
						mapList->setText(mapList->getSelectionIndex(), campaign.getMap(i).getMapName());
					}
					else if(rcmee==CampaignMapEntryEditor::CANCEL)
					{
					}
				}
			}
		}
		else if (source == removeMap)
		{
			if(mapList->getSelectionIndex()!=-1)
			{
				for(unsigned i=0; i<campaign.getMapCount(); ++i)
				{
					std::vector<std::string>::iterator iter=std::find(campaign.getMap(i).getUnlockedByMaps().begin(), campaign.getMap(i).getUnlockedByMaps().end(), mapList->get());
					if(iter!=campaign.getMap(i).getUnlockedByMaps().end())
					{
						campaign.getMap(i).getUnlockedByMaps().erase(iter);
					}
				}
				campaign.removeMap(mapList->getSelectionIndex());
				mapList->removeText(mapList->getSelectionIndex());
			}
		}
	}
	else if (action == TEXT_MODIFIED)
	{
		if(source==nameEditor)
		{
			campaign.setName(nameEditor->getText());
		}
	}
}



void CampaignEditor::syncMapList()
{
	for(unsigned n=0; n<campaign.getMapCount(); n++)
	{
		mapList->addText(campaign.getMap(n).getMapName());
	}
}


CampaignMapEntryEditor::CampaignMapEntryEditor(Campaign& campaign, CampaignMapEntry& mapEntry) : entry(mapEntry), campaign(campaign)
{
	StringTable& table=*Toolkit::getStringTable();
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", table.getString("[editing map]"));
	mapsUnlockedBy = new List(10, 80, 150, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	mapsAvailable = new List(230, 80, 150, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	mapsUnlockedByLabel = new Text(10, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[unlocked by]"));
	mapsAvailableLabel = new Text(230, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[maps available]"));
	nameEditor=new TextInput(420, 105, 180, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", entry.getMapName());
	nameEditorLabel = new Text(405, 80, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[map name]"));
	descriptionEditor = new TextInput(420, 165, 180, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", entry.getDescription().c_str());
	descriptionEditorLabel = new Text(405, 140, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[map description]"));
	ok = new TextButton(260, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[ok]"), OK);
	cancel = new TextButton(450, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[Cancel]"), CANCEL);
	addToUnlocked = new TextButton(170, 150, 50, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", "<", ADDTOUNLOCKED);
	removeFromUnlocked = new TextButton(170, 210, 50, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", ">", REMOVEFROMUNLOCKED);
	std::set<std::string> unlockedBy;
	for(unsigned n=0; n<entry.getUnlockedByMaps().size(); ++n)
	{
		mapsUnlockedBy->addText(entry.getUnlockedByMaps()[n]);
		unlockedBy.insert(entry.getUnlockedByMaps()[n]);
	}
	for(unsigned n=0; n<campaign.getMapCount(); ++n)
	{
		if(unlockedBy.find(campaign.getMap(n).getMapName())==unlockedBy.end())
		{
			if(campaign.getMap(n).getMapName() != entry.getMapName())
			{
				mapsAvailable->addText(campaign.getMap(n).getMapName());
			}
		}
	}
	addWidget(title);
	addWidget(mapsUnlockedBy);
	addWidget(mapsAvailable);
	addWidget(mapsUnlockedByLabel);
	addWidget(mapsAvailableLabel);
	addWidget(addToUnlocked);
	addWidget(removeFromUnlocked);
	addWidget(nameEditor);
	addWidget(nameEditorLabel);
	addWidget(descriptionEditor);
	addWidget(descriptionEditorLabel);
	addWidget(ok);
	addWidget(cancel);
}



void CampaignMapEntryEditor::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			///If the maps name was changes, make sure to change it in all of the other map entries
			for(unsigned i=0; i<campaign.getMapCount(); ++i)
			{
				std::vector<std::string>::iterator iter=std::find(campaign.getMap(i).getUnlockedByMaps().begin(), campaign.getMap(i).getUnlockedByMaps().end(), entry.getMapName());
				if(iter!=campaign.getMap(i).getUnlockedByMaps().end())
				{
					(*iter)=nameEditor->getText();
				}
			}
			entry.setMapName(nameEditor->getText());
			entry.setDescription(descriptionEditor->getText());
			entry.getUnlockedByMaps().clear();
			for(unsigned n=0; n<mapsUnlockedBy->getCount(); ++n)
			{
				entry.getUnlockedByMaps().push_back(mapsUnlockedBy->getText(n));
			}
			if(entry.getUnlockedByMaps().size()>0)
				entry.lockMap();
			else
				entry.unlockMap();
			endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(CANCEL);
		}
		else if (source == addToUnlocked)
		{
			if(mapsAvailable->getSelectionIndex()!=-1)
			{
				mapsUnlockedBy->addText(mapsAvailable->get());
				mapsAvailable->removeText(mapsAvailable->getSelectionIndex());
			}
		}
		else if (source == removeFromUnlocked)
		{
			if(mapsUnlockedBy->getSelectionIndex()!=-1)
			{
				mapsAvailable->addText(mapsUnlockedBy->get());
				mapsUnlockedBy->removeText(mapsUnlockedBy->getSelectionIndex());
			}
		}
	}
}



