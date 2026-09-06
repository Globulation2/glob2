// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

///Script numbers travel as a single Uint8 on the wire (encodeData writes
///Uint8), so the storable domain is [0..Max].
namespace ScriptNumber
{
	constexpr int Max = 255;

	///Clamps to [0..Max] so the in-memory value always matches what a
	///save/load round-trip yields.
	inline int clampToWireDomain(int scriptNumber)
	{
		return std::clamp(scriptNumber, 0, Max);
	}
}
