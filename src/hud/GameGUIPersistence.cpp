// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <iostream>

#include <FileManager.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>

#include "Game.h"
#include "FileFormatVersions.h"
#include <stdexcept>
#include "GameGUI.h"
#include "GameGUIViewport.h"
#include "GameGUITouch.h"
#include "GameGUIDialog.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include <glob2/BuildConfig.h>


bool GameGUI::loadFromHeaders(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI, const std::string& sourceFileName)
{
    return loadFromHeadersTask(mapHeader, gameHeader, setGameHeader, ignoreGUIData, saveAI, sourceFileName).run();
}
bool GameGUI::load(GAGCore::InputStream *stream, bool ignoreGUIData)
{
    return loadTask(stream, ignoreGUIData).run();
}

GAGCore::CooperativeTask GameGUI::loadFromHeadersTask(MapHeader mapHeader, GameHeader gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI, std::string sourceFileName)
{
	// In the browser the game sprites may still be downloading.
	co_await globalContainer->gameGraphicsTask();
	init();
	auto stream = std::make_unique<BinaryInputStream>(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), sourceFileName.empty()?mapHeader.getFileName():sourceFileName));
	if (stream->isEndOfStream())
	{
		if(!sourceFileName.empty()) co_return false;
		stream = std::make_unique<BinaryInputStream>(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), mapHeader.getFileName(true)));
		if(stream->isEndOfStream())
		{
			stream = std::make_unique<BinaryInputStream>(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), mapHeader.getFileName(false,true)));
			if(stream->isEndOfStream())
			{
				std::cerr << "GameGUI::loadFromHeaders() : error, can't open file " << mapHeader.getFileName() << ", " << mapHeader.getFileName(true) << " or " << mapHeader.getFileName(false,true) << std::endl;
				co_return false;
			}
		}
	}

	co_return co_await loadFromStreamTask(mapHeader, gameHeader, setGameHeader, ignoreGUIData, saveAI, stream.get());
}

GAGCore::CooperativeTask GameGUI::loadFromStreamTask(MapHeader mapHeader, GameHeader gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI, GAGCore::InputStream *stream)
{
	bool res = co_await loadTask(stream, ignoreGUIData);
	if (!res)
		co_return false;

	// Intentionally keep the map header loaded from the file rather than the
	// one sent across the network: the network header is in the latest format
	// version, whereas the actual map may be an older file version.
	if(setGameHeader)
	{
		game.setGameHeader(gameHeader, saveAI);
		rebuildBuildingChoices(true);
		// A saved game already carries its units and buildings under the rules.
		if (!game.mapHeader.getIsSavedGame())
			game.applyStartingRules();
	}

	co_return true;
}

GAGCore::CooperativeTask GameGUI::loadTask(GAGCore::InputStream *stream, bool ignoreGUIData)
{
	co_await globalContainer->gameGraphicsTask();
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	init();

	bool result = co_await game.loadTask(stream);

	if (result == false)
	{
		std::cerr << "GameGUI::load : can't load game" << std::endl;
		co_return false;
	}
	rebuildBuildingChoices();
	defaultGameSaveName = game.mapHeader.getMapName();
	if (game.mapHeader.getIsSavedGame())
	{
		// load gui's specific infos
		stream->readEnterSection("GameGUI");

		///Load the data, but don't store it in local variables
		if(ignoreGUIData)
		{
			stream->readUint32("chatMask");
			stream->readSint32("localPlayer");
			stream->readSint32("localTeamNo");
			stream->readSint32("viewportX");
			stream->readSint32("viewportY");
			stream->readUint32("hiddenGUIElements");
			if (game.mapHeader.getVersionMinor()<FILE_FORMAT_VERSION_BUILDING_CATALOG)
			{
				stream->readUint32("buildingsChoiceMask");
				stream->readUint32("flagsChoiceMask");
			}
		}
		else
		{
			chatMask = stream->readUint32("chatMask");

			localPlayer = stream->readSint32("localPlayer");
			localTeamNo = stream->readSint32("localTeamNo");
			if (localPlayer < 0 || localPlayer >= game.gameHeader.getNumberOfPlayers() ||
				localTeamNo < 0 || localTeamNo >= game.mapHeader.getNumberOfTeams())
			{
				std::cerr << "Invalid saved GUI player/team: " << localPlayer << "/" << localTeamNo << std::endl;
				co_return false;
			}

			viewportX = stream->readSint32("viewportX");
			viewportY = stream->readSint32("viewportY");

			hiddenGUIElements = stream->readUint32("hiddenGUIElements");
			if (game.mapHeader.getVersionMinor()<FILE_FORMAT_VERSION_BUILDING_CATALOG)
			{
			Uint32 buildingsChoiceMask = stream->readUint32("buildingsChoiceMask");
			Uint32 flagsChoiceMask = stream->readUint32("flagsChoiceMask");

			// invert value if hidden
			for (unsigned i=0; i<buildingsChoiceState.size(); ++i)
			{
				int id = game.buildingsTypes.get(game.buildingsTypes.findByKey(buildingsChoiceName[i]))->shortTypeNum;
				buildingsChoiceState[i] = id>=0 && id<32 && ((Uint32(1)<<id) & buildingsChoiceMask) != 0;
			}
			for (unsigned i=0; i<flagsChoiceState.size(); ++i)
			{
				int id = game.buildingsTypes.get(game.buildingsTypes.findByKey(flagsChoiceName[i]))->shortTypeNum;
				flagsChoiceState[i] = id>=0 && id<32 && ((Uint32(1)<<id) & flagsChoiceMask) != 0;
			}
			}
		}

		if (game.mapHeader.getVersionMinor()>=FILE_FORMAT_VERSION_BUILDING_CATALOG)
		{
			const auto readChoices = [&](const char* section,const auto& names,auto& states) {
				stream->readEnterSection(section);
				const auto count=stream->readUint32("count");
				if (count>game.buildingsTypes.size()) throw std::runtime_error("Invalid building choice count");
				std::set<std::string> seen;
				for (Uint32 i=0; i<count; ++i)
				{
					stream->readEnterSection(i);
					const auto key=stream->readText("key");
					const auto enabled=stream->readUint32("enabled");
					if (enabled>1 || !seen.insert(key).second || game.buildingsTypes.findByKey(key)<0)
						throw std::runtime_error("Invalid saved building choice");
					if (!ignoreGUIData)
						for (size_t choice=0; choice<names.size(); ++choice) if(names[choice]==key) states[choice]=enabled;
					stream->readLeaveSection();
				}
				stream->readLeaveSection();
			};
			readChoices("buildingChoices",buildingsChoiceName,buildingsChoiceState);
			readChoices("flagChoices",flagsChoiceName,flagsChoiceState);
		}

		if(game.mapHeader.getVersionMinor() >= 69)
			defaultAssign.load(stream, game.mapHeader.getVersionMinor());
		stream->readLeaveSection();
	}

	game.mapscript.restorePresentation(*this);
	minimap.setGame(game);

	co_return true;
}

