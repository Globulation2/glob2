// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for the BaseTeam "colorPAD" wire byte: load() pins
// color.a to Color::ALPHA_OPAQUE but save() used to write the live alpha,
// so a team whose in-memory alpha ever differed from opaque would produce
// a save->load->save binary drift on that byte. Post-fix, save() writes
// ALPHA_OPAQUE unconditionally. Exercised here:
//   1. round-trip with a non-opaque in-memory alpha -> loaded alpha is
//      opaque and every following field still decodes correctly
//   2. save->load->save with a non-opaque alpha -> the two saved byte
//      streams are identical (the drift the bug produced)
// Race::load is only reached for versionMinor < 73 streams, which this
// harness never writes, so Race is stubbed out below.
// Links libgag_server.a for BinaryStream + MemoryStreamBackend.

#include <cstdio>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "BaseTeam.h"
#include "Race.h"
#include "Version.h"

using namespace GAGCore;

// SHA1 is wired in this project by direct .c-into-.cpp inclusion (see
// YOGServerPasswordRegistry.cpp); the .h has no extern "C" wrapper, so
// libgag_server.a's BinaryOutputStream references C++-mangled SHA1 names.
// Same trick as BulletSaveLoadTest.cpp to satisfy the linker.
#include "../gnupg/sha1.c"

// BaseTeam::load only constructs a Race for pre-73 streams, which this
// harness never produces; stub the symbols instead of linking Race.cpp.
Race::Race() {}
Race::~Race() {}
bool Race::load(GAGCore::InputStream*, Sint32) { return true; }

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

BaseTeam makeNonOpaqueTeam()
{
	BaseTeam team;
	team.type = BaseTeam::T_HUMAN;
	team.teamNumber = 4;
	team.numberOfPlayer = 2;
	team.color = Color(10, 20, 30, 0x42);  // alpha deliberately non-opaque
	team.playersMask = 0x0000000C;
	return team;
}

std::string saveToBytes(const BaseTeam& team)
{
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	team.save(&ostream);
	backend->seekFromEnd(0);
	const size_t length = backend->getPosition();
	std::string bytes(length, '\0');
	backend->seekFromStart(0);
	backend->read(bytes.data(), length);
	return bytes;
}

void testRoundTripPinsAlphaOpaque()
{
	const BaseTeam original = makeNonOpaqueTeam();

	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	original.save(&ostream);

	auto istream = makeInputStream(*backend);
	BaseTeam loaded;
	check(loaded.load(istream.get(), VERSION_MINOR), "roundTrip: load succeeds");

	check(loaded.type == BaseTeam::T_HUMAN, "roundTrip: type preserved");
	check(loaded.teamNumber == 4, "roundTrip: teamNumber preserved");
	check(loaded.numberOfPlayer == 2, "roundTrip: numberOfPlayer preserved");
	check(loaded.color.r == 10 && loaded.color.g == 20 && loaded.color.b == 30,
	      "roundTrip: rgb preserved");
	check(loaded.color.a == Color::ALPHA_OPAQUE, "roundTrip: alpha pinned opaque");
	check(loaded.playersMask == 0x0000000C,
	      "roundTrip: playersMask still aligned after pad byte");
}

void testSaveLoadSaveIsByteStable()
{
	const BaseTeam original = makeNonOpaqueTeam();
	const std::string firstSave = saveToBytes(original);

	MemoryStreamBackend* backend = new MemoryStreamBackend;
	BinaryOutputStream ostream(backend);
	original.save(&ostream);
	auto istream = makeInputStream(*backend);
	BaseTeam loaded;
	check(loaded.load(istream.get(), VERSION_MINOR), "byteStable: load succeeds");

	const std::string secondSave = saveToBytes(loaded);
	check(firstSave == secondSave, "byteStable: save->load->save byte-identical");
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	testRoundTripPinsAlphaOpaque();
	testSaveLoadSaveIsByteStable();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
