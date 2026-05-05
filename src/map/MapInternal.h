// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Private shared definitions for the Map.cpp family of translation units.
// Not intended for inclusion outside Map*.cpp.

#pragma once

#include <algorithm>
#include <vector>

#define UPDATE_MAX(max,value) { if (value>(max)) (max)=value; }

// use deltaOne for first perpendicular direction
extern const int deltaOne[8][2];
// use tabClose for original circular direction
extern const int tabClose[8][2];
// use tabMiniFar for all miniGrad far points
extern const int tabFar[16][2];

// helper to fill vectors
template <typename T>
inline void fill(std::vector<T>& vec, const T& value) {
	std::fill(vec.begin(), vec.end(), value);
}

// Helper for updateLocalGradient and the local-gradient pathfinders.
inline int clip_0_31(int x) { return (x < 0) ? 0 : (x > 31) ? 31 : x; }

