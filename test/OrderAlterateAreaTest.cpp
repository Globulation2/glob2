// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression harness for BH-195: OrderAlterateArea::setData must reject
// malformed packets where the header-declared bitmap area doesn't match the
// payload length, or where the dimensions would overflow / exceed the brush
// side cap. Pre-fix tree: TC1, TC2 pass; TC3-TC7 fail (or crash in release).
// Post-fix tree: all TCs pass and the binary exits 0.

#include <cstdio>
#include <SDL.h>
#include "Order.h"
#include "Marshaling.h"

// Minimal stub for the Order base-class constructor. Linking the real
// Order.cpp would drag in OrderCreate/OrderDelete/MessageOrder/etc.
// deserialize symbols (via Order::getOrder's switch) which we don't need.
Order::Order(void)
{
	sender = ORDER_SENDER_NONE;
	gameCheckSum = ORDER_CHECKSUM_NONE;
}

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const char* tc, const char* what)
{
	if (cond)
	{
		++g_passed;
		std::printf("  PASS  %s — %s\n", tc, what);
	}
	else
	{
		++g_failed;
		std::printf("  FAIL  %s — %s\n", tc, what);
	}
}

void writeHeader(Uint8* buf, Sint16 minX, Sint16 minY, Sint16 maxX, Sint16 maxY)
{
	addUint8(buf, 0, 0);                                   // teamNumber
	addUint8(buf, 0, 1);                                   // type
	addSint16(buf, 0, 2);                                  // centerX
	addSint16(buf, 0, 4);                                  // centerY
	addSint16(buf, minX, 6);
	addSint16(buf, minY, 8);
	addUint16(buf, static_cast<Uint16>(maxX), 10);
	addUint16(buf, static_cast<Uint16>(maxY), 12);
}

// TC1 — well-formed 10x10 brush is accepted and round-trips.
void tc1_happyPath()
{
	// sideX=sideY=10, bits=100, bytes = bitToByte(100) = ceil(100/8) = 13
	Uint8 buf[ALTERATE_AREA_HEADER_BYTES + 13] = {0};
	writeHeader(buf, 45, 45, 55, 55);

	OrderAlterateForbidden order;
	bool ok = order.setData(buf, sizeof(buf), 0);
	check(ok, "TC1", "well-formed 10x10 brush accepted");
	if (ok)
	{
		check(order.maxX - order.minX == 10, "TC1", "sideX round-trips");
		check(order.maxY - order.minY == 10, "TC1", "sideY round-trips");
	}
}

// TC2 — dataLength below header size is rejected.
void tc2_undersizedHeader()
{
	Uint8 buf[ALTERATE_AREA_HEADER_BYTES - 1] = {0};
	OrderAlterateForbidden order;
	bool ok = order.setData(buf, sizeof(buf), 0);
	check(!ok, "TC2", "dataLength<14 rejected");
}

// TC3 — maxX < minX (negative side) is rejected.
void tc3_negativeSide()
{
	Uint8 buf[ALTERATE_AREA_HEADER_BYTES] = {0};
	writeHeader(buf, 60, 45, 55, 55);   // sideX = -5
	OrderAlterateForbidden order;
	bool ok = order.setData(buf, sizeof(buf), 0);
	check(!ok, "TC3", "negative side rejected");
}

// TC4 — side exceeding ORDER_AREA_BRUSH_MAX_SIDE is rejected.
void tc4_sideOverCap()
{
	// sideX=1000, sideY=10 → bits=10000, bytes = bitToByte(10000) = 1250.
	// Allocate a buffer large enough that, if the fix is missing in release,
	// the OOB read inside BitArray::deserialize doesn't crash the harness
	// before we can record the FAIL.
	constexpr int payloadLen = 1250;
	Uint8 buf[ALTERATE_AREA_HEADER_BYTES + payloadLen] = {0};
	writeHeader(buf, 0, 0, 1000, 10);
	OrderAlterateForbidden order;
	bool ok = order.setData(buf, sizeof(buf), 0);
	check(!ok, "TC4", "side > ORDER_AREA_BRUSH_MAX_SIDE rejected");
}

// TC5 — Sint16-extreme header (sideX=sideY=65535) is rejected before the
// signed-int multiply that would otherwise be UB.
void tc5_overflowAttack()
{
	Uint8 buf[ALTERATE_AREA_HEADER_BYTES] = {0};
	writeHeader(buf, -32768, -32768, 32767, 32767);
	OrderAlterateForbidden order;
	bool ok = order.setData(buf, sizeof(buf), 0);
	check(!ok, "TC5", "overflow-attack header rejected without UB");
}

// TC6 — 10x10 header claims 13 bitmap bytes but only 5 are provided.
// Pre-fix: BitArray::deserialize std::copies past the end of the buffer.
void tc6_payloadTooShort()
{
	Uint8 buf[ALTERATE_AREA_HEADER_BYTES + 5] = {0};
	writeHeader(buf, 0, 0, 10, 10);
	OrderAlterateForbidden order;
	bool ok = order.setData(buf, sizeof(buf), 0);
	check(!ok, "TC6", "payload-too-short rejected (was OOB read)");
}

// TC7 — 2x2 header claims 1 bitmap byte but 100 are provided.
void tc7_payloadTooLong()
{
	Uint8 buf[ALTERATE_AREA_HEADER_BYTES + 100] = {0};
	writeHeader(buf, 0, 0, 2, 2);
	OrderAlterateForbidden order;
	bool ok = order.setData(buf, sizeof(buf), 0);
	check(!ok, "TC7", "payload-too-long rejected");
}

} // namespace

int main()
{
	std::printf("OrderAlterateAreaTest — BH-195 regression\n");
	tc1_happyPath();
	tc2_undersizedHeader();
	tc3_negativeSide();
	tc4_sideOverCap();
	tc5_overflowAttack();
	tc6_payloadTooShort();
	tc7_payloadTooLong();
	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
