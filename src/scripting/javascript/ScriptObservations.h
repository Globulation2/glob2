// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include "TerrainType.h"
#include "Version.h"
#include <array>
#include <memory>
class Game;
class TerrainRegistry;
class Unit;
class Building;
namespace GAGCore
{
class InputStream;
class OutputStream;
} // namespace GAGCore
namespace Script
{
class Observations
{
	struct RememberedTile
	{
		unsigned tick = 0;
		unsigned short terrain = 0, fertility = 0;
		TerrainType terrainType = GRASS;
		unsigned char type = 255, variety = 0, amount = 0;
		bool known = false;
	};
	Game &game;
	mutable std::shared_ptr<const TerrainRegistry> terrainDefinitionRegistry;
	mutable Value terrainDefinitions;
	int team;
	unsigned profile = 1;
	// Lazily allocated indexed chunks avoid tree lookups without allocating an
	// entire map per controller before it has explored any terrain.
	using Chunk = std::array<RememberedTile, 256>;
	std::vector<std::unique_ptr<Chunk>> remembered;
	unsigned knownTiles = 0;
	const RememberedTile *lookup(unsigned index) const;
	RememberedTile &remember(unsigned index);
	unsigned lastTick = 0xffffffffu;
	Value ref(const Unit *unit) const;
	Value ref(const Building *building) const;
	Value unit(const Unit &unit) const;
	Value building(const Building &building) const;
	Value tile(int x, int y) const;

  public:
	struct Cell
	{
		unsigned tick = 0;
		unsigned short terrain = 0, fertility = 0;
		TerrainType terrainType = GRASS;
		unsigned char resource = 255, amount = 0;
		bool known = false, visible = false, forbidden = false, building = false;
		bool operator==(const Cell &) const = default;
	};
	Cell cell(int x, int y) const;
	struct SpatialEntity
	{
		int team, type, x, y, hp, attack;
		bool isVirtual;
		int buildingType = -1;
	};
	// The native spatial view shares the script visibility predicate, without
	// allocating complete JavaScript records for every unit in a density query.
	void visitSpatialEntities(bool units, int filter,
							  const std::function<void(const SpatialEntity &)> &visit,
							  const QueryBudget &budget) const;
	void setProfile(unsigned value) { profile = value; }
	Observations(Game &game, int team) : game(game), team(team) {}
	void observe();
	Value query(const std::string &name, const std::vector<Value> &args,
				const QueryBudget &budget = {}) const;
	void save(GAGCore::OutputStream *stream) const;
	void load(GAGCore::InputStream *stream, int version = VERSION_MINOR);
};
} // namespace Script
