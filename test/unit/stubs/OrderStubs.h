// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Order.h"

// A payload-less "real player order": anything whose type is not ORDER_NULL keeps the
// replay reader's order loop going. The stubbed Order::getOrder decodes ORDER_DELETE
// into one of these, so a round trip through the wire format is exact.
class StepTestOrder : public MiscOrder
{
public:
	Uint8 *getData(void) { return NULL; }
	bool setData(const Uint8*, int, Uint32) { return true; }
	int getDataLength(void) { return 0; }
	Uint8 getOrderType(void) { return ORDER_DELETE; }
};

// The versionMinor most recently passed to the stubbed Order::getOrder.
extern Uint32 lastDecodeVersionMinor;
