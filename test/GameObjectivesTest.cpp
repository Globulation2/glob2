// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for GameObjectives index handling and scriptNumber
// serialization. Covers:
//   1. removeObjective bounds: out-of-range n (negative, past-end, empty
//      list) used to run vector::erase(begin() + n) on six parallel vectors
//      -- UB. Post-fix it is a silent no-op, matching the file's guard style.
//   2. Accessor bounds: setGameObjectiveText / setObjectiveType /
//      setScriptNumber / getScriptNumber used to assert only n < size(), so
//      negative n indexed vector[-1]. Post-fix all accessors share one
//      guard: setters no-op, getters return their documented defaults.
//   3. scriptNumber wire domain: the value is serialized as Uint8, so the
//      entry points clamp to [0..255] -- an out-of-domain value can no
//      longer diverge between memory and a save/load round-trip.
// Links libgag_server.a for BinaryStream + MemoryStreamBackend.

#include <cstdio>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "GameObjectives.h"
#include "Version.h"

using namespace GAGCore;

// SHA1 is wired in this project by direct .c-into-.cpp inclusion (see
// YOGServerPasswordRegistry.cpp); the .h has no extern "C" wrapper, so
// libgag_server.a's BinaryOutputStream references C++-mangled SHA1 names.
// Same trick as NetSendOrderDecodeTest.cpp to satisfy the linker.
#include "../gnupg/sha1.c"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		++failures;
}

void testRemoveObjectiveOutOfRangeIsNoOp()
{
	GameObjectives objectives;
	check(objectives.getNumberOfObjectives() == 1,
	      "fresh GameObjectives has the one default objective");

	objectives.removeObjective(5);
	check(objectives.getNumberOfObjectives() == 1,
	      "removeObjective past the end is a no-op");

	objectives.removeObjective(-1);
	check(objectives.getNumberOfObjectives() == 1,
	      "removeObjective with negative index is a no-op");

	check(objectives.getGameObjectiveText(0) == "[Defeat Your Oppenents]",
	      "default objective text survives out-of-range removals");
}

void testRemoveObjectiveInRangeRemovesAcrossAllFields()
{
	GameObjectives objectives;
	objectives.addNewObjective("second", true, true, false,
	                           GameObjectives::Secondary, 9);

	objectives.removeObjective(0);
	check(objectives.getNumberOfObjectives() == 1,
	      "in-range removeObjective shrinks the list");
	check(objectives.getGameObjectiveText(0) == "second",
	      "remaining objective keeps its text");
	check(objectives.getObjectiveType(0) == GameObjectives::Secondary,
	      "remaining objective keeps its type");
	check(objectives.getScriptNumber(0) == 9,
	      "remaining objective keeps its script number");
	check(!objectives.isObjectiveVisible(0),
	      "remaining objective keeps its hidden flag");
	check(objectives.isObjectiveComplete(0),
	      "remaining objective keeps its completed flag");
}

void testRemoveObjectiveOnEmptyListIsNoOp()
{
	GameObjectives objectives;
	objectives.removeObjective(0);
	check(objectives.getNumberOfObjectives() == 0,
	      "draining the list leaves it empty");

	objectives.removeObjective(0);
	check(objectives.getNumberOfObjectives() == 0,
	      "removeObjective on an empty list is a no-op");
}

void testSettersIgnoreOutOfRangeIndices()
{
	GameObjectives objectives;

	objectives.setGameObjectiveText(-1, "clobbered");
	objectives.setGameObjectiveText(5, "clobbered");
	check(objectives.getGameObjectiveText(0) == "[Defeat Your Oppenents]",
	      "setGameObjectiveText ignores out-of-range indices");

	objectives.setObjectiveType(-1, GameObjectives::Secondary);
	objectives.setObjectiveType(5, GameObjectives::Secondary);
	check(objectives.getObjectiveType(0) == GameObjectives::Primary,
	      "setObjectiveType ignores out-of-range indices");

	objectives.setScriptNumber(-1, 42);
	objectives.setScriptNumber(5, 42);
	check(objectives.getScriptNumber(0) == 1,
	      "setScriptNumber ignores out-of-range indices");
}

