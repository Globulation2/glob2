// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TeamStat.h"
#include <span>

// Routine simulation coverage. Diagnostic ledgers stay in TeamStats::checkSum()
// for full verification; the measurement values the win-probability model reads
// (WinProbability::teamInputs) drive a winning condition and are covered here.
Uint32 teamStatsCoreChecksum(const TeamStats&, std::span<const TeamSmoothedStat>,
                             int statsIndex, int smoothedIndex);
