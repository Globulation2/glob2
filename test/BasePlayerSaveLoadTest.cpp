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
// Also covers BasePlayer::load's corruption validation: out-of-range type
// values are rejected before the enum cast (including the highest valid
// P_AI+n slot on the accept side), out-of-range number/teamNumber are
// rejected, and an exhausted stream fails at entry instead of zero-filling.
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

// Hand-write a current-version wire record with arbitrary raw field values,
// so corruption cases the type-safe save() cannot produce are expressible.
std::unique_ptr<BinaryInputStream> makeRawRecordStream(Uint32 rawType, Sint32 number,
                                                       Sint32 teamNumber)
{
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	ostream.writeUint32(rawType, "type");
	ostream.writeSint32(number, "number");
	ostream.writeUint32(number >= 0 ? Uint32(1) << number : 0, "numberMask");
	ostream.writeUint32(42, "playerID");
	ostream.writeText("Raw Player", "name");
	ostream.writeSint32(teamNumber, "teamNumber");
	ostream.writeUint32(teamNumber >= 0 ? Uint32(1) << teamNumber : 0, "teamNumberMask");
	return makeInputStream(*backend);
}

void testTypeValidation()
{
	const Uint32 firstInvalidType = Uint32(BasePlayer::P_AI) + Uint32(AI::SIZE);

	auto istream = makeRawRecordStream(firstInvalidType, 1, 1);
	BasePlayer loaded;
	check(!loaded.load(istream.get(), VERSION_MINOR),
	      "typeValidation: first out-of-range type rejected");

	istream = makeRawRecordStream(0xFFFFFFFFu, 1, 1);
	check(!loaded.load(istream.get(), VERSION_MINOR),
	      "typeValidation: hostile 0xFFFFFFFF type rejected");

	// Highest valid slot: P_AI + last AI implementation.
	const Uint32 maxValidType = firstInvalidType - 1;
	istream = makeRawRecordStream(maxValidType, 1, 1);
	check(loaded.load(istream.get(), VERSION_MINOR),
	      "typeValidation: highest P_AI+n type accepted");
	check((Uint32)loaded.type == maxValidType, "typeValidation: type value preserved");
}

void testNumberAndTeamNumberValidation()
{
	BasePlayer loaded;

	auto istream = makeRawRecordStream((Uint32)BasePlayer::P_LOCAL, Team::MAX_COUNT, 1);
	check(!loaded.load(istream.get(), VERSION_MINOR),
	      "rangeValidation: number == Team::MAX_COUNT rejected");

	istream = makeRawRecordStream((Uint32)BasePlayer::P_LOCAL, -1, 1);
	check(!loaded.load(istream.get(), VERSION_MINOR),
	      "rangeValidation: negative number rejected");

	istream = makeRawRecordStream((Uint32)BasePlayer::P_LOCAL, 1, Team::MAX_COUNT);
	check(!loaded.load(istream.get(), VERSION_MINOR),
	      "rangeValidation: teamNumber == Team::MAX_COUNT rejected");
}

void testExhaustedStreamFails()
{
	// Empty stream: load must fail at entry, not zero-fill a "valid" player.
	BinaryInputStream istream(new MemoryStreamBackend);
	BasePlayer loaded;
	check(!loaded.load(&istream, VERSION_MINOR), "exhausted: empty stream rejected");
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	testRoundTripCurrentVersion();
	testPre86StreamReadsUint16PlayerID();
	testTypeValidation();
	testNumberAndTeamNumberValidation();
	testExhaustedStreamFails();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
