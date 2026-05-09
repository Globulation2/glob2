// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Behaviour-equivalence harness for src/WinningConditions.cpp.
//
// The five WinningCondition predicates (hasTeamWon/hasTeamLost on Death,
// Allies, Prestige, Script, OpponentsDefeated) read only a small slice of
// game state:
//   Team:  me, allies, prestige, isAlive, hasWon, hasLost
//   Game:  mapHeader.numberOfTeams (via getNumberOfTeams), totalPrestige,
//          prestigeToReach, sgslScript (hasTeamWon/hasTeamLost only).
//
// Constructing real Team objects pulls in Unit/Building/Race; constructing
// a real Game pulls in globalContainer / replayWriter / SGSL / Map. None of
// that is needed for these predicates. The harness therefore allocates raw
// aligned storage for one Game and N Teams and writes only the fields the
// predicates consult. The storage is never destructed -- non-trivial member
// destructors for std::list/std::map etc. on Team and Game are skipped, and
// the program leaks the storage at exit. WinningConditionsTestStubs.cpp
// supplies the few non-inline methods WC.cpp invokes (MapHeader getters and
// the MapScriptSGSL hooks).
//
// The output is a deterministic stream of one-line records. To verify the
// recent cleanup of WinningConditions.cpp is behaviour-preserving, run the
// harness on the cleaned-up source, save stdout, then `git stash` the WC
// changes, rebuild, run, and `diff` the two outputs. Identical output =
// equivalent behaviour.

#include "WinningConditions.h"
#include "Game.h"
#include "Team.h"
#include "MapHeader.h"
#include "SGSL.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace harness {
	// Defined in WinningConditionsTestStubs.cpp -- the SGSL stub reads these.
	extern bool sgslTeamWon[32];
	extern bool sgslTeamLost[32];
}

