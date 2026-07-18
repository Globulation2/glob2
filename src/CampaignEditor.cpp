// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "CampaignEditor.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "ChooseMapScreen.h"
#include "GlobalContainer.h"
#include "GUIMessageBox.h"
#include <set>
#include <algorithm>
#include "GUICheckList.h"


CampaignEditor::CampaignEditor(const std::string& name)
{
	if (name != "" && !campaign.load(name))
		campaign.setName(name);
	StringTable& table=*Toolkit::getStringTable();
	title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", table.getString("[campaign editor]"));
	mapList = new List(10, 50, 300, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard");
	addMap = new TextButton(10, 360, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[add map]"), ADDMAP);
	editMap = new TextButton(165, 360, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[edit map]"), EDITMAP);
	removeMap = new TextButton(10, 410, 145, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[remove map]"), REMOVEMAP);
	nameEditor = new TextInput(320, 60, 310, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", campaign.getName());
	ok = new TextButton(260, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[ok]"), OK);
	cancel = new TextButton(450, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[Cancel]"), CANCEL);
	description = new TextArea(320, 90, 310, 225, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", false, campaign.getDescription().c_str());
	addWidget(title);
	addWidget(mapList);
	addWidget(addMap);
	addWidget(editMap);
	addWidget(removeMap);
	addWidget(nameEditor);
	addWidget(ok);
	addWidget(cancel);
	addWidget(description);
	syncMapList();
}




void CampaignEditor::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			if (campaign.save())
				endExecute(OK);
			else
				GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON,
					Toolkit::getStringTable()->getString("[ERROR_CANT_SAVE_CAMPAIGN]"),
					Toolkit::getStringTable()->getString("[ok]"));
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
			auto sel = mapList->selection();
			if (sel)
			{
				for(unsigned i=0; i<campaign.getMapCount(); ++i)
				{
					if(campaign.getMap(i).getMapName()==mapList->get())
					{
						CampaignMapEntryEditor cmee(campaign, campaign.getMap(i));
						int rcmee = cmee.execute(gfx, 40);
						if(rcmee==CampaignMapEntryEditor::OK)
						{
							mapList->setText(*sel, campaign.getMap(i).getMapName());
						}
						else if(rcmee==CampaignMapEntryEditor::CANCEL)
						{
						}
					}
				}
			}
		}
		else if (source == removeMap)
		{
			auto sel = mapList->selection();
			if (sel)
			{
				for(unsigned i=0; i<campaign.getMapCount(); ++i)
				{
					std::vector<std::string>::iterator iter=std::find(campaign.getMap(i).getUnlockedByMaps().begin(), campaign.getMap(i).getUnlockedByMaps().end(), mapList->get());
					if(iter!=campaign.getMap(i).getUnlockedByMaps().end())
					{
						campaign.getMap(i).getUnlockedByMaps().erase(iter);
					}
				}
				campaign.removeMap(*sel);
				mapList->removeText(*sel);
			}
		}
	}
	else if (action == TEXT_MODIFIED)
	{
		if(source==nameEditor)
		{
			campaign.setName(nameEditor->getText());
		}
		if(source==description)
		{
			campaign.setDescription(description->getText());
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
	mapsUnlockedBy = new CheckList(10, 80, 150, 300, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", false);
	mapsUnlockedByLabel = new Text(10, 50, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[unlocked by]"));
	nameEditorLabel = new Text(405, 80, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[map name]"));
	nameEditor=new TextInput(420, 105, 180, 25, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", entry.getMapName());
	isUnlockedLabel = new Text(430, 140, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[unlocked at start]"));
	isUnlocked = new OnOffButton(405, 140, 20, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, entry.isUnlocked(), ISUNLOCKED);
	descriptionEditorLabel = new Text(405, 170, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", table.getString("[map description]"));
	descriptionEditor = new TextArea(420, 195, 180, 225, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", false, entry.getDescription().c_str());
	ok = new TextButton(260, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[ok]"), OK);
	cancel = new TextButton(450, 430, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", table.getString("[Cancel]"), CANCEL);

	std::set<std::string> unlockedBy;
	for(unsigned n=0; n<entry.getUnlockedByMaps().size(); ++n)
	{
		unlockedBy.insert(entry.getUnlockedByMaps()[n]);
	}
	for(unsigned n=0; n<campaign.getMapCount(); ++n)
	{
		if(campaign.getMap(n).getMapName() != entry.getMapName())
		{
			if(unlockedBy.find(campaign.getMap(n).getMapName())==unlockedBy.end())
			{
				mapsUnlockedBy->addItem(campaign.getMap(n).getMapName(), false);
			}
			else
			{
				mapsUnlockedBy->addItem(campaign.getMap(n).getMapName(), true);
			}
		}
	}
	addWidget(title);
	addWidget(mapsUnlockedBy);
	addWidget(mapsUnlockedByLabel);
	addWidget(nameEditorLabel);
	addWidget(nameEditor);
	addWidget(isUnlockedLabel);
	addWidget(isUnlocked);
	addWidget(descriptionEditorLabel);
	addWidget(descriptionEditor);
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
			for(unsigned i=0; i<mapsUnlockedBy->getCount(); ++i)
			{
				if(mapsUnlockedBy->isChecked(i))
				{
					entry.getUnlockedByMaps().push_back(mapsUnlockedBy->getText(i));
				}
			}
			
			if(!isUnlocked->getState())
				entry.lockMap();
			else
				entry.unlockMap();
			endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(CANCEL);
		}
	}
	else if(action == TEXT_ACTIVATED)
	{
		if(source == nameEditor)
			descriptionEditor->deactivate();
		else if(source == descriptionEditor)
			nameEditor->deactivate();
	}
}



