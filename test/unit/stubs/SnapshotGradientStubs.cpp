// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "gradient/SnapshotGradient.h"
#include "Game.h"

// Unit Map fixtures have no game-owned statistics pipeline. Its real barrier
// is exercised by the engine's statistics and save/load suites.
void Game::drainTeamStats() {}
void Game::drainHiring() {}
// Unit map fixtures have no owner observation; engine tests cover invalidation.
void Game::invalidateObservation() {}

// Unit Map fixtures do not link SnapshotGradient.cpp or create owned snapshot
// cost views. Supply only their diagnostic getter; real cost-view admission,
// recovery and accounting remain covered by the engine binary.
namespace gradient_preparation
{
MinimalCostViewMetrics minimalCostViewMetrics() noexcept { return {}; }
}
