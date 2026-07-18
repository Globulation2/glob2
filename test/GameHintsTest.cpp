// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for GameHints scriptNumber serialization: the value is
// an int in memory but travels as a single byte on the wire (encodeData
// writes Uint8), so values above 255 (or below 0) silently truncated on save
// and came back different after a reload, breaking the SGSL
// getScriptNumber(i) == n hint lookups. Post-fix the entry points
// (addNewHint / setScriptNumber) clamp to the documented [0..255] wire
// domain, mirroring GameObjectives. Covers:
//   1. clamping at both entry points (above, below, and at the boundary)
//   2. a full encode/decode round-trip preserving every field, including
//      clamped script numbers
// Links libgag_server.a for BinaryStream + MemoryStreamBackend.

#include <cstdio>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "GameHints.h"
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

std::unique_ptr<BinaryInputStream> makeInputStream(const MemoryStreamBackend& written)
{
	// BinaryInputStream takes ownership of the backend.
	MemoryStreamBackend* copy = new MemoryStreamBackend(written);
	copy->seekFromStart(0);
	return std::make_unique<BinaryInputStream>(copy);
}

void testScriptNumberClampedToWireDomain()
{
	GameHints hints;

	hints.addNewHint("wide", false, 300);
	check(hints.getScriptNumber(0) == GameHints::MaxScriptNumber,
	      "addNewHint clamps values above 255 to MaxScriptNumber");

	hints.addNewHint("negative", false, -5);
	check(hints.getScriptNumber(1) == 0,
	      "addNewHint clamps negative values to 0");

	hints.setScriptNumber(0, 999);
	check(hints.getScriptNumber(0) == GameHints::MaxScriptNumber,
	      "setScriptNumber clamps values above 255 to MaxScriptNumber");

	hints.setScriptNumber(0, -1);
	check(hints.getScriptNumber(0) == 0,
	      "setScriptNumber clamps negative values to 0");

	hints.setScriptNumber(0, GameHints::MaxScriptNumber);
	check(hints.getScriptNumber(0) == GameHints::MaxScriptNumber,
	      "setScriptNumber stores 255 unchanged");

	hints.setScriptNumber(1, 8);
	check(hints.getScriptNumber(1) == 8,
	      "setScriptNumber stores in-domain values unchanged");
}

void testEncodeDecodeRoundTripMatchesMemory()
{
	GameHints original;
	original.addNewHint("first hint", false, 1);
	original.addNewHint("hidden hint", true, 8);
	original.addNewHint("wide script number", false, 4000);

	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	original.encodeData(&ostream);

	GameHints loaded;
	std::unique_ptr<BinaryInputStream> istream = makeInputStream(*backend);
	loaded.decodeData(istream.get(), VERSION_MINOR);

	check(loaded.getNumberOfHints() == original.getNumberOfHints(),
	      "round-trip preserves the hint count");
	bool allMatch = true;
	for (int i = 0; i < original.getNumberOfHints(); ++i)
	{
		allMatch = allMatch
			&& loaded.getGameHintText(i) == original.getGameHintText(i)
			&& loaded.isHintVisible(i) == original.isHintVisible(i)
			&& loaded.getScriptNumber(i) == original.getScriptNumber(i);
	}
	check(allMatch,
	      "round-trip preserves every field, including clamped script numbers");
}

} // namespace

int main()
{
	testScriptNumberClampedToWireDomain();
	testEncodeDecodeRoundTripMatchesMemory();

	if (failures == 0)
		std::printf("all tests passed\n");
	else
		std::printf("%d check(s) FAILED\n", failures);
	return failures == 0 ? 0 : 1;
}
