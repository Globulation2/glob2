// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "NetGamePlayerManager.h"
#include "AINames.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Version.h"

#include <cstdio>
#include <vector>

// Match the other stream harnesses' SHA1 linkage.
#include "../gnupg/sha1.c"

// Only localization is stubbed; exercise the real manager, BasePlayer,
// GameHeader and binary player-info serialization without a GUI/string table.
namespace AINames
{
std::string getAIText(int id)
{
	return "AI-" + std::to_string(id);
}
}

namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok)
	{
		std::printf("FAIL: %s\n", what);
		++failures;
	}
}

bool samePlayer(const BasePlayer& a, const BasePlayer& b)
{
	return a.type == b.type && a.number == b.number && a.numberMask == b.numberMask
		&& a.playerID == b.playerID && a.name == b.name
		&& a.teamNumber == b.teamNumber && a.teamNumberMask == b.teamNumberMask;
}

void checkRoundTrip(GameHeader& header)
{
	using namespace GAGCore;
	auto* written = new MemoryStreamBackend;
	BinaryOutputStream output(written);
	header.savePlayerInfo(&output);
	auto* copy = new MemoryStreamBackend(*written);
	copy->seekFromStart(0);
	BinaryInputStream input(copy);
	GameHeader loaded;
	check(loaded.loadPlayerInfo(&input, VERSION_MINOR), "player-info load succeeds");
	check(loaded.getNumberOfPlayers() == header.getNumberOfPlayers(), "wire player count");
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		check(samePlayer(header.getBasePlayer(i), loaded.getBasePlayer(i)),
			"wire player fields, including compacted slot masks");
}

void testRemoval(int count, int removed, bool mixed, bool invertReady)
{
	GameHeader header;
	NetGamePlayerManager manager(header);
	manager.setNumberOfTeams(3);
	std::vector<BasePlayer> before;
	std::vector<bool> ready;
	for (int i = 0; i < count; ++i)
	{
		if (mixed && i % 2)
			manager.addAIPlayer(static_cast<AI::ImplementationID>(i % AI::SIZE));
		else
		{
			manager.addPerson(100 + i, "Human " + std::to_string(i));
			manager.setReadyToGo(100 + i, bool(i % 2) != invertReady);
		}
		before.push_back(header.getBasePlayer(i));
		ready.push_back(before.back().type >= BasePlayer::P_AI
			|| manager.isReadyToGo(100 + i));
	}

	// Exercise both the human departure and indexed AI-removal entry points.
	if (before[removed].type == BasePlayer::P_IP)
		manager.removePerson(before[removed].playerID);
	else
		manager.removePlayer(removed);
	check(header.getNumberOfPlayers() == count - 1, "removed exactly one player");
	bool allReady = true;
	for (int dest = 0; dest < count - 1; ++dest)
	{
		const int source = dest < removed ? dest : dest + 1;
		BasePlayer expected = before[source];
		expected.number = dest;
		expected.numberMask = Uint32(1) << dest;
		if (expected.type >= BasePlayer::P_AI)
			expected.name = AINames::getAIText(expected.type - BasePlayer::P_AI)
				+ " " + std::to_string(dest + 1);
		BasePlayer& actual = header.getBasePlayer(dest);
		check(samePlayer(actual, expected), "compaction preserves identity and team, updates slot and AI name");
		check(actual.checkSum() == expected.checkSum(), "compacted checksum matches canonical player");
		if (actual.type == BasePlayer::P_IP)
			check(manager.isReadyToGo(actual.playerID) == ready[source], "survivor readiness follows player");
		allReady = allReady && ready[source];
	}
	for (int i = count - 1; i < Team::MAX_COUNT; ++i)
		check(samePlayer(header.getBasePlayer(i), BasePlayer()), "unused slots are cleared");
	check(manager.isEveryoneReadyToGo() == allReady, "departure cannot bypass lobby readiness");
	checkRoundTrip(header);
}

void testRepeatedRemovalAndRejoin()
{
	GameHeader header;
	NetGamePlayerManager manager(header);
	manager.setNumberOfTeams(2);
	manager.addPerson(1, "Host");
	manager.addPerson(2, "Leaving");
	manager.addPerson(3, "Waiting");
	manager.removePerson(2);
	check(!manager.isReadyToGo(3), "next player stays unready after a departure");
	manager.setReadyToGo(3, true);
	check(manager.isEveryoneReadyToGo(), "survivor can ready up by unchanged ID");
	manager.addPerson(4, "Joining");
	check(!manager.isEveryoneReadyToGo(), "new joiner must ready up");
	manager.removePerson(4);
	check(manager.isEveryoneReadyToGo(), "removing unready last player clears readiness");
	manager.removePerson(3);
	manager.removePerson(1);
	check(header.getNumberOfPlayers() == 0 && manager.isEveryoneReadyToGo(), "drained lobby is clean");
	manager.addPerson(5, "New host");
	check(header.getBasePlayer(0).numberMask == 1 && manager.isReadyToGo(5), "empty lobby can be reused");
}

void testAllAITypes()
{
	// Include the highest encoded type, which exceeds the range of an enum
	// whose only named values are P_NONE through P_AI unless its type is fixed.
	for (int id = AI::NONE; id < AI::SIZE; ++id)
	{
		GameHeader header;
		NetGamePlayerManager manager(header);
		manager.setNumberOfTeams(2);
		manager.addPerson(1, "Host");
		manager.addAIPlayer(static_cast<AI::ImplementationID>(id));
		manager.removePerson(1);
		const BasePlayer& ai = header.getBasePlayer(0);
		check(ai.type == Uint32(BasePlayer::P_AI) + Uint32(id), "encoded AI type survives compaction");
		check(BasePlayer::implementationIdFromPlayerType(ai.type) == id, "AI implementation round-trips");
		check(ai.number == 0 && ai.numberMask == 1 && manager.isEveryoneReadyToGo(), "AI slot and readiness survive compaction");
		checkRoundTrip(header);
	}
}
}

int main()
{
	// Every valid removal position, including slot zero, the last player,
	// a full lobby and a sole player; both human-only and mixed AI lobbies.
	for (int count = 1; count <= Team::MAX_COUNT; ++count)
		for (int removed = 0; removed < count; ++removed)
			for (bool mixed : {false, true})
				for (bool invertReady : {false, true})
					testRemoval(count, removed, mixed, invertReady);
	testRepeatedRemovalAndRejoin();
	testAllAITypes();
	std::printf("NetGamePlayerManagerTest: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
