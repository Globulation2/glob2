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
// the program leaks the storage at exit. test/unit/stubs/MapHeaderStubs.cpp
// supplies the few non-inline methods WC.cpp invokes (MapHeader getters and
// the MapScriptSGSL hooks).
//
// The output is a deterministic stream of one-line records. To verify the
// recent cleanup of WinningConditions.cpp is behaviour-preserving, run the
// harness on the cleaned-up source, save stdout, then `git stash` the WC
// changes, rebuild, run, and `diff` the two outputs. Identical output =
// equivalent behaviour.

#include "Glob2Test.h"
#include "unit/stubs/MapHeaderStubs.h"
#include "WinningConditions.h"
#include "Game.h"
#include "Team.h"
#include "Player.h"
#include "MapHeader.h"
#include "SGSL.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace {

// Every record of the golden stream; compared with test/fixtures/winning-conditions/expected.txt.
std::string out;

void emit(const char* text)
{
	out += text;
}

template <typename... Args>
void emit(const char* format, Args... args)
{
	char line[256];
	std::snprintf(line, sizeof line, format, args...);
	out += line;
}

constexpr int kMaxTeams = 4;

alignas(Game) unsigned char gameStorage[sizeof(Game)];
alignas(Team) unsigned char teamStorage[kMaxTeams][sizeof(Team)];

Game* g() { return reinterpret_cast<Game*>(gameStorage); }
Team* T(int i) { return reinterpret_cast<Team*>(teamStorage[i]); }

void clearAll()
{
	std::memset(gameStorage, 0, sizeof(gameStorage));
	std::memset(teamStorage, 0, sizeof(teamStorage));
	std::memset(glob2test::sgsl::teamWon, 0, sizeof(glob2test::sgsl::teamWon));
	std::memset(glob2test::sgsl::teamLost, 0, sizeof(glob2test::sgsl::teamLost));
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
		emit("  %s team=%d won=%d lost=%d\n", tag, t, w ? 1 : 0, l ? 1 : 0);
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
		emit("Death/aliveMask=0x%x\n", aliveMask);
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
			emit("Allies/allyMask=0x%x winner=%d\n", ally, winner);
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
		emit("Allies/oneWay 0->1, 1 wins\n");
		WinningConditionAllies wc;
		emitWonLost("Allies", wc, N);
	}
}