void GameGUI::save(GAGCore::OutputStream *stream, const std::string name, DeferredGameSHA1* deferredSHA1)
{
	// Game is can't be no more automatically generated
	game.save(stream, false, name, deferredSHA1);

	stream->writeEnterSection("GameGUI");
	stream->writeUint32(chatMask, "chatMask");
	stream->writeSint32(localPlayer, "localPlayer");
	stream->writeSint32(localTeamNo, "localTeamNo");
	stream->writeSint32(viewportX, "viewportX");
	stream->writeSint32(viewportY, "viewportY");
	stream->writeUint32(hiddenGUIElements, "hiddenGUIElements");
	const auto writeChoices = [&](const char* section,const auto& names,const auto& states) {
		stream->writeEnterSection(section);
		stream->writeUint32(names.size(),"count");
		for (size_t i=0; i<names.size(); ++i)
		{
			stream->writeEnterSection(i);
			stream->writeText(names[i],"key");
			stream->writeUint32(states[i],"enabled");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	};
	writeChoices("buildingChoices",buildingsChoiceName,buildingsChoiceState);
	writeChoices("flagChoices",flagsChoiceName,flagsChoiceState);
	defaultAssign.save(stream);
	stream->writeLeaveSection();
}

void GameGUI::viewportResized(int oldWidth, int oldHeight, int width, int height)
{
    if (!game.map.getW() || !game.map.getH()) return;
    minimap.resizeViewport(width);
    suspendInput();
    const int oldX = viewportX, oldY = viewportY;
    // Before the first draw there are no camera bounds to resize. Preserve the
    // legacy tile-centered viewport in that case; an initialized camera keeps
    // its precise world center (including zoom and fractional panning).
    if (camera.width <= 0 || camera.height <= 0) {
        viewportX = (viewportX + (oldWidth - GAME_GUI_RIGHT_MENU_WIDTH) / 64 -
                     (width - GAME_GUI_RIGHT_MENU_WIDTH) / 64) & game.map.wMask;
        viewportY = (viewportY + oldHeight / 64 - height / 64) & game.map.hMask;
    }
    updateCamera();
    viewportChanged(oldX, viewportX, oldY, viewportY);
    if (auto *dialog = activeDialog()) dialog->cancelInput();
}

void GameGUI::stopViewportMotion()
{
    if (touch) touch->stopMapMotion();
}

void GameGUI::suspendInput()
{
    if (touch) touch->cancel(true);
    panPushed=false;
    mapPanPushed=false;
    torusView.stopMoving();
    torusPointerDown=false;
    torusView.setPointerHeld(false);
    inputState.clearHeld();
    viewportSpeedX = viewportSpeedY = 0;
    lastMouseButtonState = 0;
    miniMapPushed = selectionPushed = false;
    toolManager.cancelDrag(localTeamNo);
}
