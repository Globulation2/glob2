// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for GameHeader::setDefaultAlliances, the widget-free
// alliance layout used by CustomGameScreen::updatePlayers. The screen used
// to track the human's color with an `int humanColor = 0;` sentinel: with no
// active human, team 0 was silently treated as "the human's team", so every
// AI with a different color was flagged onto the enemy ally team and an AI
// on color 0 was treated as the human's teammate. The helper models "no
// human" explicitly with std::optional. Exercised here:
//   1. human on color 0, AIs elsewhere -> human team 1, AI teams 2
//   2. AI sharing the human's color shares its team (ally slot stays 1)
//   3. no human -> the reset() free-for-all layout (i+1), no team-2 flagging
//   4. stale ally numbers from a previous layout are fully reset
// Links libgag_server.a for the Stream surface GameHeader.cpp pulls in.

#include <cstdio>
#include <SDL.h>
#include "GameHeader.h"

// Same direct sha1.c inclusion as GameHeaderTextSaveLoadTest.cpp:
// libgag_server.a objects reference C++-mangled SHA1 names with no
// extern "C" wrapper.
#include "../gnupg/sha1.c"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		++failures;
}

// True if every team not listed as human or AI still carries the reset()
// singleton value i+1.
bool untouchedTeamsAtDefault(GameHeader& header, std::initializer_list<int> assigned)
{
	for (int i = 0; i < Team::MAX_COUNT; ++i)
	{
		bool isAssigned = false;
		for (int a : assigned)
			if (a == i)
				isAssigned = true;
		if (!isAssigned && header.getAllyTeamNumber(i) != i + 1)
			return false;
	}
	return true;
}

void testHumanOnColorZero()
{
	GameHeader header;
	header.setDefaultAlliances(0, {1, 2});
	check(header.getAllyTeamNumber(0) == 1, "human color 0 joins ally team 1");
	check(header.getAllyTeamNumber(1) == 2 && header.getAllyTeamNumber(2) == 2,
	      "AI colors 1 and 2 join ally team 2");
	check(untouchedTeamsAtDefault(header, {0, 1, 2}),
	      "unassigned teams keep the free-for-all default");
}

void testAiSharingHumanColor()
{
	GameHeader header;
	header.setDefaultAlliances(2, {2, 0});
	check(header.getAllyTeamNumber(2) == 1,
	      "AI sharing the human's color stays on the human's ally team");
	check(header.getAllyTeamNumber(0) == 2,
	      "AI on color 0 is an enemy when the human is elsewhere");
	check(untouchedTeamsAtDefault(header, {0, 2}),
	      "unassigned teams keep the free-for-all default");
}

void testNoHuman()
{
	GameHeader header;
	header.setDefaultAlliances(std::nullopt, {1, 2, 3});
	check(untouchedTeamsAtDefault(header, {}),
	      "no human -> every team keeps its singleton ally group");
}

void testStaleLayoutReset()
{
	GameHeader header;
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		header.setAllyTeamNumber(i, 7);
	header.setDefaultAlliances(1, {3});
	check(header.getAllyTeamNumber(1) == 1 && header.getAllyTeamNumber(3) == 2,
	      "new layout applied over a stale one");
	check(untouchedTeamsAtDefault(header, {1, 3}),
	      "stale ally numbers reset to the free-for-all default");
}

} // namespace

int main()
{
	testHumanOnColorZero();
	testAiSharingHumanColor();
	testNoHuman();
	testStaleLayoutReset();
	if (failures == 0)
		std::printf("All GameHeaderDefaultAlliancesTest checks passed\n");
	else
		std::printf("%d GameHeaderDefaultAlliancesTest check(s) FAILED\n", failures);
	return failures == 0 ? 0 : 1;
}
