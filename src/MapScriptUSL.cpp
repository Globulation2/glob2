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


#include <Toolkit.h>
#include <FileManager.h>
using namespace GAGCore;

#include "MapScriptUSL.h"
#include "GameGUI.h"

#include "error.h"
#include "native.h"

#include "Stream.h"
#include <iostream>
#include <sstream>
#include <memory>
#include <boost/functional.hpp>

using namespace std;


template<>
inline void NativeValuePrototype<GameGUI*>::initialize()
{
	// For network safeness, this interface is not allowed to read user-defined variables
	addMethod<void(GameGUI*,string)>("enableBuildingsChoice", &GameGUI::enableBuildingsChoice);
	addMethod<void(GameGUI*,string)>("disableBuildingsChoice", &GameGUI::disableBuildingsChoice);
	addMethod<bool(GameGUI*,string)>("isBuildingEnabled", &GameGUI::isBuildingEnabled);
	addMethod<void(GameGUI*,string)>("enableFlagsChoice", &GameGUI::enableFlagsChoice);
	addMethod<void(GameGUI*,string)>("disableFlagsChoice", &GameGUI::disableFlagsChoice);
	addMethod<bool(GameGUI*,string)>("isFlagEnabled", &GameGUI::isFlagEnabled);
	addMethod<void(GameGUI*,int)>("enableGUIElement", &GameGUI::enableGUIElement);
	addMethod<void(GameGUI*,int)>("disableGUIElement", &GameGUI::disableGUIElement);
	
	//addMethod<bool(GameGUI*)>("isSpaceSet", &GameGUI::isSpaceSet);
	//addMethod<void(GameGUI*,bool)>("setIsSpaceSet", &GameGUI::setIsSpaceSet);
	//addMethod<bool(GameGUI*)>("isSwallowSpaceKey", &GameGUI::isSwallowSpaceKey);
	//addMethod<void(GameGUI*,bool)>("setSwallowSpaceKey", &GameGUI::setSwallowSpaceKey);
	
	addMethod<void(GameGUI*,string)>("showScriptText", &GameGUI::showScriptText);
	addMethod<void(GameGUI*,string,string)>("showScriptTextTr", &GameGUI::showScriptTextTr);
	addMethod<void(GameGUI*)>("hideScriptText", &GameGUI::hideScriptText);
}

template<>
inline void NativeValuePrototype<Game*>::initialize()
{
	addMethod<int(Game*)>("teamsCount", &Game::teamsCount);
	addMethod<bool(Game*,int)>("isTeamAlive", &Game::isTeamAlive);
	
	addMethod<int(Game*,int,int)>("unitsCount", &Game::unitsCount);
	addMethod<int(Game*,int,int,int,int)>("unitsUpgradesCount", &Game::unitsUpgradesCount);
	addMethod<int(Game*,int,int,int)>("buildingsCount", &Game::buildingsCount);
	
	// TODO: if required, add more from teamStats, maybe amount of unit starving can be usefull
}

template<>
inline void NativeValuePrototype<GameHints*>::initialize()
{
	addMethod<int(GameHints*)>("count", &GameHints::getNumberOfHints);
	
	addMethod<void(GameHints*,int)>("show", &GameHints::setHintVisible);
	addMethod<void(GameHints*,int)>("hide", &GameHints::setHintHidden);
	
	addMethod<bool(GameHints*,int)>("isVisible", &GameHints::isHintVisible);
}

