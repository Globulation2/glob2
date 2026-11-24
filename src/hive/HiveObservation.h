// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HiveWorker.h"
#include "scripting/javascript/ScriptObservations.h"
#include <memory>
#include <vector>
class Game;
namespace Hive
{
// Tile records from earlier captures. Most of the map does not change between runs:
// only visible tiles and tiles whose remembered state changed are rebuilt. Only the
// worker thread touches it, one capture at a time.
struct TileCache
{
	unsigned width = 0, height = 0;
	std::vector<Script::Observations::Cell> cells;
	std::vector<char> valid;
	Json tiles;
	Json statics; // buildingTypes, terrainTypes, resourceTypes
};
// What a capture needs from the simulation, taken at a boundary: the colony's
// observations brought up to date (fog-of-war memory), a captured world view and a
// copy of the memory. Cheap; everything else is read from it, on any thread.
struct CaptureInput
{
	Script::DetachedObservation observation;
	int team;
	unsigned tick;
};
CaptureInput prepareCapture(Script::Observations &observations, Game &game, int team);
// The colony's snapshot from a prepared capture: the only input to the isolated
// interpreter. With a cache, only changed tiles are rebuilt.
Json buildSnapshot(const CaptureInput &input, TileCache *cache = nullptr);
// Both at once, on the calling thread.
Json capture(Script::Observations &observations, Game &game, int team, TileCache *cache = nullptr);
} // namespace Hive
