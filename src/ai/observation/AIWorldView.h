// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"

namespace AIEngine
{
using SimulationSnapshot::BuildingView;
using SimulationSnapshot::UnitView;
using SimulationSnapshot::TeamView;
using SimulationSnapshot::BuildingKindView;
using SimulationSnapshot::TileView;
using SimulationSnapshot::RuleView;
using SimulationSnapshot::BuildProjectView;

// Controller observations adapt an engine lease. Borrowed spans remain valid
// for this observation's lifetime; controllers release it after each invocation.
class AIWorldView
{
	SimulationSnapshot::Handle lease;
public:
	using Catalog = std::vector<BuildingKindView>;
	explicit AIWorldView(SimulationSnapshot::Handle captured);
	AIWorldView(const AIWorldView&) = delete;
	AIWorldView& operator=(const AIWorldView&) = delete;
	AIWorldView(AIWorldView&&) = delete;
	AIWorldView& operator=(AIWorldView&&) = delete;
	static std::shared_ptr<const Catalog> captureCatalog(const Game& game);
	static std::shared_ptr<const AIWorldView> capture(const Game& game, std::shared_ptr<const Catalog> catalog);
	std::span<const Uint16> resourceGradient(int team, int resource, int swim, bool market = false) const
	{
		if (!lease.resourceFields) return {};
		const auto found = lease.resourceFields->values.find({team, resource, swim, market});
		return found == lease.resourceFields->values.end() ? std::span<const Uint16>{} : std::span<const Uint16>(*found->second.values);
	}
	const SimulationSnapshot::Handle& components() const { return lease; }
	Uint32 tick = 0;
	int width = 0, height = 0, totalPrestige = 0;
	Uint64 terrainRevision = 0;
	bool terrainMovementModifiers = false, airTerrainConstraints = false;
	RuleView rules;
	std::span<const std::pair<std::string, int>> ruleValues;
	std::span<const std::string> experimentKeys;
	bool farmAreasEnabled = false;
	std::array<bool, MAX_NB_RESOURCES> resourceShrinkable{}, resourceVisibleToBeCollected{}, resourceEternal{};
	std::array<int, MAX_NB_RESOURCES> resourceSizesCount{};
	std::shared_ptr<const TerrainRegistry> terrain;
	std::shared_ptr<const Catalog> catalog;
	std::shared_ptr<const GameHeader> configuration;
	std::shared_ptr<const Fertility::GrowthCache> growth;
	const UnitType& unitType(int type, int level) const { return lease.catalogs->unitTypes.at(type).at(level); }
	std::span<const TeamView> teams;
	std::span<const BuildingView> buildings;
	std::span<const UnitView> units;
	std::span<const BuildProjectView> buildProjects;
	class Tiles
	{
		const SimulationSnapshot::Handle* lease;
	public:
		explicit Tiles(const SimulationSnapshot::Handle& value) : lease(&value) {}
		std::size_t size() const { return std::size_t(lease->width) * lease->height; }
		TileView operator[](std::size_t index) const { return lease->tileAt(index); }
		TileView at(std::size_t index) const { return lease->tileAt(index); }
	} tiles;
	std::span<const UnitRef> workers(const BuildingView& building) const
	{ return std::span<const UnitRef>(lease.entities->relationships).subspan(building.working.offset, building.working.count); }
	std::span<const UnitRef> occupants(const BuildingView& building) const
	{ return std::span<const UnitRef>(lease.entities->relationships).subspan(building.inside.offset, building.inside.count); }
	const BuildingView* building(BuildingRef identity) const;
	const UnitView* unit(UnitRef identity) const;
	const BuildingView* buildingAtSlot(Uint16 gid) const;
	const UnitView* unitAtSlot(Uint16 gid) const;
	TileView tile(int x, int y) const;
	int normalizeX(int x) const;
	int normalizeY(int y) const;
	int distanceSquared(int x1, int y1, int x2, int y2) const;
};
} // namespace AIEngine
