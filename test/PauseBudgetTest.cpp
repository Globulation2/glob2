// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors
//
// Pausing in network matches (src/gui/PauseBudget.h): rooms and LAN games pause
// freely; quick matches give every player three pauses and a minute in all, anyone
// may resume, and any client resumes a pause whose holder has run out.

#include "Glob2Test.h"

#include "PauseBudget.h"

TEST_SUITE("PauseBudget")
{
	TEST_CASE("rooms and LAN games pause without limit")
	{
		PauseBudget budget;
		for (int i = 0; i < 10; ++i)
		{
			CHECK(budget.canPause(0));
			budget.executed(0, true, i * 100000);
			CHECK_FALSE(budget.expired(i * 100000 + 90000));
			budget.executed(0, false, i * 100000 + 90000);
		}
		CHECK(budget.canPause(0));
	}

	TEST_CASE("a quick match allows three pauses and a minute in all, then resumes for everyone")
	{
		PauseBudget budget;
		budget.setRules({true});
		CHECK(budget.pausesLeft(1) == 3);
		budget.executed(1, true, 1000);
		CHECK(budget.pauser() == 1);
		CHECK(budget.timeLeftMs(1, 21000) == 40000);
		// The other player resumes it.
		budget.executed(0, false, 21000);
		CHECK(budget.pauser() == -1);
		CHECK(budget.pausesLeft(1) == 2);
		CHECK(budget.timeLeftMs(1, 50000) == 40000);
		// A second pause runs out of the remaining 40 seconds: any client resumes it.
		budget.executed(1, true, 60000);
		CHECK_FALSE(budget.expired(99000));
		CHECK(budget.expired(100000));
		budget.executed(0, false, 100500);
		CHECK_FALSE(budget.canPause(1)); // no time left, one pause left
		CHECK(budget.pausesLeft(1) == 1);
		CHECK(budget.timeLeftMs(1, 200000) == 0);
		// Player 0's budget is its own.
		CHECK(budget.canPause(0));
	}

	TEST_CASE("a pause beyond the count ends at once; a pause during a pause changes nothing")
	{
		PauseBudget budget;
		budget.setRules({true});
		for (int i = 0; i < 3; ++i)
		{
			budget.executed(0, true, i * 1000);
			budget.executed(0, false, i * 1000 + 100);
		}
		CHECK_FALSE(budget.canPause(0));
		// A modified client pauses anyway: the others resume it straight away.
		budget.executed(0, true, 10000);
		CHECK(budget.expired(10000));
		budget.executed(1, false, 10050);
		// Player 1 pauses; player 0's pause order during it neither takes it over nor counts.
		budget.executed(1, true, 20000);
		budget.executed(0, true, 21000);
		CHECK(budget.pauser() == 1);
		CHECK(budget.pausesLeft(1) == 2);
		budget.executed(0, false, 25000);
		CHECK(budget.timeLeftMs(1, 30000) == 55000);
	}
}
