// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Ressource.h"

// A harvested packet is one raw unit. Building stock is denominated by that
// building's multiplier; partially filled supplier packets retain their fraction.
struct MaterialPacket
{
	Uint32 numerator = 1, denominator = 1;
	bool operator==(const MaterialPacket&) const = default;
};
struct MaterialDeliveryResult
{
	Sint32 acceptedStock = 0;
	Uint64 discardedNumerator = 0, discardedDenominator = 1;
};
