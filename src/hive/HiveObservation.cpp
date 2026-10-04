// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiveObservation.h"
#include "scripting/javascript/ScriptObservations.h"
#include "Game.h"
namespace Hive
{
Json capture(Script::Observations &observations, Game &game, int team)
{
	if (team < 0 || team >= game.mapHeader.getNumberOfTeams())
		throw std::runtime_error("Invalid player team");
	observations.observe();
	Json snapshot = {{"team", team},
					 {"tick", game.stepCounter},
					 {"width", game.map.getW()},
					 {"height", game.map.getH()}};
	for (const char *name : {"teams", "units", "buildings", "buildingTypes"})
		snapshot[name] = json(observations.query(name, {}));
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
