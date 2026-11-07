// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// Shared by Team and the lightweight entity GID helpers. Save-layout and
// generator limits remain separate from the live team/controller capacity.
inline constexpr int TEAM_MAX_COUNT = 16;

/// Bit for `team` in team-mask bitfields (allies, sharedVision*, Tile::forbidden, ...).
/// Masks are 32 bits wide; `1<<31` on a signed int would be UB.
inline constexpr std::uint32_t teamNumberToMask(int team) { return std::uint32_t(1) << team; }
