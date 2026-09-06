// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

/// Identifies a player connected through YOG. Player IDs are 16-bit on the
/// YOG wire protocol; BasePlayer stores the value in a wider saved-game field.
using YOGPlayerID = std::uint16_t;