// ---------------- Prestige ----------------
void testPrestige()
{
	constexpr int N = 3;
	struct Tile
	{
		const char* tag;
		int totalPrestige;
		int prestigeToReach;
		std::array<int, N> teamPrestige;
	};
	static const Tile tiles[] = {
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
	for (const auto& c : tiles)
	{
		clearAll();
		setupTeams(N);
		g()->totalPrestige = c.totalPrestige;
		g()->prestigeToReach = c.prestigeToReach;
		for (int i = 0; i < N; ++i) T(i)->prestige = c.teamPrestige[i];
		emit("Prestige/%s total=%d gate=%d prestiges=[%d,%d,%d]\n",
		            c.tag, c.totalPrestige, c.prestigeToReach,
		            c.teamPrestige[0], c.teamPrestige[1], c.teamPrestige[2]);
		WinningConditionPrestige wc;
		emitWonLost("Prestige", wc, N);
	}
}

// ---------------- SuddenDeath ----------------
void testSuddenDeath()
{
	constexpr int N = 3;
	struct Tile
	{
		const char* tag;
		Uint32 stepCounter;
		Uint32 endStepTick;
		std::array<int, N> teamPrestige;
	};
	static const Tile tiles[] = {
		{"beforeTick-noTie",    50,  100, {10, 30, 10}},
		{"beforeTick-tieAtTop", 50,  100, {30, 30, 10}},
		{"atTick-uniqueMax",    100, 100, {10, 30, 10}},
		{"atTick-tieAtTop",     100, 100, {30, 30, 10}},
		{"afterTick-uniqueMax", 150, 100, {10, 30, 10}},
		{"negativePrestige",    100, 100, {-5, 0, -10}},
		// All-negative: maximumPrestige()'s floor at 0 (shared with Prestige,
		// which never actually hits it thanks to its own totalPrestige gate)
		// would make every team compare against 0 here, so nobody ties it and
		// everybody loses. actualMaximumPrestige() must find team 1's -2 as
		// the true max instead, so team 1 (only) wins and the other two lose.
		{"allNegativePrestige", 100, 100, {-5, -2, -10}},
	};
	for (const auto& c : tiles)
	{
		clearAll();
		setupTeams(N);
		g()->stepCounter = c.stepCounter;
		for (int i = 0; i < N; ++i) T(i)->prestige = c.teamPrestige[i];
		emit("SuddenDeath/%s step=%u endTick=%u prestiges=[%d,%d,%d]\n",
		            c.tag, c.stepCounter, c.endStepTick,
		            c.teamPrestige[0], c.teamPrestige[1], c.teamPrestige[2]);
		WinningConditionSuddenDeath wc;
		wc.endStepTick = c.endStepTick;
		emitWonLost("SuddenDeath", wc, N);
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
				glob2test::sgsl::teamWon[i]  = (wMask & (1u << i)) != 0;
				glob2test::sgsl::teamLost[i] = (lMask & (1u << i)) != 0;
			}
			emit("Script/wonMask=0x%x lostMask=0x%x\n", wMask, lMask);
			WinningConditionScript wc;
			emitWonLost("Script", wc, N);
		}
	}
#else
	emit("Script/skipped (YOG_SERVER_ONLY)\n");
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
			emit("OpponentsDefeated/allyMask=0x%x lostMask=0x%x\n", ally, lostMask);
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
		emit("OpponentsDefeated/oneWay 0->1, 2 lost, 1 alive\n");
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
		emit("DefaultList/%d type=%d\n", idx++, static_cast<int>(wc->getType()));
	}
}

// Sets every team's hasWon/hasLost from `cond` the way Team::checkWinConditions
// would for a single-condition list, then lets WinningConditionAllies spread a
// win to mutual allies, as the default list does on later evaluation.
void resolveFlags(const WinningCondition& cond, int n)
{
	bool won[kMaxTeams] = {}, lost[kMaxTeams] = {};
	for (int t = 0; t < n; ++t)
	{
		won[t] = cond.hasTeamWon(t, g());
		lost[t] = !won[t] && cond.hasTeamLost(t, g());
	}
	for (int t = 0; t < n; ++t)
	{
		T(t)->hasWon = won[t];
		T(t)->hasLost = lost[t];
	}
	WinningConditionAllies allies;
	for (int t = 0; t < n; ++t)
		if (allies.hasTeamWon(t, g()))
		{
			T(t)->hasWon = true;
			T(t)->hasLost = false;
		}
}

void suddenDeathAtBuzzer(std::array<int, 3> prestige, Uint32 allianceMask)
{
	clearAll();
	setupTeams(3);
	mutualAlliances(3, allianceMask);
	g()->stepCounter = 100;
	for (int i = 0; i < 3; ++i) T(i)->prestige = prestige[i];
	WinningConditionSuddenDeath wc;
	wc.endStepTick = 100;
	resolveFlags(wc, 3);
}

// Seats `players[p]` on team p with the given player type, the way an online
// MatchSetup does: an empty seat is an AI::NONE player whose colony stays alive.
alignas(Player) unsigned char playerStorage[kMaxTeams][sizeof(Player)];

void seatPlayers(std::initializer_list<BasePlayer::PlayerType> types)
{
	std::memset(playerStorage, 0, sizeof(playerStorage));
	int p = 0;
	for (BasePlayer::PlayerType type : types)
	{
		Player* player = reinterpret_cast<Player*>(playerStorage[p]);
		player->type = type;
		player->teamNumber = p;
		g()->players[p] = player;
		++p;
	}
	g()->gameHeader.setNumberOfPlayers(p);
}

