// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TeamStat.h"
#include <span>

// Routine simulation coverage. Diagnostic ledgers stay in TeamStats::checkSum()
// for full verification; they do not drive simulation decisions.
Uint32 teamStatsCoreChecksum(const TeamStats&, std::span<const TeamSmoothedStat>,
                             int statsIndex, int smoothedIndex);
