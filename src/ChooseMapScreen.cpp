// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "ChooseMapScreen.h"
#include <vector>
#include <algorithm>
#include "Team.h"
#include "MapTiling.h"
#include "GUIGlob2FileList.h"
#include "GUIMapPreview.h"
#include "GlobalContainer.h"
#include <FormatableString.h>
#include <GUIButton.h>
#include <GUIMessageBox.h>
#include <GUIText.h>
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <memory>

#include "Game.h"

ChooseMapScreen::ChooseMapScreen(const char *directory, const char *extension, bool recurse, const char* alternateDirectory, const char* alternateExtension, const bool alternateRecurse)
{
	ok = new TextButton(440, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[ok]"), OK, 13);
	addWidget(ok);
	
	cancel = new TextButton(440, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[Cancel]"), CANCEL, 27);
	addWidget(cancel);

	fileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", directory, extension, recurse);
	addWidget(fileList);
	
	mapPreview = new MapPreview(640-20-26-128, 70, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED);
	addWidget(mapPreview);
	
	currentDirectoryMode=DisplayRegular;

	deleteMap = NULL;
	if (strcmp(directory, "maps") == 0)
	{
		type1 = MAP;
		type2 = GAME;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose map]"));
	}
	else if (strcmp(directory, "games") == 0)
	{
		type1 = GAME;
		type2 = REPLAY;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose game]"));
		//deleteMap = new TextButton(225, 380, 200, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", Toolkit::getStringTable()->getString("[Delete game]"), DELETEGAME);
		deleteMap = new TextButton(250, 360, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[delete]"), DELETEGAME);
		addWidget(deleteMap);
	}
	else
	{
		type1 = GAME;
		type2 = NONE;

		title = new Text(0, 18, ALIGN_FILL, ALIGN_SCREEN_CENTERED, "menu", Toolkit::getStringTable()->getString("[choose game]"));
	}
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
	repeatXText=new Text(440, 60+128+128, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "little", Toolkit::getStringTable()->getString("[tile x]"), 44);
	addWidget(repeatXText);
	repeatYText=new Text(484, 60+128+128, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "little", Toolkit::getStringTable()->getString("[tile y]"), 44);
	addWidget(repeatYText);
	teamCountText=new Text(528, 60+128+128, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "little", Toolkit::getStringTable()->getString("[colonies]"), 44);
	addWidget(teamCountText);
	coloniesPerTeamText=new Text(572, 60+128+128, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "little", Toolkit::getStringTable()->getString("[swarms]"), 44);
	addWidget(coloniesPerTeamText);
	repeatX=new Number(440, 60+128+142, 44, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 8, "standard", Toolkit::getStringTable()->getString("[repeat map horizontally]"), "standard");
	addWidget(repeatX);
	repeatY=new Number(484, 60+128+142, 44, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 8, "standard", Toolkit::getStringTable()->getString("[repeat map vertically]"), "standard");
	addWidget(repeatY);
	teamCount=new Number(528, 60+128+142, 44, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 8, "standard", Toolkit::getStringTable()->getString("[number of colonies]"), "standard");
	addWidget(teamCount);
	coloniesPerTeam=new Number(572, 60+128+142, 44, 20, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, 8, "standard", Toolkit::getStringTable()->getString("[swarms per colony]"), "standard");
	addWidget(coloniesPerTeam);
	repeatX->visible = repeatY->visible = teamCount->visible = coloniesPerTeam->visible = false;
	repeatXText->visible = repeatYText->visible = teamCountText->visible = coloniesPerTeamText->visible = false;

	if(alternateDirectory)
	{
		assert(type2 != NONE);

		switchType = new TextButton(250, 420, 180, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", loadableTypeName(type2).c_str(), SWITCHTYPE, 27);
		addWidget(switchType);

		alternateFileList = new Glob2FileList(20, 60, 180, 400, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "standard", alternateDirectory, alternateExtension, alternateRecurse);
		addWidget(alternateFileList);
		alternateFileList->visible=false;
	}
	
	validMapSelected = false;
	selectedType = NONE;
}

ChooseMapScreen::~ChooseMapScreen()
{
}

void ChooseMapScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action == LIST_ELEMENT_SELECTED)
	{
		Glob2FileList* active = activeFileList();
		if (active->selection())
		{
			std::string mapFileName = active->listToFile(active->getText(par1).c_str());

			try
			{
				mapPreview->setMapThumbnail(mapFileName.c_str());

				auto stream = std::unique_ptr<InputStream>(new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapFileName)));
				if (stream->isEndOfStream())
				{
					std::cerr << "ChooseMapScreen::onAction() : error, can't open file " << mapFileName  << std::endl;
				}
				else
				{
					if (verbose)
						std::cout << "ChooseMapScreen::onAction : loading map " << mapFileName << std::endl;
					validMapSelected = mapHeader.load(stream.get());
					if (!validMapSelected) selectedType = NONE;
					mapHeader.setMapName(glob2FilenameToName(mapFileName));
					if (validMapSelected)
					{
						selectedType = activeType();
						sourceMapHeader = mapHeader;
						if (selectedType == MAP)
						{
							if (!teamsPicked)
								tileTeams = 0;
							if (!swarmsPicked)
								tileColonies = 0;
							MapTiling::adjust(infoOfMap(mapFileName), tileX, tileY, tileTeams, tileColonies);
						}
						updateTilingControls();
						applyTiling();
						sortMapList();
						time_t mtime = Toolkit::getFileManager()->mtime(mapFileName);
						mapDate->setText(ctime(&mtime));
					}
					else
						std::cerr << "ChooseMapScreen::onAction : invalid map header for map " << mapFileName << std::endl;
				}
			}
			catch (std::exception &e)
			{
				// Show error message
				GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[ERROR_CANT_LOAD_MAP]"), Toolkit::getStringTable()->getString("[ok]"));

				validMapSelected = false;
			}
		}
		else 
		{
			mapDate->setText("");
			mapVersion->setText("");
			mapInfo->setText("");
			mapSize->setText("");
			mapName->setText("");
			mapPreview->setMapThumbnail("");
			validMapSelected = false;
			repeatX->visible = repeatY->visible = teamCount->visible = coloniesPerTeam->visible = false;
			repeatXText->visible = repeatYText->visible = teamCountText->visible = coloniesPerTeamText->visible = false;
		}
	}
	else if (action == NUMBER_ELEMENT_SELECTED && (source == repeatX || source == repeatY || source == teamCount || source == coloniesPerTeam))
	{
		if (source == repeatX)
			tileX = repeatX->get();
		else if (source == repeatY)
			tileY = repeatY->get();
		else if (source == teamCount)
		{
			tileTeams = teamCount->get();
			teamsPicked = true;
		}
		else
		{
			tileColonies = coloniesPerTeam->get();
			swarmsPicked = true;
		}
		if (source == teamCount)
		{
			// the colony count comes first: the repeat grows to make room for it
			const int wanted = tileTeams;
			MapTiling::adjust(infoOfMap(activeFileList()->listToFile(activeFileList()->get())), tileX, tileY, tileTeams, tileColonies);
			if (tileTeams != wanted)
				tileTeams = wanted;
		}
		updateTilingControls();
		applyTiling();
		sortMapList();
	}
	else if ((action == BUTTON_RELEASED) || (action == BUTTON_SHORTCUT))
	{
		if (source == ok)
		{
			// we accept only if a valid map is selected
			if (!validMapSelected)
				return;
			if (tilingActive())
			{
				// the repeated map becomes a map file of its own, so the game and its clients see an ordinary map
				const int teamsHeld = std::min(tileTeams, std::min<int>(Team::MAX_COUNT, MapTiling::colonyCount(sourceMapHeader.getNumberOfTeams(), tileX, tileY)));
				MapHeader written = MapTiling::writeTiledMap(sourceMapHeader, tileX, tileY, teamsHeld, tileColonies);
				if (written.getNumberOfTeams() < 1)
				{
					GAGGUI::MessageBox(globalContainer->gfx, "standard", GAGGUI::MB_ONEBUTTON, Toolkit::getStringTable()->getString("[ERROR_CANT_LOAD_MAP]"), Toolkit::getStringTable()->getString("[ok]"));
					return;
				}
				mapHeader = written;
			}
			endExecute(OK);
		}
		else if (source == cancel)
		{
			endExecute(par1);
		}
		else if (source == deleteMap)
		{
			// if a valid file is selected, delete it
			Glob2FileList* active = activeFileList();
			if (auto sel = active->selection())
			{
				size_t i = *sel;
				std::string mapFileName = active->listToFile(active->get().c_str());

				Toolkit::getFileManager()->remove(mapFileName);
				active->generateList();

				active->setSelection(List::selectionAfterRemoval(i, active->getCount()));
				active->selectionChanged();
			}
		}
		else if (source == switchType)
		{
			setDirectoryMode(currentDirectoryMode == DisplayRegular ? DisplayAlternate : DisplayRegular);
		}
	}
}