template<>
inline void NativeValuePrototype<GameObjectives*>::initialize()
{
	addMethod<int(GameObjectives*)>("count", &GameObjectives::getNumberOfObjectives);
	
	addMethod<void(GameObjectives*,int)>("setComplete", &GameObjectives::setObjectiveComplete);
    addMethod<void(GameObjectives*,int)>("setIncomplete", &GameObjectives::setObjectiveIncomplete);
    addMethod<void(GameObjectives*,int)>("setFailed", &GameObjectives::setObjectiveFailed);
    addMethod<void(GameObjectives*,int)>("setHidden", &GameObjectives::setObjectiveHidden);
    addMethod<void(GameObjectives*,int)>("setVisible", &GameObjectives::setObjectiveVisible);

    addMethod<bool(GameObjectives*,int)>("isVisible", &GameObjectives::isObjectiveVisible);
	addMethod<bool(GameObjectives*,int)>("isComplete", &GameObjectives::isObjectiveComplete);
	addMethod<bool(GameObjectives*,int)>("isFailed", &GameObjectives::isObjectiveFailed);
	
	addMethod<string(GameObjectives*,int)>("getText", &GameObjectives::getGameObjectiveText);
	addMethod<int(GameObjectives*,int)>("getType", &GameObjectives::getObjectiveType);
}


void MapScriptUSL::addGlob2Values(GameGUI* gui)
{
	usl.setConstant("gui", new NativeValue<GameGUI*>(&usl.heap, gui));
	usl.setConstant("engine", new NativeValue<Game*>(&usl.heap, &(gui->game)));
	usl.setConstant("hints", new NativeValue<GameHints*>(&usl.heap, &(gui->game.gameHints)));
	usl.setConstant("objectives", new NativeValue<GameObjectives*>(&usl.heap, &(gui->game.objectives)));
}


MapScriptUSL::MapScriptUSL(GameGUI* gui)
{
	addGlob2Values(gui);
}


MapScriptUSL::~MapScriptUSL()
{
	
}


void MapScriptUSL::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("MapScriptUSL");
	// TODO: serialize state
	stream->writeLeaveSection();
}


void MapScriptUSL::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("MapScriptUSL");
	// TODO: deserialize state if version do match
	stream->readLeaveSection();
}


bool MapScriptUSL::compileCode(const std::string& code)
{
	GameGUI* gui = dynamic_cast<NativeValue<GameGUI*>*>(usl.getConstant("gui"))->value;
	usl = Usl();
	addGlob2Values(gui);
	
	const char* dirsToLoad[] = { "data/usl/Language/Runtime" , "data/usl/Glob2/Runtime", 0 };
	const char** dir = dirsToLoad;
	
	while (*(dir) != 0)
	{
		try
		{
			if (Toolkit::getFileManager()->initDirectoryListing(*dir, "usl"))
			{
				const char* fileName;
				while ((fileName = Toolkit::getFileManager()->getNextDirectoryEntry()) != NULL)
				{
					std::string fullFileName = string(*dir) + DIR_SEPARATOR + fileName;
					auto_ptr<ifstream> file(Toolkit::getFileManager()->openIFStream(fullFileName));
					if (file.get())
					{
						#ifdef DEBUG_USL
							cout << "* Loading " << fullFileName << endl;
						#endif
						usl.includeScript(fileName, *file.get());
					}
					else
					{
						cerr << "* Failed to load " << fullFileName << endl;
					}
				}
			}
			else
			{
				cerr << "MapScriptUSL::compileCode(): Cannot open script directory " << *dir << endl;
				return false;
			}
		}
		catch(Exception& e)
		{
			cerr << "MapScriptUSL::compileCode(): Error in usl runtime file " << e.position << " : " << e.what() << endl;
			return false;
		}
		++dir;
	}
	
	try
	{
		istringstream codeStream(code);
		usl.createThread("<mapscript>", codeStream);
	}
	catch(Exception& e)
	{
		error = MapScriptError(e.position.line, e.position.column, e.what());
		return false;
	}
	
	return true;
}

const MapScriptError& MapScriptUSL::getError() const
{
	return error;
}


void MapScriptUSL::syncStep(GameGUI *gui)
{
	const size_t stepsMax = 10000;
	size_t stepsCount = usl.run(stepsMax);
	
	#ifdef DEBUG_USL
		std::cout << "* USL executed " << stepsCount << " steps" << std::endl;
	#endif
}

