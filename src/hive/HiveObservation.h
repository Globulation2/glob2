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
// only visible tiles and tiles whose remembered state changed are rebuilt.
struct TileCache
{
	unsigned width = 0, height = 0;
	std::vector<Script::Observations::Cell> cells;
	std::vector<char> valid;
	std::shared_ptr<Json> tiles; // shared with the worker thread that sends it
	std::shared_ptr<const Json> statics; // buildingTypes, terrainTypes, resourceTypes
};
// Called at a simulation boundary. The only input to the isolated interpreter. With a
// cache, tiles are refreshed there and left out of the snapshot: the worker thread adds
// cache->tiles, so the main thread neither rebuilds nor copies the whole map.
Json capture(Script::Observations &observations, Game &game, int team, TileCache *cache = nullptr);
} // namespace Hive
