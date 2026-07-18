// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <sstream>
#include <iostream>
#include <algorithm>
#include <optional>

#include <FileManager.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GUIStyle.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <TextStream.h>
#include <FormatableString.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "IRC.h"
#include "SoundMixer.h"
#include "VoiceRecorder.h"
#include "GameGUIKeyActions.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "config.h"
#include "Order.h"

#include <SDL_keycode.h>

bool GameGUI::loadFromHeaders(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader, bool ignoreGUIData, bool saveAI)
{
	init();
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapHeader.getFileName()));
	if (stream->isEndOfStream())
	{
		delete stream;
		stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapHeader.getFileName(true)));
		if(stream->isEndOfStream())
		{
			delete stream;
			stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapHeader.getFileName(false,true)));
			if(stream->isEndOfStream())
			{
				std::cerr << "GameGUI::loadFromHeaders() : error, can't open file " << mapHeader.getFileName() << ", " << mapHeader.getFileName(true) << " or " << mapHeader.getFileName(false,true) << std::endl;
				delete stream;
				return false;
			}
		}
	}

	bool res = load(stream, ignoreGUIData);
	delete stream;
	if (!res)
		return false;

	// Intentionally keep the map header loaded from the file rather than the
	// one sent across the network: the network header is in the latest format
	// version, whereas the actual map may be an older file version.
	if(setGameHeader)
		game.setGameHeader(gameHeader, saveAI);

	return true;
}

bool GameGUI::load(GAGCore::InputStream *stream, bool ignoreGUIData)
{
	init();

	bool result = game.load(stream);

	if (result == false)
	{
		std::cerr << "GameGUI::load : can't load game" << std::endl;
		return false;
	}
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
			stream->readUint32("buildingsChoiceMask");
			stream->readUint32("flagsChoiceMask");
		}
		else
		{
			chatMask = stream->readUint32("chatMask");

			localPlayer = stream->readSint32("localPlayer");
			localTeamNo = stream->readSint32("localTeamNo");

			viewportX = stream->readSint32("viewportX");
			viewportY = stream->readSint32("viewportY");

			hiddenGUIElements = stream->readUint32("hiddenGUIElements");
			Uint32 buildingsChoiceMask = stream->readUint32("buildingsChoiceMask");
			Uint32 flagsChoiceMask = stream->readUint32("flagsChoiceMask");

			// invert value if hidden
			for (unsigned i=0; i<buildingsChoiceState.size(); ++i)
			{
				int id = IntBuildingType::shortNumberFromType(buildingsChoiceName[i]);
				buildingsChoiceState[i] = ((1<<id) & buildingsChoiceMask) != 0;
			}
			for (unsigned i=0; i<flagsChoiceState.size(); ++i)
			{
				int id = IntBuildingType::shortNumberFromType(flagsChoiceName[i]);
				flagsChoiceState[i] = ((1<<id) & flagsChoiceMask) != 0;
			}
		}

		if(game.mapHeader.getVersionMinor() >= 69)
			defaultAssign.load(stream, game.mapHeader.getVersionMinor());
		stream->readLeaveSection();
	}

	minimap.setGame(game);

	return true;
}

void GameGUI::save(GAGCore::OutputStream *stream, const std::string name)
{
	// Game is can't be no more automatically generated
	game.save(stream, false, name);

	stream->writeEnterSection("GameGUI");
	stream->writeUint32(chatMask, "chatMask");
	stream->writeSint32(localPlayer, "localPlayer");
	stream->writeSint32(localTeamNo, "localTeamNo");
	stream->writeSint32(viewportX, "viewportX");
	stream->writeSint32(viewportY, "viewportY");
	stream->writeUint32(hiddenGUIElements, "hiddenGUIElements");
	Uint32 buildingsChoiceMask = 0;
	Uint32 flagsChoiceMask = 0;
	// save one if visible
	for (unsigned i=0; i<buildingsChoiceState.size(); ++i)
	{
		if (buildingsChoiceState[i])
		{
			int id = IntBuildingType::shortNumberFromType(buildingsChoiceName[i]);
			buildingsChoiceMask |= (1<<id);
		}
	}
	for (unsigned i=0; i<flagsChoiceState.size(); ++i)
	{
		if (flagsChoiceState[i])
		{
			int id = IntBuildingType::shortNumberFromType(flagsChoiceName[i]);
			flagsChoiceMask |= (1<<id);
		}
	}
	stream->writeUint32(buildingsChoiceMask, "buildingsChoiceMask");
	stream->writeUint32(flagsChoiceMask, "flagsChoiceMask");
	defaultAssign.save(stream);
	stream->writeLeaveSection();
}
