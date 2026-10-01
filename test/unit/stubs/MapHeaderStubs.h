// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Team.h"

// Backing state for the MapScriptSGSL stubs; WinningConditions tests set these.
namespace glob2test::sgsl
{
	extern bool teamWon[Team::MAX_COUNT];
	extern bool teamLost[Team::MAX_COUNT];
}
