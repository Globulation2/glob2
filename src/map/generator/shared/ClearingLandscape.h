// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "Geometry.h"
#include "Grid.h"
#include <string>
#include <vector>
namespace MapGeneration
{
// Round, sand-contained homes separated by dry resource ground and optional distant lakes.
struct ClearingLandscapeOptions
{
	int homeSize, homePools, lakes, lakeSize;
	const char *telemetryPrefix = "old-growth";
	bool repairCrowdedLattice = false; // opt-in vacancy layout; Old Growth keeps its historic sites
};
struct ClearingLandscape
{
	Torus t{1, 1};
	double homeRadius = 0, spacing = 0;
	std::vector<ShapePoint> homes, kits;
	std::vector<int> homeOf;
	std::vector<unsigned char> water, clearing, sand, forest, lake;
	std::vector<std::vector<ShapePoint>> pools; // each home's ring of pools
	std::vector<int> lakeCentres;
	std::string failure;
};

ClearingLandscape clearingLandscape(const GenerationRequest &, GenerationContext &,
									const ClearingLandscapeOptions &);
} // namespace MapGeneration
