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
#include <utility>
#include <vector>

#include <Stream.h>

#include "Building.h"
#include "Game.h"
#include "sim/ClientCommandSink.h"
#include "sim/ClientRequests.h"
#include "SGSL.h"
#include <limits>

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

void MapScriptSGSL::swap(MapScriptSGSL& other) noexcept
{
	std::swap(isTextShown, other.isTextShown);
	textShown.swap(other.textShown);
	sourceCode.swap(other.sourceCode);
	functions.swap(other.functions);
	std::swap(mainTimer, other.mainTimer);
	hasWon.swap(other.hasWon);
	hasLost.swap(other.hasLost);
	stories.swap(other.stories);
	areas.swap(other.areas);
	flags.swap(other.flags);
	// Stories use their owner for timers, areas, flags and scenario actions.
	// Rebind both vectors: the replaced runtime also lives until its owner dies.
	for (auto& story : stories)
		story.mapscript = this;
	for (auto& story : other.stories)
		story.mapscript = &other;
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
	
	// load stories data
	stream->readEnterSection("stories");
	for (unsigned i = 0; i < stories.size(); i++)
	{
		stream->readEnterSection(i);
		// Saved PCs are untrusted. Only parser-reconstructed statement starts
		// and the explicit wait(N) suspension operand may resume execution.
		stories[i].lineSelector = stream->readSint32("ProgramCounter");
		if (!stories[i].instructionStarts.count(stories[i].lineSelector)) return false;
		stories[i].internTimer = stream->readSint32("internTimer");
		if (stories[i].internTimer < 0 || (stories[i].line[stories[i].lineSelector].type == SGSLToken::INT && stories[i].internTimer == 0)) throw std::runtime_error("Invalid SGSL internal timer: " + std::to_string(stories[i].internTimer));
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load areas
	stream->readEnterSection("areas");
	unsigned areasCount = stream->readUint32("areasCount");
	if (areasCount > 65536) return false;
	for (unsigned i = 0; i < areasCount; i++)
	{
		stream->readEnterSection(i);
		std::string name = stream->readText("name");
		areas[name].x = stream->readSint32("x");
		areas[name].y = stream->readSint32("y");
		areas[name].r = stream->readSint32("r");
		if (areas[name].r <= 0 || areas[name].r > 32767 || areas[name].x < -32767 || areas[name].x > 32767 || areas[name].y < -32767 || areas[name].y > 32767) return false;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	
	// load flags
	stream->readEnterSection("flags");
	unsigned flagsCount = stream->readUint32("flagsCount");
	if (flagsCount > Building::MAX_COUNT * Team::MAX_COUNT) return false;
	for (unsigned i = 0; i < flagsCount; i++)
	{
		stream->readEnterSection(i);
		std::string name = stream->readText("name");
		Uint16 gbid = stream->readUint16("gbid");
		if (gbid >= Building::MAX_COUNT * game->mapHeader.getNumberOfTeams() ||
			!game->teams[Building::GIDtoTeam(gbid)]) return false;
		Building *b = game->teams[Building::GIDtoTeam(gbid)]->myBuildings[Building::GIDtoID(gbid)];
		if (!b) return false;
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
	
	// save stories data
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

void MapScriptSGSL::syncStep(Game &game, ClientCommandSink &client, ClientRequests &requests)
{
	StoryContext context{&game, &client};
	// Released saves can contain negative timers. Preserve their state while
	// avoiding signed underflow at the representable boundary.
	if (mainTimer && mainTimer != std::numeric_limits<int>::min())
		mainTimer--;
	// Stories never post a Space acknowledgement, so one read serves them all.
	const bool space = requests.scriptSpacePending();
	for (std::vector<Story>::iterator it=stories.begin(); it!=stories.end(); ++it)
	{
		if (space)
			it->sendSpace();
		it->syncStep(&context);
	}
	if(space)
	{
		requests.takeScriptSpace();
		client.setSwallowSpaceKey(false);
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
	StringAcquisition acquisition(functions);
	acquisition.open(script);
	return parseScript(&acquisition, game);
}

ErrorReport MapScriptSGSL::compileScript(Game *game)
{
	return compileScript(game, sourceCode.c_str());
}

ErrorReport MapScriptSGSL::loadScript(const std::string filename, Game *game)
{
	FileAcquisition acquisition(functions);
	if (acquisition.open(filename))
		return parseScript(&acquisition, game);
	else
		return ErrorReport(ErrorReport::ET_NO_SUCH_FILE);
}

bool MapScriptSGSL::hasTeamWon(unsigned teamNumber) const
{
	// Seb: Cheapo hack. Script should initialize hasWon first :-)
	if (testMainTimer() && hasWon.size()>teamNumber)
	{
		return hasWon.at(teamNumber);
	}
	return false;
}

bool MapScriptSGSL::hasTeamLost(unsigned teamNumber) const
{
	// Seb: Cheapo hack. Script should initialize hasLost first :-)
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
