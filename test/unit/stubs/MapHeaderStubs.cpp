// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stand-ins for MapHeader.cpp and SGSL.cpp, which the unit binary does not link:
// linking them would drag in Game, Map, FileManager and globalContainer.
// WinningConditions.cpp reaches outside its translation unit only through
// MapHeader::getNumberOfTeams() and MapScriptSGSL::hasTeamWon/hasTeamLost;
// Campaign::save() calls glob2NameToFilename(), which no unit test invokes.

#include "MapHeaderStubs.h"
#include "MapHeader.h"
#include "SGSL.h"

#include <string>

Sint32 MapHeader::getNumberOfTeams() const
{
	return numberOfTeams;
}

void MapHeader::setNumberOfTeams(Sint32 teamNum)
{
	numberOfTeams = teamNum;
}

namespace glob2test::sgsl
{
	bool teamWon[Team::MAX_COUNT] = {};
	bool teamLost[Team::MAX_COUNT] = {};
}

bool MapScriptSGSL::hasTeamWon(unsigned teamNumber) const
{
	return teamNumber < Team::MAX_COUNT && glob2test::sgsl::teamWon[teamNumber];
}

bool MapScriptSGSL::hasTeamLost(unsigned teamNumber) const
{
	return teamNumber < Team::MAX_COUNT && glob2test::sgsl::teamLost[teamNumber];
}

std::string glob2NameToFilename(const std::string& /*dir*/, const std::string& /*name*/, const std::string& /*extension*/)
{
	return std::string();
}
