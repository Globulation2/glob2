// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// FixedPoint.h
//
// Fixed-point arithmetic shifts shared across the simulation. The C++
// engine carries deterministic numeric state in plain integer types and
// uses Q16.16 (and occasionally Q24.8) shifts to interpret them as
// fractional. These constants name the shift / mask / "one" so that
// expressions like `(value << 16)` or `value & 65535` become
// `value << FIXED_POINT_SHIFT_16` etc. Sites: Construction.cpp,
// TypeSteps.cpp, Update.cpp, Minimap.cpp, Step.cpp, TeamRouting.cpp,
// UnitMovement.cpp.
//
// NOTE FOR PORTERS: The Rust port replaces this with the `I16F16` type
// from the `fixed` crate (see docs/rust/determinism.md); raw shifts stay
// only on the C++ side.

#pragma once

// === Q16.16 fixed-point ===

//! Bit-shift used to encode a Q16.16 fixed-point value: the integer part
//! occupies the high 16 bits, the fractional part the low 16 bits.
static constexpr int FIXED_POINT_SHIFT_16 = 16;

//! Mask for the fractional 16 bits of a Q16.16 value, equal to
//! (1u << FIXED_POINT_SHIFT_16) - 1.
static constexpr unsigned int FIXED_POINT_FRAC_MASK = 65535u;

//! The Q16.16 representation of 1.0, equal to 1u << FIXED_POINT_SHIFT_16.
static constexpr unsigned int FIXED_POINT_ONE = 65536u;

// === Q24.8 fixed-point ===

//! Bit-shift used by per-tick scaling that does not need 16-bit fractional
//! precision (building shootSpeed, attack quality, upgrade-score scaling).
//! Sites: Step.cpp:200, TypeSteps.cpp:357, 358, 365, 366, TeamRouting.cpp:227,
//! UnitMovement.cpp:250, 270.
static constexpr int Q8_FIXED_POINT_SHIFT = 8;