const BasePlayer::PlayerType kEmptySeat = BasePlayer::playerTypeFromImplementationID(AI::NONE);
const BasePlayer::PlayerType kRealAI = BasePlayer::playerTypeFromImplementationID(AI::NUMBI);

}  // namespace

TEST_SUITE("WinningConditions")
{
TEST_CASE("every predicate matches the golden record stream [golden]")
{
	out.clear();
	out += "# WinningConditionsHarness golden output\n";
	testFactoryOrder();
	testDeath();
	testAllies();
	testPrestige();
	testSuddenDeath();
	testScript();
	testOpponentsDefeated();
	glob2test::expectGolden("winning-conditions/expected.txt", out);
}

TEST_CASE("draw outcome: before the buzzer nobody is decided")
{
	clearAll();
	setupTeams(3);
	g()->stepCounter = 50;
	WinningConditionSuddenDeath wc;
	wc.endStepTick = 100;
	resolveFlags(wc, 3);
	for (int t = 0; t < 3; ++t) CHECK(classifyTeamOutcome(g(), t) == TeamOutcome::Undecided);
	CHECK_FALSE(isGameDrawn(g()));
}

TEST_CASE("draw outcome: a single winner wins and the rest lose")
{
	suddenDeathAtBuzzer({10, 30, 10}, 0);
	CHECK(classifyTeamOutcome(g(), 0) == TeamOutcome::Lost);
	CHECK(classifyTeamOutcome(g(), 1) == TeamOutcome::Won);
	CHECK(classifyTeamOutcome(g(), 2) == TeamOutcome::Lost);
	CHECK_FALSE(isGameDrawn(g()));
}

TEST_CASE("draw outcome: a tie at the top across alliances is a draw and teams below still lose")
{
	suddenDeathAtBuzzer({30, 30, 10}, 0);
	// The engine's flags are untouched: both tied teams are still "won".
	CHECK(T(0)->hasWon);
	CHECK(T(1)->hasWon);
	CHECK(classifyTeamOutcome(g(), 0) == TeamOutcome::Draw);
	CHECK(classifyTeamOutcome(g(), 1) == TeamOutcome::Draw);
	CHECK(classifyTeamOutcome(g(), 2) == TeamOutcome::Lost);
	CHECK(isGameDrawn(g()));
}

TEST_CASE("draw outcome: allied teams tied at the top share a win")
{
	suddenDeathAtBuzzer({30, 30, 10}, 0x3);
	CHECK(classifyTeamOutcome(g(), 0) == TeamOutcome::Won);
	CHECK(classifyTeamOutcome(g(), 1) == TeamOutcome::Won);
	CHECK(classifyTeamOutcome(g(), 2) == TeamOutcome::Lost);
	CHECK_FALSE(isGameDrawn(g()));
}

TEST_CASE("draw outcome: an ally of the sole leader shares its win")
{
	suddenDeathAtBuzzer({10, 30, 5}, 0x3);
	CHECK(T(0)->hasWon);
	CHECK(classifyTeamOutcome(g(), 0) == TeamOutcome::Won);
	CHECK(classifyTeamOutcome(g(), 1) == TeamOutcome::Won);
	CHECK(classifyTeamOutcome(g(), 2) == TeamOutcome::Lost);
	CHECK_FALSE(isGameDrawn(g()));
}

TEST_CASE("draw outcome: an alliance tied with an outsider is a draw for everyone at the top")
{
	suddenDeathAtBuzzer({30, 10, 30}, 0x3);
	for (int t = 0; t < 3; ++t) CHECK(classifyTeamOutcome(g(), t) == TeamOutcome::Draw);
	CHECK(isGameDrawn(g()));
}

TEST_CASE("draw outcome: a one-way alliance does not turn a tie into a shared win")
{
	suddenDeathAtBuzzer({30, 30, 10}, 0);
	T(0)->allies = T(0)->me | T(1)->me;
	CHECK(classifyTeamOutcome(g(), 0) == TeamOutcome::Draw);
	CHECK(classifyTeamOutcome(g(), 1) == TeamOutcome::Draw);
}

TEST_CASE("draw outcome: a prestige finish tied at the top across alliances is a draw")
{
	clearAll();
	setupTeams(3);
	g()->totalPrestige = 100;
	g()->prestigeToReach = 100;
	T(0)->prestige = 45;
	T(1)->prestige = 45;
	T(2)->prestige = 10;
	WinningConditionPrestige wc;
	resolveFlags(wc, 3);
	CHECK(classifyTeamOutcome(g(), 0) == TeamOutcome::Draw);
	CHECK(classifyTeamOutcome(g(), 1) == TeamOutcome::Draw);
	CHECK(classifyTeamOutcome(g(), 2) == TeamOutcome::Lost);
}

TEST_CASE("draw outcome: empty seats count for nobody")
{
	// The staging room match: a host and a guest on a four-team map, the other
	// two seats empty (AI::NONE). The guest left and lost; the sudden-death
	// buzzer then found the host and both idle colonies tied at the top.
	clearAll();
	setupTeams(4);
	seatPlayers({BasePlayer::P_LOCAL, BasePlayer::P_IP, kEmptySeat, kEmptySeat});
	CHECK(contestedTeamsMask(g()) == 0x3u);
	g()->stepCounter = 100;
	T(1)->prestige = -5;
	WinningConditionSuddenDeath wc;
	wc.endStepTick = 100;
	resolveFlags(wc, 4);
	// The engine's flags are untouched: the idle colonies are still "won".
	CHECK(T(2)->hasWon);
	CHECK(T(3)->hasWon);
	const Uint32 contested = contestedTeamsMask(g());
	CHECK(classifyTeamOutcome(g(), 0, contested) == TeamOutcome::Won);
	CHECK_FALSE(isGameDrawn(g(), contested));
	// Without the mask the idle colonies would turn the win into a draw.
	CHECK(classifyTeamOutcome(g(), 0) == TeamOutcome::Draw);
}

TEST_CASE("draw outcome: two players tied next to empty seats still draw")
{
	clearAll();
	setupTeams(4);
	seatPlayers({BasePlayer::P_LOCAL, BasePlayer::P_IP, kEmptySeat, kEmptySeat});
	g()->stepCounter = 100;
	WinningConditionSuddenDeath wc;
	wc.endStepTick = 100;
	resolveFlags(wc, 4);
	const Uint32 contested = contestedTeamsMask(g());
	CHECK(classifyTeamOutcome(g(), 0, contested) == TeamOutcome::Draw);
	CHECK(classifyTeamOutcome(g(), 1, contested) == TeamOutcome::Draw);
	CHECK(isGameDrawn(g(), contested));
}

TEST_CASE("draw outcome: a real AI tied at the top is a draw; replays count every team")
{
	clearAll();
	setupTeams(3);
	seatPlayers({BasePlayer::P_LOCAL, kRealAI, kEmptySeat});
	CHECK(contestedTeamsMask(g()) == 0x3u);
	g()->stepCounter = 100;
	WinningConditionSuddenDeath wc;
	wc.endStepTick = 100;
	resolveFlags(wc, 3);
	CHECK(classifyTeamOutcome(g(), 0, contestedTeamsMask(g())) == TeamOutcome::Draw);
	// A replay drives every player as AI::NONE: nothing to tell apart, so all count.
	seatPlayers({kEmptySeat, kEmptySeat, kEmptySeat});
	CHECK(contestedTeamsMask(g()) == ~Uint32(0));
	CHECK(isGameDrawn(g(), contestedTeamsMask(g())));
}
}
