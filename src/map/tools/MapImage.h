// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
class Game;
struct GenerationRequest;
struct MapImageImportReport
{
	int markers = 0, ignoredMarkers = 0, terrainChanges = 0;
	int seamWidth = 0, seamTerrainChanges = 0, seamShoreChanges = 0;
	int seamResourceChanges = 0;
	bool resourceSeamFallback = false;
	int clearedResources = 0, droppedResources = 0;
	std::string json() const;
};
void exportMapImage(const Game &, const std::string &path);
// Map images are a lossy categorical interchange format for new maps. Import
// classifies image pixels to the fixed terrain/resource/marker palette,
// resamples to the requested map dimensions, then builds a fresh game from the
// decoded cells. It does not preserve arbitrary saved-game state. On failure the
// candidate must be discarded. expectedTeams == 0 accepts the marker count found
// in the image; otherwise it requires an exact marker count match before
// modifying the request team count.
void importMapImage(Game &, const std::string &path, GenerationRequest &, int expectedTeams,
					MapImageImportReport &, int seamWidth = -1);