void ChooseMapScreen::updateMapInformation()
{
	// update map name & info
	mapName->setText(mapHeader.getMapName());
	std::string textTemp;
	textTemp = FormatableString("%0%1").arg(mapHeader.getNumberOfTeams()).arg(Toolkit::getStringTable()->getString("[teams]"));
	mapInfo->setText(textTemp);
	textTemp = FormatableString("%0 %1.%2").arg(Toolkit::getStringTable()->getString("[Version]")).arg(mapHeader.getVersionMajor()).arg(mapHeader.getVersionMinor());
	mapVersion->setText(textTemp);
	if (tileX > 1 || tileY > 1)
		textTemp = FormatableString("%0 x %1 (%2 x %3)").arg(mapPreview->getLastWidth() * tileX).arg(mapPreview->getLastHeight() * tileY).arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
	else
		textTemp = FormatableString("%0 x %1").arg(mapPreview->getLastWidth()).arg(mapPreview->getLastHeight());
	mapSize->setText(textTemp);
	
	// call subclass handler
	validMapSelectedhandler();
}


void ChooseMapScreen::updateTilingControls()
{
	const bool tileable = validMapSelected && selectedType == MAP && sourceMapHeader.getNumberOfTeams() > 0;
	repeatX->visible = repeatY->visible = teamCount->visible = coloniesPerTeam->visible = tileable;
	repeatXText->visible = repeatYText->visible = teamCountText->visible = coloniesPerTeamText->visible = tileable;
	mapDate->visible = !tileable;
	if (!tileable)
	{
		tileX = tileY = 1;
		tileTeams = 0;
		tileColonies = 0;
		return;
	}
	// a cleared Number keeps its old index, so it is reset before the value is picked
	repeatX->clear();
	for (int f : MapTiling::repeatOptions(mapPreview->getLastWidth()))
		repeatX->add(f);
	repeatX->setNth(0);
	repeatX->set(tileX);
	tileX = repeatX->get();
	repeatY->clear();
	for (int f : MapTiling::repeatOptions(mapPreview->getLastHeight()))
		repeatY->add(f);
	repeatY->setNth(0);
	repeatY->set(tileY);
	tileY = repeatY->get();
	// the colony count offers the whole range; a map that cannot hold it sorts as a violator
	const int colonies = MapTiling::colonyCount(sourceMapHeader.getNumberOfTeams(), tileX, tileY);
	const int maxTeams = std::min<int>(Team::MAX_COUNT, colonies);
	if (!teamsPicked || tileTeams < 1)
		tileTeams = maxTeams;
	teamCount->clear();
	for (int t = 1; t <= Team::MAX_COUNT; t++)
		teamCount->add(t);
	teamCount->setNth(0);
	teamCount->set(tileTeams);
	tileTeams = teamCount->get();
	// swarms per colony: as many as fit unless the user asked for fewer
	const int maxSwarms = std::max(1, colonies / std::min(tileTeams, maxTeams));
	if (!swarmsPicked || tileColonies < 1)
		tileColonies = maxSwarms;
	coloniesPerTeam->clear();
	for (int c = 1; c <= maxSwarms; c++)
		coloniesPerTeam->add(c);
	coloniesPerTeam->setNth(0);
	coloniesPerTeam->set(std::min(tileColonies, maxSwarms));
	tileColonies = coloniesPerTeam->get();
}

