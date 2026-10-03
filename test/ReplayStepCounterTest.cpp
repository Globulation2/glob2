// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression harness for the replay inter-order step counter. ReplayWriter
// used to hold stepsSinceLastOrder in a Uint16 and move it over the wire with
// writeUint16, so after 65535 order-less ticks (~44 minutes at 25 Hz — routine
// in sparse AI runs and the 90000-tick trainer games) the counter silently
// wrapped, corrupting the tick-to-order alignment on read-back. Post-fix the
// counter is Uint32 on both sides. (Replays from before that fix are below
// REPLAY_MINIMUM_VERSION_MINOR and no longer load at all.)
//
// Covered here:
//   1. writer/reader round-trip with >65535 quiet steps between orders — the
//      read-back step delta is exact (would have wrapped to delta % 65536)
//   2. version floor/ceiling: replays older than REPLAY_MINIMUM_VERSION_MINOR
//      or newer than the running build are rejected
//   3. the replay header version, not VERSION_MINOR, is what orders are
//      decoded against
//
// GameGUI and the Order hierarchy are stubbed (same trick as
// NetSendOrderDecodeTest.cpp) so only ReplayWriter/ReplayReader plus
// OrderMessages.cpp are under test; libgag_server.a provides BinaryStream and
// the stream backends.

#include "Glob2Test.h"
#include <algorithm>
#include "unit/stubs/OrderStubs.h"
#include "unit/stubs/GameGUIStubs.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <SDL3/SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Order.h"
#include "OrderMessages.h"
#include "ReplayWriter.h"
#include "ReplayReader.h"
#include "Version.h"
#include "FileFormatVersions.h"

using namespace GAGCore;


namespace {

void check(bool ok, const char* what)
{
	CHECK_MESSAGE(ok, (what));
}

// Number of order-less steps between init and the first order in the
// round-trip test. Must exceed 65535 so a Uint16 counter would wrap
// (70000 % 65536 == 4464).
const Uint32 QUIET_STEPS = 70000;

// Write one NetSendOrder envelope, exactly like ReplayWriter's writeOrder.
void writeOrderEnvelope(OutputStream* stream, std::shared_ptr<Order> order)
{
	NetSendOrder msg(order);
	msg.encodeData(stream);
}

// 1. Writer/reader round-trip with a quiet stretch a Uint16 cannot hold.
void testWideRoundTrip()
{
	// The writer buffers to a file when given a path; a leading '/' bypasses
	// FileManager (which is not initialised in this harness).
	std::string path = (glob2test::profileDir() / "replay-stepcounter-test.replay").string();

	{
		GameGUI stubGui;
		ReplayWriter writer;
		writer.init(path, stubGui);
		check(writer.isValid(), "roundTrip: writer initialised on file backend");

		for (Uint32 i = 0; i < QUIET_STEPS; i++)
			writer.advanceStep();
		writer.pushOrder(std::shared_ptr<Order>(new StepTestOrder()));
		writer.finish();
		// finish is idempotent, including destruction after an explicit finish.
	}

	FILE* fp = std::fopen(path.c_str(), "r");
	check(fp != NULL, "roundTrip: replay file written");
	if (fp == NULL)
		return;

	ReplayReader reader;
	bool loaded = reader.loadReplay(new BinaryInputStream(new FileStreamBackend(fp)), false);
	check(loaded, "roundTrip: replay loads (version accepted)");
	if (!loaded)
		return;

	// Under the old Uint16 counter this would read back as 70000 % 65536 = 4464.
	check(reader.getNumStepsTotal() == QUIET_STEPS, "roundTrip: step total survives >65535 quiet steps");

	// Step through: no order may fire at the Uint16-wrapped position...
	for (Uint32 i = 0; i < QUIET_STEPS % 65536; i++)
		reader.advanceStep();
	check(!reader.hasMoreOrdersThisStep(), "roundTrip: no order at the Uint16-wrapped step");

	// ...and the order must fire exactly at the true position.
	for (Uint32 i = QUIET_STEPS % 65536; i < QUIET_STEPS; i++)
		reader.advanceStep();
	check(reader.hasMoreOrdersThisStep(), "roundTrip: order due exactly at the true step");

	std::shared_ptr<Order> order = reader.retrieveOrder();
	check(order && order->getOrderType() == ORDER_DELETE, "roundTrip: order read back");

	check(reader.hasMoreOrdersThisStep(), "roundTrip: terminator due immediately after last order");
	std::shared_ptr<Order> terminator = reader.retrieveOrder();
	check(terminator && terminator->getOrderType() == ORDER_NULL, "roundTrip: NullOrder terminator");
	check(reader.isFinished(), "roundTrip: reader finished");

	std::remove(path.c_str());
}

// Hand-write a replay body (no game header) at the given version, with the
// Uint32 step counters. Returns an input stream over a
// copy of the written bytes (BinaryOutputStream deletes its backend on
// destruction, so the copy is taken while it is still alive); the caller
// passes ownership to ReplayReader::loadReplay.
BinaryInputStream* writeReplayBody(Uint16 versionMinor, Uint32 firstCounter, Uint32 finalCounter = 0, Uint32 checksum = 0)
{
	MemoryStreamBackend* writeBackend = new MemoryStreamBackend;
	MemoryStreamBackend* readBackend = nullptr;
	{
		BinaryOutputStream ostream(writeBackend);
		ostream.writeUint16(VERSION_MAJOR, "versionMajor");
		ostream.writeUint16(versionMinor, "versionMinor");
		ostream.writeUint32(firstCounter, "replayStepsSinceLastOrder");
		auto order = std::make_shared<StepTestOrder>();
		order->gameCheckSum = checksum;
		writeOrderEnvelope(&ostream, order);
		ostream.writeUint32(finalCounter, "replayStepsSinceLastOrder");
		writeOrderEnvelope(&ostream, std::shared_ptr<Order>(new NullOrder()));
		if (versionMinor >= FILE_FORMAT_VERSION_CUSTOM_AI)
			ReplayTelemetry::Stream().write(&ostream);
		readBackend = new MemoryStreamBackend(*writeBackend);
	}
	// ostream's destructor freed writeBackend; readBackend owns its own copy.
	readBackend->seekFromStart(0);
	return new BinaryInputStream(readBackend);
}

// 2. Version floor and ceiling.
void testVersionBounds()
{
	{
		ReplayReader reader;
		check(!reader.loadReplay(writeReplayBody(REPLAY_MINIMUM_VERSION_MINOR - 1, 1), false),
		      "versionBounds: replay older than the supported floor is rejected");
	}
	{
		ReplayReader reader;
		check(!reader.loadReplay(writeReplayBody(VERSION_MINOR + 1, 1), false),
		      "versionBounds: replay newer than this build is rejected");
	}
	{
		ReplayReader reader;
		check(reader.loadReplay(writeReplayBody(VERSION_MINOR, 1), false),
		      "versionBounds: current-version replay accepted");
	}
}

// 3. Both the initial scan and playback use the replay header version.
void testDecodeVersionPlumbing()
{
	// The oldest version the reader accepts. When the floor equals the current
	// build (as right after a floor bump) this still checks that the header
	// value is what reaches the decoder.
	const Uint16 oldVersion = REPLAY_MINIMUM_VERSION_MINOR;

	ReplayReader reader;
	lastDecodeVersionMinor = 0;
	bool loaded = reader.loadReplay(writeReplayBody(oldVersion, 7), false);
	check(loaded, "decodeVersion: oldest supported replay loads");
	if (!loaded)
		return;

	check(lastDecodeVersionMinor == oldVersion,
	      "decodeVersion: initial scan uses the replay header version");

	// Reset the recorder to check playback independently of the initial scan.
	lastDecodeVersionMinor = 0;

	for (Uint32 i = 0; i < 7; i++)
		reader.advanceStep();
	std::shared_ptr<Order> order = reader.retrieveOrder();
	check(order && order->getOrderType() == ORDER_DELETE, "decodeVersion: order read back");
	check(lastDecodeVersionMinor == oldVersion,
	      "decodeVersion: order decoded against the replay's version, not VERSION_MINOR");
}

}  // namespace

