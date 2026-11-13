// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Diagnostic switch for building seed templates (BuildingSeedTemplates.h),
// kept apart so the seed kernels in SeedCells.h need not see the owner-side
// maintenance class.

namespace building_seed
{
//! GLOB2_BUILDING_SEED_AUDIT (set by test/run_tests.py; "0" or empty is off):
//! every template-backed seed is also computed by the full scan, and any
//! difference throws. Safe to read from worker threads.
bool auditEnabled();
void setAuditEnabled(bool enabled);
}
