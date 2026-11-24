// SPDX-License-Identifier: GPL-3.0-or-later
#include <nlohmann/json.hpp>
#include "Map.h"
#include "HiveObservation.h"
#include "scripting/javascript/ScriptObservations.h"
#include "Game.h"
namespace Hive
{
Json capture(Script::Observations &observations, Game &game, int team, TileCache *cache)
{
	if (team < 0 || team >= game.mapHeader.getNumberOfTeams())
		throw std::runtime_error("Invalid player team");
	// All queries in this invocation share one immutable world. Without this
	// scope, each tile query captures the entire world again (quadratic work).
	auto observation = observations.captureObservation();
	observations.observe();
	Json snapshot = {{"team", team},
					 {"tick", game.stepCounter},
					 {"width", game.map.getW()},
					 {"height", game.map.getH()}};
	// terrainTypes and resourceTypes are the match's static, public rule tables: they
	// say what a tile's corners and resource type mean, never what is on the map.
	for (const char *name : {"teams", "units", "buildings"})
		snapshot[name] = json(observations.query(name, {}));
	// The match's rule tables never change: with a cache they are built once and handed to
	// the worker thread with the tiles (buildingTypes alone is about 170 KB of JSON).
	if (cache && cache->statics)
		;
	else
	{
		Json statics = Json::object();
		for (const char *name : {"buildingTypes", "terrainTypes", "resourceTypes"})
			statics[name] = json(observations.query(name, {}));
		if (cache)
			cache->statics = std::make_shared<const Json>(std::move(statics));
		else
			for (auto &[name, table] : statics.items())
				snapshot[name] = std::move(table);
	}
	if (cache)
	{
		const unsigned width = game.map.getW(), height = game.map.getH();
		if (cache->width != width || cache->height != height || !cache->tiles)
		{
			cache->width = width, cache->height = height;
			cache->cells.assign(std::size_t(width) * height, {});
			cache->valid.assign(std::size_t(width) * height, 0);
			cache->tiles = std::make_shared<Json>(Json::array());
			for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
				cache->tiles->push_back(nullptr);
		}
		// The previous worker has finished before a new capture; copy only if it has not.
		if (cache->tiles.use_count() > 1)
			cache->tiles = std::make_shared<Json>(*cache->tiles);
		auto &tiles = *cache->tiles;
		for (unsigned y = 0; y < height; y++)
			for (unsigned x = 0; x < width; x++)
			{
				const std::size_t i = std::size_t(y) * width + x;
				const auto cell = observations.cell(int(x), int(y));
				if (cache->valid[i] && !cell.visible && cell == cache->cells[i])
					continue;
				tiles[i] = json(observations.query("tile", {Script::Value(x), Script::Value(y)}));
				cache->cells[i] = cell;
				cache->valid[i] = 1;
			}
		return snapshot;
	}
	auto tiles = Json::array();
	std::size_t bytes = 0;
	for (unsigned y = 0; y < game.map.getH(); y++)
		for (unsigned x = 0; x < game.map.getW(); x++)
		{
			auto tile = json(observations.query("tile", {Script::Value(x), Script::Value(y)}));
			bytes += tile.dump().size();
			if (bytes > 24 * 1024 * 1024)
				throw std::runtime_error("Colony observations exceed worker capacity");
			tiles.push_back(std::move(tile));
		}
	snapshot["tiles"] = std::move(tiles);
	return snapshot;
}
} // namespace Hive
