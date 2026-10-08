// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace MapSaveLayout
{
// The released artwork formats and the earlier growth prototypes used 144/145.
// A supported map has 256 or more cells, with a power-of-two first block size.
// An artwork length followed by serialized JSON cannot match these headers:
// raw lengths end in zero, constants require byte 1, and delta starts above the
// maximum artwork length. Payload validation remains the packed reader's job.
constexpr bool legacyGrowthMapHeader(std::uint8_t tag, std::uint32_t bytes, std::size_t cells)
{
	return (tag == 0 && bytes == std::min(cells, std::size_t(4096))) ||
		(tag == 1 && bytes == 1) || tag == 2;
}
}