TEST_SUITE("ReplayStepCounter")
{
    TEST_CASE("truncated replay bodies never become zero-filled orders") {
        std::unique_ptr<BinaryInputStream> original(writeReplayBody(VERSION_MINOR, 7));
        original->seekFromEnd(0);
        const size_t length = original->getPosition();
        original->seekFromStart(0);
        std::string bytes(length, '\0');
        original->read(bytes.data(), length, "body");
        for (size_t cut = 0; cut < length; ++cut) {
            INFO(cut);
            auto* memory = new MemoryStreamBackend(bytes.data(), cut);
            memory->seekFromStart(0);
            ReplayReader reader;
            CHECK_FALSE(reader.loadReplay(new BinaryInputStream(memory), false));
            CHECK_FALSE(reader.isValid());
        }
    }

	TEST_CASE("step totals cannot wrap") {
        ReplayReader reader;
        CHECK_FALSE(reader.loadReplay(writeReplayBody(VERSION_MINOR, 0xffffffffu, 1), false));
    }
	TEST_CASE("checksum mismatch closes playback safely") {
        ReplayReader reader;
        REQUIRE(reader.loadReplay(writeReplayBody(VERSION_MINOR, 0, 0, 123), false));
        reader.setCheckSum(456);
        CHECK(reader.retrieveOrder()->getOrderType() == ORDER_NULL);
        CHECK_FALSE(reader.isValid());
    }
    TEST_CASE("legacy create decoder respects both historical payload lengths") {
        const Uint8 shortPayload[20] = {};
        auto shortOrder = OrderCreate::deserialize(shortPayload, sizeof(shortPayload), FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS - 1);
        REQUIRE(shortOrder);
        CHECK(shortOrder->unitWorkingFuture == shortOrder->unitWorking);
        Uint8 oldPayload[24] = {}; oldPayload[23] = 7;
        auto oldOrder = OrderCreate::deserialize(oldPayload, sizeof(oldPayload), FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS - 1);
        REQUIRE(oldOrder);
        CHECK(oldOrder->unitWorkingFuture == 7);
        CHECK_FALSE(OrderCreate::deserialize(shortPayload, sizeof(shortPayload), VERSION_MINOR));
    }

	TEST_CASE("WideRoundTrip") { testWideRoundTrip(); }
	TEST_CASE("VersionBounds") { testVersionBounds(); }
	TEST_CASE("DecodeVersionPlumbing") { testDecodeVersionPlumbing(); }
}
