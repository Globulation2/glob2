// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
class Game;
struct GenerationRequest;
struct GenerationResult;

/// Wall time spent in each stage of a map report, in seconds, for a caller measuring what its
/// measurements cost. Stages are timed around the analysis only, not the JSON they become,
/// which has its own entry.
struct MapReportTimings
{
	double tileScan = 0;        ///< terrain, resources, buildings and both fertility fields
	double resourcePatches = 0; ///< connected patches of each of the eight resource types
	double space = 0;           ///< buildable tiles, 4x4 anchors, land and water regions
	double startQuality = 0;    ///< scoreStarts and the fitted fairness model
	double walking = 0, swimming = 0, clearing = 0; ///< per-colony movement analysis, per mode
	double serialise = 0;       ///< building and printing the JSON
};

// Snapshot analysis only: no simulation steps, and all scorer cache writes are restored.
std::string describeMap(Game &game, const GenerationRequest *request = nullptr,
						const GenerationResult *generation = nullptr,
						MapReportTimings *timings = nullptr);

// Partial attempt diagnostics only; never analyze an invalid or partially built world.
std::string describeGenerationFailure(const GenerationRequest &, const GenerationResult &);
