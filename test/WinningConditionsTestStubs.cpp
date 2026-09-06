// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Stubs for the WinningConditionsHarness. WinningConditions.cpp's predicates
// reach outside the WC translation unit only via:
//   - MapHeader::getNumberOfTeams() const         (read by every WC loop)
//   - MapScriptSGSL::hasTeamWon(unsigned)         (Script only)
//   - MapScriptSGSL::hasTeamLost(unsigned)        (Script only)
//
// The harness itself also calls:
//   - MapHeader::setNumberOfTeams(Sint32)         (to size the team table)
//
// Linking the real MapHeader.cpp / SGSL.cpp would drag in Game / Map /
// FileManager / globalContainer / etc.; instead these inline-style stubs
// satisfy the linker without any of that. WC predicates are otherwise pure
// field readers, so direct member access on the harness's raw storage is
// enough.

#include "MapHeader.h"
#include "SGSL.h"
#include "Team.h"

Sint32 MapHeader::getNumberOfTeams() const
{
	return numberOfTeams;
}

void MapHeader::setNumberOfTeams(Sint32 teamNum)
{
	numberOfTeams = teamNum;
}

namespace harness {
	bool sgslTeamWon[Team::MAX_COUNT]  = {};
	bool sgslTeamLost[Team::MAX_COUNT] = {};
}

bool MapScriptSGSL::hasTeamWon(unsigned teamNumber) const
{
	return teamNumber < Team::MAX_COUNT && harness::sgslTeamWon[teamNumber];
}

bool MapScriptSGSL::hasTeamLost(unsigned teamNumber) const
{
	return teamNumber < Team::MAX_COUNT && harness::sgslTeamLost[teamNumber];
}
