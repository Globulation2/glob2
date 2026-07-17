// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for BasePlayer::playerID serialization: the field is
// Uint32 (the YOG/internet identifier) but save()/load() moved it over the
// wire as Uint16, silently truncating IDs above 0xFFFF. Post-fix, version 86
// saves the full Uint32 and load() gates the width on versionMinor.
// Exercised here:
//   1. version-86 round-trip -> playerID > 0xFFFF preserved, and every
//      following field still decodes correctly
//   2. hand-written pre-86 stream (Uint16 playerID on the wire) -> loads
//      correctly, with the fields after playerID still aligned
// Links libgag_server.a for BinaryStream + MemoryStreamBackend.

#include <cstdio>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "BasePlayer.h"
#include "Version.h"

using namespace GAGCore;

// SHA1 is wired in this project by direct .c-into-.cpp inclusion (see
// YOGServerPasswordRegistry.cpp); the .h has no extern "C" wrapper, so
// libgag_server.a's BinaryOutputStream references C++-mangled SHA1 names.
// Same trick as BulletSaveLoadTest.cpp to satisfy the linker.
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

void testRoundTripCurrentVersion()
{
	BasePlayer original(3, "Wide Id Player", 5, BasePlayer::P_IP);
	original.playerID = 0x12345678;  // would truncate to 0x5678 pre-fix

	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	original.save(&ostream);

	auto istream = makeInputStream(*backend);
	BasePlayer loaded;
	check(loaded.load(istream.get(), VERSION_MINOR), "roundTrip: load succeeds");

	check(loaded.type == BasePlayer::P_IP, "roundTrip: type preserved");
	check(loaded.number == 3 && loaded.numberMask == (Uint32(1) << 3),
	      "roundTrip: number preserved");
	check(loaded.playerID == 0x12345678, "roundTrip: playerID > 0xFFFF preserved");
	check(loaded.name == "Wide Id Player", "roundTrip: name preserved");
	check(loaded.teamNumber == 5 && loaded.teamNumberMask == (Uint32(1) << 5),
	      "roundTrip: teamNumber preserved");
}

void testPre86StreamReadsUint16PlayerID()
{
	// Hand-write the version-85 wire layout: playerID is a Uint16.
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	ostream.writeUint32((Uint32)BasePlayer::P_LOCAL, "type");
	ostream.writeSint32(2, "number");
	ostream.writeUint32(Uint32(1) << 2, "numberMask");
	ostream.writeUint16(0xBEEF, "playerID");
	ostream.writeText("Old Save Player", "name");
	ostream.writeSint32(7, "teamNumber");
	ostream.writeUint32(Uint32(1) << 7, "teamNumberMask");

	auto istream = makeInputStream(*backend);
	BasePlayer loaded;
	check(loaded.load(istream.get(), 85), "pre86: load succeeds");

	check(loaded.type == BasePlayer::P_LOCAL, "pre86: type read");
	check(loaded.number == 2, "pre86: number read");
	check(loaded.playerID == 0xBEEF, "pre86: playerID read as Uint16");
	check(loaded.name == "Old Save Player", "pre86: name still aligned");
	check(loaded.teamNumber == 7 && loaded.teamNumberMask == (Uint32(1) << 7),
	      "pre86: teamNumber still aligned");
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	testRoundTripCurrentVersion();
	testPre86StreamReadsUint16PlayerID();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