const MapTiling::MapInfo& ChooseMapScreen::infoOfMap(const std::string& fileName)
{
	auto it = mapInfos.find(fileName);
	if (it == mapInfos.end())
		it = mapInfos.insert(std::make_pair(fileName, MapTiling::readMapInfo(fileName))).first;
	return it->second;
}

void ChooseMapScreen::sortMapList()
{
	if (activeType() != MAP)
		return;
	Glob2FileList* list = activeFileList();
	const std::string selected = list->selection() ? list->get() : "";
	std::vector<std::string> dirs, fitting, others;
	for (size_t i = 0; i < list->getCount(); i++)
	{
		const std::string& entry = list->getText(i);
		if (!entry.empty() && entry[entry.size()-1] == '/')
			dirs.push_back(entry);
		else if (MapTiling::fits(infoOfMap(list->listToFile(entry)), tileX, tileY, tileTeams, tileColonies))
			fitting.push_back(entry);
		else
			others.push_back(entry);
	}
	list->clear();
	for (const std::string& e : dirs) list->addText(e);
	for (const std::string& e : fitting) list->addText(e);
	for (const std::string& e : others) list->addText(e);
	for (size_t i = 0; i < list->getCount(); i++)
		if (list->getText(i) == selected)
			list->setSelection(i);
}

