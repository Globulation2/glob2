// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression harness for BH-207: NetSendOrder::decodeData must reject a
// corrupt-or-attacker-supplied envelope whose `size` field overflows a sane
// cap, by throwing std::ios_base::failure *before* allocating any buffer.
// Pre-fix: the raw `new Uint8[size]` either escapes std::bad_alloc (which
// callers — loadReplay, retrieveOrder, getNetMessage — catch only as
// ios_base::failure and therefore miss) or wastes a multi-GB allocation
// before the generic "Couldn't decode" throw fires. Post-fix: an explicit
// size check rejects the envelope with a distinct ios_base::failure message
// citing the size cap. Both test cases are independent of which Order
// subclasses are linked — Order::getOrder and the Order/MiscOrder/NullOrder
// ctors are stubbed so only OrderMessages.cpp's behaviour is under test.

#include "Glob2Test.h"
#include "unit/stubs/OrderStubs.h"

#include <cstdio>
#include <ios>
#include <memory>
#include <new>
#include <string>
#include <SDL.h>
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "NetMessage.h"
#include "Order.h"
#include "OrderMessages.h"

using namespace GAGCore;


namespace {

void check(bool cond, const char* tc, const char* what)
{
	CHECK_MESSAGE(cond, (std::string(tc) + ": " + what));
}

// Build a BinaryInputStream containing one NetSendOrder envelope:
//   Uint32 size | size bytes payload | Uint8 sender | Uint32 checksum
// Mirrors the testSerialize() copy-via-fresh-backend pattern in
// NetTestSuite.cpp so we don't have to chase ownership of the writer's
// backend after BinaryOutputStream's dtor runs. Caller owns the returned
// stream and is responsible for `delete`.
BinaryInputStream* makeStream(Uint32 declaredSize,
                              const Uint8* payload,
                              size_t payloadSize,
                              Uint8 sender,
                              Uint32 checksum)
{
	MemoryStreamBackend* writeBackend = new MemoryStreamBackend;
	MemoryStreamBackend* readBackend = nullptr;
	{
		BinaryOutputStream out(writeBackend);
		out.writeEnterSection("NetSendOrder");
		out.writeUint32(declaredSize, "size");
		if (payloadSize > 0)
			out.write(payload, payloadSize, "data");
		out.writeUint8(sender, "sender");
		out.writeUint32(checksum, "checksum");
		out.writeLeaveSection();

		const size_t totalLen = writeBackend->getPosition();
		readBackend = new MemoryStreamBackend(writeBackend->getBuffer(), totalLen);
	}
	// `out` is destructed here, freeing writeBackend. readBackend owns its own copy.
	readBackend->seekFromStart(0);
	return new BinaryInputStream(readBackend);
}

// TC1 — Oversized `size` field must produce ios_base::failure (not bad_alloc,
// not any other exception type), thrown by the bound-check before allocation.
// Distinguishing pre-fix from post-fix: the post-fix message cites the cap;
// the pre-fix "Couldn't decode data stream to an Order" message does not.
void tc1_rejectsOversizedSize()
{
	// 1 MiB + 1 — just over the documented cap; small enough that pre-fix's
	// `new Uint8[size]` won't OOM the test runner, large enough to be
	// unambiguously rejected post-fix.
	const Uint32 oversized = (1u << 20) + 1u;
	BinaryInputStream* stream = makeStream(oversized, nullptr, 0, 0, 0);

	enum class Outcome { NoThrow, IosFailure, BadAlloc, Other };
	Outcome outcome = Outcome::NoThrow;
	std::string what;

	try
	{
		NetSendOrder msg;
		msg.decodeData(stream);
	}
	catch (const std::ios_base::failure& e)
	{
		outcome = Outcome::IosFailure;
		what = e.what();
	}
	catch (const std::bad_alloc&)
	{
		outcome = Outcome::BadAlloc;
	}
	catch (...)
	{
		outcome = Outcome::Other;
	}

	delete stream;

	check(outcome != Outcome::BadAlloc, "TC1",
	      "no std::bad_alloc escapes decodeData (pre-fix would on a 64-bit host without overcommit)");
	check(outcome != Outcome::Other, "TC1",
	      "no unexpected exception type escapes");
	check(outcome == Outcome::IosFailure, "TC1",
	      "ios_base::failure thrown on oversized size");
	check(what.find("NetSendOrder size") != std::string::npos, "TC1",
	      "rejection message cites the size cap (proves bound-check fired, not the downstream 'bad format' throw)");
}

// TC2 — Well-formed minimal envelope (ORDER_NULL payload, size=1) round-trips.
// Regression coverage that the new bound-check doesn't over-reject valid input.
void tc2_acceptsValidNullOrder()
{
	const Uint8 payload[1] = { ORDER_NULL };
	const Uint8 senderIn = 7;
	const Uint32 checksumIn = 0x12345678u;
	BinaryInputStream* stream = makeStream(1, payload, 1, senderIn, checksumIn);

	bool decoded = false;
	Uint8 orderType = 0xFF;
	Uint8 senderOut = 0;
	Uint32 checksumOut = 0;
	std::string err;

	try
	{
		NetSendOrder msg;
		msg.decodeData(stream);
		auto order = msg.getOrder();
		if (order)
		{
			decoded = true;
			orderType = order->getOrderType();
			senderOut = order->sender;
			checksumOut = order->gameCheckSum;
		}
	}
	catch (const std::exception& e) { err = e.what(); }
	catch (...) { err = "non-std exception"; }

	delete stream;

	check(decoded, "TC2", err.empty()
	      ? "valid envelope decoded without exception"
	      : ("expected no throw but got: " + err).c_str());
	check(orderType == ORDER_NULL, "TC2", "decoded order is NullOrder");
	check(senderOut == senderIn, "TC2", "sender round-trips");
	check(checksumOut == checksumIn, "TC2", "checksum round-trips");
}

} // namespace

TEST_SUITE("NetSendOrderDecode")
{
	TEST_CASE("rejects oversized size before allocating") { tc1_rejectsOversizedSize(); }
	TEST_CASE("accepts a valid NullOrder envelope") { tc2_acceptsValidNullOrder(); }
}
