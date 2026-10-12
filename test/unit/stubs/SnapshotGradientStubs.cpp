// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "gradient/SnapshotGradient.h"

// Unit Map fixtures do not link SnapshotGradient.cpp or create owned snapshot
// cost views. Supply only their diagnostic getter; real cost-view admission,
// recovery and accounting remain covered by the engine binary.
namespace gradient_preparation
{
MinimalCostViewMetrics minimalCostViewMetrics() noexcept { return {}; }
}
