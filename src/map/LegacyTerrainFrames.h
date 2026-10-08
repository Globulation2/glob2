// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainType.h"
#include <array>
#include <cstdint>
#include <optional>

// Classic sprite frames, as files before format 146 stored them per cell: the
// frame range each grass/sand/water corner pattern drew, indexed
// (2-tl)*27 + (2-tr)*9 + (2-bl)*3 + (2-br). Patterns with grass against water
// never had art and drew grass. Only loaders of old data read these.
inline constexpr std::uint16_t LEGACY_CLASSIC_FRAMES[81][2] =
{
	{ 0, 16 }, { 80, 8 }, { 0, 16 }, { 88, 8 }, { 48, 8 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 },
	{ 104, 8 }, { 64, 8 }, { 0, 16 }, { 120, 8 }, { 32, 8 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 },
	{ 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 },
	{ 96, 8 }, { 112, 8 }, { 0, 16 }, { 72, 8 }, { 40, 8 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 },
	{ 56, 8 }, { 24, 8 }, { 0, 16 }, { 16, 8 }, { 128, 16 }, { 208, 8 }, { 0, 16 }, { 216, 8 }, { 176, 8 },
	{ 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 232, 8 }, { 192, 8 }, { 0, 16 }, { 240, 8 }, { 160, 8 },
	{ 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 },
	{ 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 224, 8 }, { 248, 8 }, { 0, 16 }, { 200, 8 }, { 168, 8 },
	{ 0, 16 }, { 0, 16 }, { 0, 16 }, { 0, 16 }, { 184, 8 }, { 152, 8 }, { 0, 16 }, { 144, 8 }, { 256, 16 },
};

// The corners (top-left, top-right, bottom-left, bottom-right) a classic frame
// drew, or nothing for frames outside the classic ranges (272 and up).
constexpr std::optional<std::array<TerrainType, 4>> legacyFrameCorners(std::uint16_t frame)
{
	// Uniform ranges first: they also stand for the artless grass/water patterns.
	if (frame < 16) return std::array{GRASS, GRASS, GRASS, GRASS};
	if (frame >= 128 && frame < 144) return std::array{SAND, SAND, SAND, SAND};
	if (frame >= 256 && frame < 272) return std::array{WATER, WATER, WATER, WATER};
	for (unsigned pattern = 0; pattern < 81; ++pattern)
	{
		const auto &range = LEGACY_CLASSIC_FRAMES[pattern];
		if (frame >= range[0] && frame < range[0] + range[1])
			return std::array{TerrainType(2 - pattern / 27), TerrainType(2 - pattern / 9 % 3),
							  TerrainType(2 - pattern / 3 % 3), TerrainType(2 - pattern % 3)};
	}
	return std::nullopt;
}
static_assert(legacyFrameCorners(16) == std::array{SAND, SAND, SAND, GRASS});
static_assert(legacyFrameCorners(259) == std::array{WATER, WATER, WATER, WATER});
