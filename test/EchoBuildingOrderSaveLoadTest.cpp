// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Regression harness for AIEcho::Construction::BuildingOrder::id serialization.
// The id is handed out at runtime by Echo::add_building_order (via
// BuildingRegister::register_building) and is the key the order is known by in
// BuildingRegister::pending_buildings, but save()/load() never moved it and the
// member had no initialiser. Every pending building order restored from a save
// therefore carried an uninitialised heap value, which Echo::update_building_orders
// then used as a map key for issue_order() and passed to AssignWorkers -- so an
// AI game resumed from a save was not reproducible run to run, and a resumed
// multiplayer game could desync without packet loss or a version mismatch.
//
// Version 96 serialises the field. Saves older than that do not carry it and
// load() leaves the member at -1, the sentinel Echo::load keys its
// fresh-registration fallback on (a real BuildingRegister key is required, so a
// sentinel must never reach issue_order).
//
// Exercised here:
//   1. version-96 round trip -> id preserved, every other field intact
//   2. an unregistered order (id still -1) survives the Uint32 on the wire as -1
//      rather than coming back as 4294967295 and being treated as a real key
//   3. pre-96 stream (no id on the wire) -> id left at the -1 sentinel, and
//      every following field still decodes from the right offset
//
// Links libgagserver.a for BinaryStream + MemoryStreamBackend.

#include <cstdio>
#include <memory>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Version.h"
#include "echo/Echo.h"

// SHA1 is wired in this project by direct .c-into-.cpp inclusion (see
// YOGServerPasswordRegistry.cpp); the .h has no extern "C" wrapper, so
// libgagserver.a's BinaryOutputStream references C++-mangled SHA1 names.
// Same trick as BulletSaveLoadTest.cpp to satisfy the linker.
#include "../gnupg/sha1.c"

using namespace GAGCore;
using AIEcho::Construction::BuildingOrder;

// BuildingOrder::save/load, the id member and the default constructor are all
// private and friended to this class (src/ai/echo/Construction.h), the same way
// GradientBFSTest reaches AIEcho::Gradients::Gradient.
class EchoBuildingOrderSaveLoadTest
{
public:
	static int failures;

	static void check(bool ok, const char* what)
	{
		std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
		if (!ok)
			++failures;
	}

	static std::unique_ptr<BinaryInputStream> makeInputStream(const MemoryStreamBackend& written)
	{
		// BinaryInputStream takes ownership of the backend.
		MemoryStreamBackend* copy = new MemoryStreamBackend(written);
		copy->seekFromStart(0);
		return std::make_unique<BinaryInputStream>(copy);
	}

	// A constraint-free, condition-free order: load() then never reaches the
	// Constraint/Condition factories, so the fixture needs no Player.
	static void testRoundTripCurrentVersion()
	{
		BuildingOrder original(5, 3);
		original.id = 4242;

		MemoryStreamBackend* backend = new MemoryStreamBackend;
		BinaryOutputStream ostream(backend);
		original.save(&ostream);

		auto istream = makeInputStream(*backend);
		BuildingOrder loaded;
		loaded.load(istream.get(), NULL, VERSION_MINOR);

		check(loaded.id == 4242, "roundTrip: id preserved");
		check(loaded.building_type == 5, "roundTrip: building_type preserved");
		check(loaded.number_of_workers == 3, "roundTrip: number_of_workers preserved");
		check(loaded.constraints.empty(), "roundTrip: constraint list decodes");
		check(loaded.conditions.empty(), "roundTrip: condition list decodes");
	}

	// An order constructed but never registered keeps id == -1. It goes onto the
	// wire as a Uint32, so this pins that it comes back as -1 (and therefore
	// takes the fallback) rather than as a huge positive key.
	static void testUnregisteredOrderRoundTrips()
	{
		BuildingOrder original(7, 1);

		MemoryStreamBackend* backend = new MemoryStreamBackend;
		BinaryOutputStream ostream(backend);
		original.save(&ostream);

		auto istream = makeInputStream(*backend);
		BuildingOrder loaded;
		loaded.load(istream.get(), NULL, VERSION_MINOR);

		check(loaded.id == -1, "unregistered: id round-trips as -1 through Uint32");
		check(loaded.building_type == 7, "unregistered: building_type preserved");
	}

	// Hand-write the version-95 wire layout: no id after number_of_workers.
	static void testPre96StreamLeavesSentinel()
	{
		MemoryStreamBackend* backend = new MemoryStreamBackend;
		BinaryOutputStream ostream(backend);
		ostream.writeEnterSection("BuildingOrder");
		ostream.writeUint32(5, "building_type");
		ostream.writeUint32(3, "number_of_workers");
		ostream.writeEnterSection("constraints");
		ostream.writeUint32(0, "size");
		ostream.writeLeaveSection();
		ostream.writeEnterSection("conditions");
		ostream.writeUint32(0, "size");
		ostream.writeLeaveSection();
		ostream.writeLeaveSection();

		auto istream = makeInputStream(*backend);
		BuildingOrder loaded;
		loaded.load(istream.get(), NULL, 95);

		check(loaded.id == -1, "pre96: id left at the -1 sentinel for Echo::load to replace");
		check(loaded.building_type == 5, "pre96: building_type still aligned");
		check(loaded.number_of_workers == 3, "pre96: number_of_workers still aligned");
		check(loaded.constraints.empty(), "pre96: constraint list still aligned");
		check(loaded.conditions.empty(), "pre96: condition list still aligned");
	}
};

int EchoBuildingOrderSaveLoadTest::failures = 0;

int main(int /*argc*/, char* /*argv*/[])
{
	EchoBuildingOrderSaveLoadTest::testRoundTripCurrentVersion();
	EchoBuildingOrderSaveLoadTest::testUnregisteredOrderRoundTrips();
	EchoBuildingOrderSaveLoadTest::testPre96StreamLeavesSentinel();
	const int failures = EchoBuildingOrderSaveLoadTest::failures;
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
