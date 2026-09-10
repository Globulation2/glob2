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

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Order.h"
#include "OrderMessages.h"
#include "ReplayWriter.h"
#include "ReplayReader.h"
#include "Version.h"

using namespace GAGCore;

// --- Stubs ---------------------------------------------------------------
//
// ReplayWriter::init takes a GameGUI& only to write the game header, and
// ReplayReader::loadReplay(stream, true) constructs one only to skip that
// header. Neither path matters here (the tests use skipToOrders=false and an
// empty stub header), so provide link-level stand-ins without pulling in the
// real GameGUI.h include surface. The declaration only has to produce the
// same mangled names as the real out-of-line members.
class GameGUI
{
public:
	explicit GameGUI(bool persistPreferences = true);
	~GameGUI();
	bool load(GAGCore::InputStream *stream, bool ignoreGUIData=false);
	void save(GAGCore::OutputStream *stream, const std::string name);
};
GameGUI::GameGUI(bool) {}
GameGUI::~GameGUI() {}
bool GameGUI::load(GAGCore::InputStream*, bool) { return false; }
void GameGUI::save(GAGCore::OutputStream*, const std::string) {}

// Linking the real Order.cpp would drag in every OrderCreate / OrderDelete /
// OrderModify… deserialize symbol through the switch in Order::getOrder. The
// replay counter logic only cares about "some real order" vs "NullOrder", so
// stub the factory down to those two cases.
Order::Order(void)
{
	sender = ORDER_SENDER_NONE;
	gameCheckSum = ORDER_CHECKSUM_NONE;
}
MiscOrder::MiscOrder() : Order() {}
NullOrder::NullOrder() : MiscOrder() {}

// A payload-less stand-in for "a real player order" — anything whose type is
// not ORDER_NULL keeps the reader's order loop going.
class StepTestOrder : public MiscOrder
{
public:
	Uint8 *getData(void) { return NULL; }
	bool setData(const Uint8*, int, Uint32) { return true; }
	int getDataLength(void) { return 0; }
	Uint8 getOrderType(void) { return ORDER_DELETE; }
};

// Record the requested version because supported replay versions currently
// decode identically.
Uint32 lastDecodeVersionMinor = 0;

std::shared_ptr<Order> Order::getOrder(const Uint8 *netData, int netDataLength, Uint32 versionMinor)
{
	lastDecodeVersionMinor = versionMinor;
	if (netDataLength < 1 || netData == NULL)
		return std::shared_ptr<Order>();
	if (netData[0] == ORDER_NULL)
		return std::shared_ptr<Order>(new NullOrder());
	if (netData[0] == ORDER_DELETE)
		return std::shared_ptr<Order>(new StepTestOrder());
	return std::shared_ptr<Order>();
}

// NetMessage::operator!= anchors the NetMessage vtable so OrderMessages.cpp
// links without NetMessage.cpp (whose getNetMessage switch would drag in
// every NetXxx subclass).
bool NetMessage::operator!=(const NetMessage& rhs) const
{
	return !(*this == rhs);
}

// SHA1 is wired in this project by direct .c-into-.cpp inclusion (see
// YOGServerPasswordRegistry.cpp); the .h has no extern "C" wrapper, so
// libgag_server.a's BinaryOutputStream::write references C++-mangled
// SHA1Update/Init/Final names. Mirror the same trick here.
#include "../gnupg/sha1.c"

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		++failures;
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
	const char* tmpdir = std::getenv("TMPDIR");
	std::string path = std::string(tmpdir ? tmpdir : "/tmp") + "/replay-stepcounter-test.replay";

	{
		GameGUI stubGui;
		ReplayWriter writer;
		writer.init(path, stubGui);
		check(writer.isValid(), "roundTrip: writer initialised on file backend");

		for (Uint32 i = 0; i < QUIET_STEPS; i++)
			writer.advanceStep();
		writer.pushOrder(std::shared_ptr<Order>(new StepTestOrder()));
		writer.finish();
		// ~ReplayWriter appends a second terminator; the reader stops at the
		// first NullOrder, so it is harmless.
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
BinaryInputStream* writeReplayBody(Uint16 versionMinor, Uint32 firstCounter)
{
	MemoryStreamBackend* writeBackend = new MemoryStreamBackend;
	MemoryStreamBackend* readBackend = nullptr;
	{
		BinaryOutputStream ostream(writeBackend);
		ostream.writeUint16(VERSION_MAJOR, "versionMajor");
		ostream.writeUint16(versionMinor, "versionMinor");
		ostream.writeUint32(firstCounter, "replayStepsSinceLastOrder");
		writeOrderEnvelope(&ostream, std::shared_ptr<Order>(new StepTestOrder()));
		ostream.writeUint32(0, "replayStepsSinceLastOrder");
		writeOrderEnvelope(&ostream, std::shared_ptr<Order>(new NullOrder()));
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

int main(int /*argc*/, char* /*argv*/[])
{
	testWideRoundTrip();
	testVersionBounds();
	testDecodeVersionPlumbing();
	std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
	return failures == 0 ? 0 : 1;
}
