// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors
//
// AIToubibTuning.h
//
// Behavior-preserving tuning constants for AIToubib, extracted from
// AIToubib.cpp during the magic-number cleanup pass that prepares the
// codebase for the Rust port. AIToubib is a stub AI: per `AI::getOrder()` it
// alternates each tick between a no-op build step and a no-op stats step.
// Only one live constant comes out of that file -- everything else
// (MAX_NB_PROJECTS, NB_HISTORY_STATES) is dead, lives inside `/* */` blocks
// in AIToubib.h, and is intentionally NOT named here.
//
// Constants are file-scope `static constexpr int` per the slice convention.

#pragma once

// ---------------------------------------------------------------------------
// Decision-cycle modulus (AIToubib::getOrder):
//   switch (now % AI_TOUBIB_STEP_MODULUS) {
//     case 0:  return getOrderBuildingStep();    // no-op: NullOrder
//     default: computeMyStatsStep(); return NullOrder; // no-op
//   }
// Alternates between the two stub steps every tick.
// ---------------------------------------------------------------------------
static constexpr int AI_TOUBIB_STEP_MODULUS = 2;