bool ChooseMapScreen::tilingActive() const
{
	const int teamsHeld = std::min(tileTeams, std::min<int>(Team::MAX_COUNT, MapTiling::colonyCount(sourceMapHeader.getNumberOfTeams(), tileX, tileY)));
	return selectedType == MAP && (MapTiling::isActive(tileX, tileY, teamsHeld, sourceMapHeader.getNumberOfTeams())
		|| tileColonies * teamsHeld < MapTiling::colonyCount(sourceMapHeader.getNumberOfTeams(), tileX, tileY));
}

void ChooseMapScreen::applyTiling()
{
	if (!validMapSelected)
		return;
	if (selectedType == MAP)
	{
		const int teamsHeld = std::min(tileTeams, std::min<int>(Team::MAX_COUNT, MapTiling::colonyCount(sourceMapHeader.getNumberOfTeams(), tileX, tileY)));
		mapHeader = MapTiling::tiledHeader(sourceMapHeader, tileX, tileY, teamsHeld);
	}
	updateMapInformation();
}

MapHeader& ChooseMapScreen::getMapHeader()
{
	return mapHeader;
}


GameHeader& ChooseMapScreen::getGameHeader()
{
	return gameHeader;
}

ChooseMapScreen::LoadableType ChooseMapScreen::getSelectedType()
{
	return selectedType;
}

Glob2FileList* ChooseMapScreen::activeFileList() const
{
	return (currentDirectoryMode == DisplayRegular) ? fileList : alternateFileList;
}

ChooseMapScreen::LoadableType ChooseMapScreen::activeType() const
{
	return (currentDirectoryMode == DisplayRegular) ? type1 : type2;
}

std::string ChooseMapScreen::loadableTypeName(LoadableType type)
{
	switch (type)
	{
		case GAME:   return Toolkit::getStringTable()->getString("[the games]");
		case MAP:    return Toolkit::getStringTable()->getString("[the maps]");
		case REPLAY: return Toolkit::getStringTable()->getString("[the replays]");
		case NONE:   break;
	}
	assert(false);
	return {};
}

void ChooseMapScreen::setDirectoryMode(DirectoryMode newMode)
{
	currentDirectoryMode = newMode;
	const bool regular = (newMode == DisplayRegular);
	fileList->visible = regular;
	alternateFileList->visible = !regular;
	// After switching, the button label points back to the list we just left.
	switchType->setText(loadableTypeName(regular ? type2 : type1));
	activeFileList()->selectionChanged();
}
