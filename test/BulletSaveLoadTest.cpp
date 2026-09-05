// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for Bullet::ticksInitial serialization: save() used to
// omit the field and load() hardcoded it to 0, so any bullet in flight at
// save time lost its ballistic-arc rendering after a reload (the overlay
// divides by ticksInitial, guarded by a zero check, so the arc silently
// flattened). Post-fix, version 85 saves the field and load() gates the read
// on versionMinor, defaulting to ticksLeft for older saves. Exercised here:
//   1. version-85 round-trip -> every field preserved, including ticksInitial
//   2. pre-85 stream (no ticksInitial on the wire) -> defaults to ticksLeft,
//      and every following field still decodes from the right offset
// Links libgag_server.a for BinaryStream + MemoryStreamBackend.

#include <cstdio>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Bullet.h"
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

void testRoundTripCurrentVersion()
{
	Bullet original(11, 22, 3, -4, 40, 7, 55, 66, 1, 2, 3, 4);
	// A bullet mid-flight: ticksInitial stays at launch value, ticksLeft drops.
	original.step();
	original.step();

	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	original.save(&ostream);

	auto istream = makeInputStream(*backend);
	Bullet loaded(istream.get(), VERSION_MINOR);

	check(loaded.px == original.px && loaded.py == original.py, "roundTrip: position preserved");
	check(loaded.speedX == 3 && loaded.speedY == -4, "roundTrip: speed preserved");
	check(loaded.ticksLeft == 38, "roundTrip: ticksLeft preserved");
	check(loaded.ticksInitial == 40, "roundTrip: ticksInitial preserved");
	check(loaded.shootDamage == 7, "roundTrip: shootDamage preserved");
	check(loaded.targetX == 55 && loaded.targetY == 66, "roundTrip: target preserved");
	check(loaded.revealX == 1 && loaded.revealY == 2 && loaded.revealW == 3 && loaded.revealH == 4,
	      "roundTrip: reveal area preserved");
}

void testPre85StreamDefaultsToTicksLeft()
{
	// Hand-write the version-84 wire layout: no ticksInitial after ticksLeft.
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	ostream.writeSint32(11, "px");
	ostream.writeSint32(22, "py");
	ostream.writeSint32(3, "speedX");
	ostream.writeSint32(-4, "speedY");
	ostream.writeSint32(38, "ticksLeft");
	ostream.writeSint32(7, "shootDamage");
	ostream.writeSint32(55, "targetX");
	ostream.writeSint32(66, "targetY");
	ostream.writeSint32(1, "revealX");
	ostream.writeSint32(2, "revealY");
	ostream.writeSint32(3, "revealW");
	ostream.writeSint32(4, "revealH");

	auto istream = makeInputStream(*backend);
	Bullet loaded(istream.get(), 84);

	check(loaded.ticksInitial == 38, "pre85: ticksInitial defaults to ticksLeft");
	check(loaded.ticksLeft == 38, "pre85: ticksLeft read");
	check(loaded.shootDamage == 7, "pre85: shootDamage still aligned");
	check(loaded.revealX == 1 && loaded.revealY == 2 && loaded.revealW == 3 && loaded.revealH == 4,
	      "pre85: reveal area still aligned");
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	testRoundTripCurrentVersion();
	testPre85StreamDefaultsToTicksLeft();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
