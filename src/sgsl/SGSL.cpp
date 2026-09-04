// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file SGSL.cpp
	\brief SGSL: Simple Globulation Scripting Language: script lifecycle, state save/load and stepping
*/

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <Stream.h>

#include "Building.h"
#include "GameGUI.h"
#include "SGSL.h"

std::optional<int> mapAreaNumber(const Game *game, const std::string &name)
{
	for(int n=0; n<9; ++n)
	{
		if(game->map.getAreaName(n)==name)
			return n;
	}
	return std::nullopt;
}

MapScriptSGSL::~MapScriptSGSL(void)
{
	
}

bool MapScriptSGSL::load(GAGCore::InputStream *stream, Game *game)
{
	stream->readEnterSection("SGSL");
	
	// load source code
	sourceCode = stream->readText("sourceCode");
	
	// compile source code
	ErrorReport er = compileScript(game);
	if (er.type != ErrorReport::ET_OK)
	{
		std::cout << "SGSL : " << er.getErrorString()
				<< " at line " << er.line+1
				<< " on col " << er.col
				<< std::endl;
		stream->readLeaveSection();
		return false;
	}
	
	// load state
	// load main timer
	mainTimer = stream->readSint32("mainTimer");
	
	// load hasWon / hasLost vectors
	stream->readEnterSection("victoryConditions");
	for (unsigned i = 0; i < (unsigned)game->mapHeader.getNumberOfTeams(); i++)
	{
		stream->readEnterSection(i);
		hasWon[i] = stream->readSint32("hasWon") != 0;
		hasLost[i] = stream->readSint32("hasLost") != 0;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load stories datas
	stream->readEnterSection("stories");
	for (unsigned i = 0; i < stories.size(); i++)
	{
		stream->readEnterSection(i);
		stories[i].lineSelector = stream->readSint32("ProgramCounter");
		stories[i].internTimer = stream->readSint32("internTimer");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load areas
	stream->readEnterSection("areas");
	unsigned areasCount = stream->readUint32("areasCount");
	for (unsigned i = 0; i < areasCount; i++)
	{
		stream->readEnterSection(i);
		std::string name = stream->readText("name");
		areas[name].x = stream->readSint32("x");
		areas[name].y = stream->readSint32("y");
		areas[name].r = stream->readSint32("r");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load flags
	stream->readEnterSection("flags");
	unsigned flagsCount = stream->readUint32("flagsCount");
	for (unsigned i = 0; i < flagsCount; i++)
	{
		stream->readEnterSection(i);
		std::string name = stream->readText("name");
		Uint16 gbid = stream->readUint16("gbid");
		Building *b = game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
		assert(b);
		flags[name] = b;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}

void MapScriptSGSL::save(GAGCore::OutputStream *stream, const Game *game)
{
	stream->writeEnterSection("SGSL");
	
	stream->writeText(sourceCode, "sourceCode");
	
	// save state
	
	// save main timer
	stream->writeSint32(mainTimer, "mainTimer");
	
	// save hasWon / hasLost vectors
	stream->writeEnterSection("victoryConditions");
	for (unsigned i = 0; i < (unsigned)game->mapHeader.getNumberOfTeams(); i++)
	{
		stream->writeEnterSection(i);
		stream->writeSint32(hasWon[i] ? 1 : 0, "hasWon");
		stream->writeSint32(hasLost[i] ? 1 : 0, "hasLost");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	
	// save stories datas
	stream->writeEnterSection("stories");
	for (unsigned i = 0; i < stories.size(); i++)
	{
		stream->writeEnterSection(i);
		stream->writeSint32(stories[i].lineSelector, "ProgramCounter");
		stream->writeSint32(stories[i].internTimer, "internTimer");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	
	// save areas
	stream->writeEnterSection("areas");
	stream->writeUint32(areas.size(), "areasCount");
	unsigned i = 0;
	for (AreaMap::iterator it = areas.begin(); it != areas.end(); ++it)
	{
		stream->writeEnterSection(i);
		stream->writeText(it->first, "name");
		stream->writeSint32(it->second.x, "x");
		stream->writeSint32(it->second.y, "y");
		stream->writeSint32(it->second.r, "r");
		stream->writeLeaveSection();
		i++;
	}
	stream->writeLeaveSection();
	
	// save flags
	stream->writeEnterSection("flags");
	stream->writeUint32(flags.size(), "flagsCount");
	i = 0;
	for (BuildingMap::iterator it = flags.begin(); it != flags.end(); ++it)
	{
		stream->writeEnterSection(i);
		stream->writeText(it->first, "name");
		stream->writeUint16(it->second->gid, "x");
		stream->writeLeaveSection();
		i++;
	}
	stream->writeLeaveSection();
	
	stream->writeLeaveSection();
}

void MapScriptSGSL::reset(void)
{
	isTextShown = false;
	mainTimer=0;
	stories.clear();
	areas.clear();
	flags.clear();
}

bool MapScriptSGSL::testMainTimer() const
{
	return (mainTimer <= 0);
}

void MapScriptSGSL::syncStep(GameGUI *gui)
{
	if (mainTimer)
		mainTimer--;
	for (std::vector<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		if (gui->isSpaceSet())
			it->sendSpace();
		it->syncStep(gui);
	}
	if(gui->isSpaceSet())
	{
		gui->setIsSpaceSet(false);
		gui->setSwallowSpaceKey(false);
	}
}

Sint32 MapScriptSGSL::checkSum()
{
	Sint32 cs=0;
	for (std::vector<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		cs^=it->checkSum();
		cs=(cs<<28)|(cs>>4);
	}
	return cs;
}


ErrorReport MapScriptSGSL::compileScript(Game *game, const char *script)
{
	StringAquisition aquisition(functions);
	aquisition.open(script);
	return parseScript(&aquisition, game);
}

ErrorReport MapScriptSGSL::compileScript(Game *game)
{
	return compileScript(game, sourceCode.c_str());
}

ErrorReport MapScriptSGSL::loadScript(const std::string filename, Game *game)
{
	FileAquisition aquisition(functions);
	if (aquisition.open(filename))
		return parseScript(&aquisition, game);
	else
		return ErrorReport(ErrorReport::ET_NO_SUCH_FILE);
}

bool MapScriptSGSL::hasTeamWon(unsigned teamNumber) const
{
	// Seb: Cheapo hack. Script should intialize hasWon first :-)
	if (testMainTimer() && hasWon.size()>teamNumber)
	{
		return hasWon.at(teamNumber);
	}
	return false;
}

bool MapScriptSGSL::hasTeamLost(unsigned teamNumber) const
{
	// Seb: Cheapo hack. Script should intialize hasLost first :-)
	if(hasLost.size()>teamNumber)
		return hasLost.at(teamNumber);
	return false;
}



void MapScriptSGSL::addTeam()
{
	hasWon.push_back(false);
	hasLost.push_back(false);
}



void MapScriptSGSL::removeTeam(int n)
{
	hasWon.erase(hasWon.begin()+n);
	hasLost.erase(hasLost.begin()+n);
}
