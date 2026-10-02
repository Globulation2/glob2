// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Linking the real Order.cpp / OrderMisc.cpp / NetMessage.cpp would drag in every
// OrderCreate / OrderDelete / NetXxx deserialise symbol through their factory
// switches. The unit tests only need "some real order" versus NullOrder, so the
// base constructors and the factory are stubbed down to that.

#include "OrderStubs.h"
#include "NetMessage.h"
#include "OrderMessages.h"

#include <memory>

Order::Order(void)
{
	sender = ORDER_SENDER_NONE;
	gameCheckSum = ORDER_CHECKSUM_NONE;
}
MiscOrder::MiscOrder() : Order() {}
NullOrder::NullOrder() : MiscOrder() {}

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
	// Anything else: "couldn't decode", so decodeData throws.
	return std::shared_ptr<Order>();
}