void testGettersReturnDefaultsForOutOfRangeIndices()
{
	GameObjectives objectives;

	check(objectives.getScriptNumber(-1) == GameObjectives::InvalidScriptNumber,
	      "getScriptNumber returns InvalidScriptNumber for negative index");
	check(objectives.getScriptNumber(5) == GameObjectives::InvalidScriptNumber,
	      "getScriptNumber returns InvalidScriptNumber past the end");
	check(objectives.getGameObjectiveText(-1) == "invalid",
	      "getGameObjectiveText returns the invalid marker out of range");
	check(objectives.getObjectiveType(-1) == GameObjectives::Invalid,
	      "getObjectiveType returns Invalid out of range");
	check(!objectives.isObjectiveVisible(-1),
	      "isObjectiveVisible is false out of range");
	check(!objectives.isObjectiveComplete(-1),
	      "isObjectiveComplete is false out of range");
	check(!objectives.isObjectiveFailed(-1),
	      "isObjectiveFailed is false out of range");
}

std::unique_ptr<BinaryInputStream> makeInputStream(const MemoryStreamBackend& written)
{
	// BinaryInputStream takes ownership of the backend.
	MemoryStreamBackend* copy = new MemoryStreamBackend(written);
	copy->seekFromStart(0);
	return std::make_unique<BinaryInputStream>(copy);
}

void testScriptNumberClampedToWireDomain()
{
	GameObjectives objectives;

	objectives.setScriptNumber(0, 300);
	check(objectives.getScriptNumber(0) == GameObjectives::MaxScriptNumber,
	      "setScriptNumber clamps values above 255 to MaxScriptNumber");

	objectives.setScriptNumber(0, -5);
	check(objectives.getScriptNumber(0) == 0,
	      "setScriptNumber clamps negative values to 0");

	objectives.addNewObjective("wide", false, false, false,
	                           GameObjectives::Primary, 999);
	check(objectives.getScriptNumber(1) == GameObjectives::MaxScriptNumber,
	      "addNewObjective clamps values above 255 to MaxScriptNumber");

	objectives.setScriptNumber(1, GameObjectives::MaxScriptNumber);
	check(objectives.getScriptNumber(1) == GameObjectives::MaxScriptNumber,
	      "setScriptNumber stores 255 unchanged");
}

void testEncodeDecodeRoundTripMatchesMemory()
{
	GameObjectives original;
	original.addNewObjective("secondary goal", true, false, true,
	                         GameObjectives::Secondary, 16);
	original.addNewObjective("wide script number", false, true, false,
	                         GameObjectives::Primary, 4000);

	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	original.encodeData(&ostream);

	GameObjectives loaded;
	std::unique_ptr<BinaryInputStream> istream = makeInputStream(*backend);
	loaded.decodeData(istream.get(), VERSION_MINOR);

	check(loaded.getNumberOfObjectives() == original.getNumberOfObjectives(),
	      "round-trip preserves the objective count");
	bool allMatch = true;
	for (int i = 0; i < original.getNumberOfObjectives(); ++i)
	{
		allMatch = allMatch
			&& loaded.getGameObjectiveText(i) == original.getGameObjectiveText(i)
			&& loaded.getObjectiveType(i) == original.getObjectiveType(i)
			&& loaded.isObjectiveVisible(i) == original.isObjectiveVisible(i)
			&& loaded.isObjectiveComplete(i) == original.isObjectiveComplete(i)
			&& loaded.isObjectiveFailed(i) == original.isObjectiveFailed(i)
			&& loaded.getScriptNumber(i) == original.getScriptNumber(i);
	}
	check(allMatch,
	      "round-trip preserves every field, including clamped script numbers");
}

} // namespace

int main()
{
	testRemoveObjectiveOutOfRangeIsNoOp();
	testRemoveObjectiveInRangeRemovesAcrossAllFields();
	testRemoveObjectiveOnEmptyListIsNoOp();
	testSettersIgnoreOutOfRangeIndices();
	testGettersReturnDefaultsForOutOfRangeIndices();
	testScriptNumberClampedToWireDomain();
	testEncodeDecodeRoundTripMatchesMemory();

	if (failures == 0)
		std::printf("all tests passed\n");
	else
		std::printf("%d check(s) FAILED\n", failures);
	return failures == 0 ? 0 : 1;
}
