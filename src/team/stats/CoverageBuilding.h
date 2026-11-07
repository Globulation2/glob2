// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A building footprint the growth-coverage statistics count, apart from TeamStat.h so
// the map can store them without including team statistics (TeamStats::CoverageBuilding).
struct CoverageBuilding { int x, y, width, height; bool operator==(const CoverageBuilding &) const = default; };

// Wire-locked growth-distance bands: keep order and count stable for old saves.
inline constexpr int GROWTH_COVERAGE_RADII[] = {8, 16, 32};
inline constexpr int GROWTH_COVERAGE_BANDS = sizeof(GROWTH_COVERAGE_RADII) / sizeof(GROWTH_COVERAGE_RADII[0]);
