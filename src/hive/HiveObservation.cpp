// SPDX-License-Identifier: GPL-3.0-or-later
#include <nlohmann/json.hpp>
#include "Map.h"
#include "HiveObservation.h"
#include "scripting/javascript/ScriptObservations.h"
#include "Game.h"
namespace Hive
{
CaptureInput prepareCapture(Script::Observations &observations, Game &game, int team)
{
	if (team < 0 || team >= game.mapHeader.getNumberOfTeams())
		throw std::runtime_error("Invalid player team");
	return {observations.observeAndDetach(), team, game.stepCounter};
}
Json buildSnapshot(const CaptureInput &input, TileCache *cache)
{
	// All queries read one immutable world view and the copied memory.
	const auto observations = input.observation.reader();
	const unsigned width = unsigned(observations.world().width), height = unsigned(observations.world().height);
	Json snapshot = {{"team", input.team}, {"tick", input.tick}, {"width", width}, {"height", height}};
	for (const char *name : {"teams", "units", "buildings"})
		snapshot[name] = json(observations.query(name, {}));
	// terrainTypes and resourceTypes are the match's static, public rule tables: they
	// say what a tile's corners and resource type mean, never what is on the map. They
	// never change, so with a cache they are built once (buildingTypes alone is about
	// 170 KB of JSON).
	auto statics = [&]
	{
		Json tables = Json::object();
		for (const char *name : {"buildingTypes", "terrainTypes", "resourceTypes"})
			tables[name] = json(observations.query(name, {}));
		return tables;
	};
	if (!cache)
	{
		auto tables = statics();
		for (auto &[name, table] : tables.items())
			snapshot[name] = std::move(table);
		auto tiles = Json::array();
		std::size_t bytes = 0;
		for (unsigned y = 0; y < height; y++)
			for (unsigned x = 0; x < width; x++)
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
	if (cache->statics.is_null())
		cache->statics = statics();
	for (const auto &[name, table] : cache->statics.items())
		snapshot[name] = table;
	if (cache->width != width || cache->height != height || !cache->tiles.is_array())
	{
		cache->width = width, cache->height = height;
		cache->cells.assign(std::size_t(width) * height, {});
		cache->valid.assign(std::size_t(width) * height, 0);
		cache->tiles = Json::array();
		for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
			cache->tiles.push_back(nullptr);
	}
	auto &tiles = cache->tiles;
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
	snapshot["tiles"] = tiles;
	return snapshot;
}
Json capture(Script::Observations &observations, Game &game, int team, TileCache *cache)
{
	return buildSnapshot(prepareCapture(observations, game, team), cache);
}
} // namespace Hive
