// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "SDL_stdinc.h"

/// Identifies a player connected through YOG. Player IDs are Uint16 on the
/// YOG wire protocol; BasePlayer stores the value in a wider saved-game field.
using YOGPlayerID = Uint16;