namespace {

constexpr int kMaxTeams = 4;

alignas(Game) unsigned char gameStorage[sizeof(Game)];
alignas(Team) unsigned char teamStorage[kMaxTeams][sizeof(Team)];

Game* g() { return reinterpret_cast<Game*>(gameStorage); }
Team* T(int i) { return reinterpret_cast<Team*>(teamStorage[i]); }

void clearAll()
{
	std::memset(gameStorage, 0, sizeof(gameStorage));
	std::memset(teamStorage, 0, sizeof(teamStorage));
	std::memset(harness::sgslTeamWon, 0, sizeof(harness::sgslTeamWon));
	std::memset(harness::sgslTeamLost, 0, sizeof(harness::sgslTeamLost));
}

void setupTeams(int n)
{
	g()->mapHeader.setNumberOfTeams(n);
	g()->totalPrestige = 0;
	g()->prestigeToReach = 0;
	for (int i = 0; i < n; ++i)
	{
		g()->teams[i] = T(i);
		T(i)->me = 1u << i;
		T(i)->allies = 0;
		T(i)->prestige = 0;
		T(i)->isAlive = true;
		T(i)->hasWon = false;
		T(i)->hasLost = false;
	}
}

// Mutually ally every team whose bit is set in `mask` -- each such team's
// `allies` becomes the OR of all those teams' `me` masks.
void mutualAlliances(int n, Uint32 mask)
{
	Uint32 m = 0;
	for (int i = 0; i < n; ++i)
		if (mask & (1u << i)) m |= T(i)->me;
	for (int i = 0; i < n; ++i)
		if (mask & (1u << i)) T(i)->allies = m;
}

void emitWonLost(const char* tag, WinningCondition& cond, int n)
{
	for (int t = 0; t < n; ++t)
	{
		const bool w = cond.hasTeamWon(t, g());
		const bool l = cond.hasTeamLost(t, g());
		std::printf("  %s team=%d won=%d lost=%d\n", tag, t, w ? 1 : 0, l ? 1 : 0);
	}
}

// ---------------- Death ----------------
void testDeath()
{
	constexpr int N = 3;
	for (unsigned aliveMask = 0; aliveMask < (1u << N); ++aliveMask)
	{
		clearAll();
		setupTeams(N);
		for (int i = 0; i < N; ++i)
			T(i)->isAlive = (aliveMask & (1u << i)) != 0;
		std::printf("Death/aliveMask=0x%x\n", aliveMask);
		WinningConditionDeath wc;
		emitWonLost("Death", wc, N);
	}
}

// ---------------- Allies ----------------
void testAllies()
{
	constexpr int N = 3;
	// All 8 alliance partitions on 3 teams (treated as mutual-ally subsets;
	// 0b000 = no alliances, 0b011 = {0,1} mutually allied, etc.). Self-only
	// alliances (one bit) reduce to "no allies", which is fine.
	for (Uint32 ally = 0; ally < (1u << N); ++ally)
	{
		// hasWon flag on -1 (none), 0, 1, or 2.
		for (int winner = -1; winner < N; ++winner)
		{
			clearAll();
			setupTeams(N);
			mutualAlliances(N, ally);
			if (winner >= 0) T(winner)->hasWon = true;
			std::printf("Allies/allyMask=0x%x winner=%d\n", ally, winner);
			WinningConditionAllies wc;
			emitWonLost("Allies", wc, N);
		}
	}

	// One-way alliances: team 0 lists team 1 as an ally, but not vice versa.
	// Must NOT count as mutual; team 0 should not win even if team 1 has won.
	{
		clearAll();
		setupTeams(N);
		T(0)->allies = T(0)->me | T(1)->me;
		T(1)->allies = T(1)->me;
		T(2)->allies = T(2)->me;
		T(1)->hasWon = true;
		std::printf("Allies/oneWay 0->1, 1 wins\n");
		WinningConditionAllies wc;
		emitWonLost("Allies", wc, N);
	}
}

// ---------------- Prestige ----------------
void testPrestige()
{
	constexpr int N = 3;
	struct Case
	{
		const char* tag;
		int totalPrestige;
		int prestigeToReach;
		std::array<int, N> teamPrestige;
	};
	static const Case cases[] = {
		{"all-zero-belowGate",   0,   100, {0, 0, 0}},
		{"all-zero-atGate",      100, 100, {0, 0, 0}},
		{"belowGate-noTie",      50,  100, {10, 30, 10}},
		{"atGate-uniqueMax",     100, 100, {10, 30, 10}},
		{"atGate-tieAtTop",      100, 100, {30, 30, 10}},
		{"atGate-allTied",       100, 100, {25, 25, 25}},
		{"aboveGate-uniqueMax",  200, 100, {50, 100, 25}},
		{"aboveGate-tieAtTop",   200, 100, {100, 100, 25}},
		{"negativePrestige",     100, 100, {-5, 0, -10}},
	};
	for (const auto& c : cases)
	{
		clearAll();
		setupTeams(N);
		g()->totalPrestige = c.totalPrestige;
		g()->prestigeToReach = c.prestigeToReach;
		for (int i = 0; i < N; ++i) T(i)->prestige = c.teamPrestige[i];
		std::printf("Prestige/%s total=%d gate=%d prestiges=[%d,%d,%d]\n",
		            c.tag, c.totalPrestige, c.prestigeToReach,
		            c.teamPrestige[0], c.teamPrestige[1], c.teamPrestige[2]);
		WinningConditionPrestige wc;
		emitWonLost("Prestige", wc, N);
	}
}

// ---------------- Script ----------------
void testScript()
{
#ifndef YOG_SERVER_ONLY
	constexpr int N = 3;
	for (unsigned wMask = 0; wMask < (1u << N); ++wMask)
	{
		for (unsigned lMask = 0; lMask < (1u << N); ++lMask)
		{
			clearAll();
			setupTeams(N);
			for (int i = 0; i < N; ++i)
			{
				harness::sgslTeamWon[i]  = (wMask & (1u << i)) != 0;
				harness::sgslTeamLost[i] = (lMask & (1u << i)) != 0;
			}
			std::printf("Script/wonMask=0x%x lostMask=0x%x\n", wMask, lMask);
			WinningConditionScript wc;
			emitWonLost("Script", wc, N);
		}
	}
#else
	std::printf("Script/skipped (YOG_SERVER_ONLY)\n");
#endif
}

// ---------------- OpponentsDefeated ----------------
void testOpponentsDefeated()
{
	constexpr int N = 3;
	for (Uint32 ally = 0; ally < (1u << N); ++ally)
	{
		for (unsigned lostMask = 0; lostMask < (1u << N); ++lostMask)
		{
			clearAll();
			setupTeams(N);
			mutualAlliances(N, ally);
			for (int i = 0; i < N; ++i)
				T(i)->hasLost = (lostMask & (1u << i)) != 0;
			std::printf("OpponentsDefeated/allyMask=0x%x lostMask=0x%x\n", ally, lostMask);
			WinningConditionOpponentsDefeated wc;
			emitWonLost("OppDef", wc, N);
		}
	}

	// One-way alliance: team 0 lists team 1 as ally, team 1 does not. From
	// team 0's perspective team 1 must still count as an enemy (mutual check
	// fails) -- so if team 1 is undefeated, team 0 should NOT win.
	{
		clearAll();
		setupTeams(N);
		T(0)->allies = T(0)->me | T(1)->me;
		T(1)->allies = T(1)->me;
		T(2)->allies = T(2)->me;
		T(2)->hasLost = true;
		std::printf("OpponentsDefeated/oneWay 0->1, 2 lost, 1 alive\n");
		WinningConditionOpponentsDefeated wc;
		emitWonLost("OppDef", wc, N);
	}
}

// ---------------- factory dispatch ----------------
// Walks getDefaultWinningConditions(), reports the type of each condition in
// listed order. Touches the cleaned-up factory only via getType(); a
// regression in the type-tag dispatch would surface as a different list.
void testFactoryOrder()
{
	auto wcs = WinningCondition::getDefaultWinningConditions();
	int idx = 0;
	for (const auto& wc : wcs)
	{
		std::printf("DefaultList/%d type=%d\n", idx++, static_cast<int>(wc->getType()));
	}
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	std::printf("# WinningConditionsHarness golden output\n");
	testFactoryOrder();
	testDeath();
	testAllies();
	testPrestige();
	testScript();
	testOpponentsDefeated();
	return 0;
}
