// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "BuildingUtils.h"
#include "UnitUtils.h"
#include <iterator>

namespace AIEngine
{
using SimulationSnapshot::BuildingView;
using SimulationSnapshot::UnitView;
using SimulationSnapshot::TeamView;
using SimulationSnapshot::BuildingKindView;
using SimulationSnapshot::TileView;
using SimulationSnapshot::RuleView;
using SimulationSnapshot::BuildProjectView;

// Borrowed slot traversal over the engine's captured index. Empty legacy slots
// remain visible, with no allocated pointer arrays or copied entity state.
template<class Record> class EntitySlots
{
	std::span<const Record> records;
	std::span<const Uint32> indices;
public:
	EntitySlots() = default;
	EntitySlots(std::span<const Record> records, std::span<const Uint32> indices)
		: records(records), indices(indices) {}
	std::size_t size() const { return indices.size(); }
	const Record* operator[](std::size_t slot) const
	{ const auto index = indices[slot]; return index == SimulationSnapshot::Entities::NoRecord ? nullptr : &records[index]; }
	class Iterator
	{
		const Record* records = nullptr;
		const Uint32* indices = nullptr;
		std::size_t position = 0;
	public:
		using value_type = const Record*;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;
		Iterator() = default;
		Iterator(const Record* records, const Uint32* indices, std::size_t position)
			: records(records), indices(indices), position(position) {}
		const Record* operator*() const
		{ const auto index = indices[position]; return index == SimulationSnapshot::Entities::NoRecord ? nullptr : &records[index]; }
		Iterator& operator++() { ++position; return *this; }
		Iterator operator++(int) { auto previous = *this; ++*this; return previous; }
		bool operator==(const Iterator&) const = default;
	};
	Iterator begin() const { return {records.data(), indices.data(), 0}; }
	Iterator end() const { return {records.data(), indices.data(), indices.size()}; }
};

// Controller observations adapt an engine lease. Borrowed spans remain valid
// for this observation's lifetime; controllers release it after each invocation.
class AIWorldView
{
	SimulationSnapshot::Handle lease;
	const decltype(SimulationSnapshot::Catalogs::unitTypes)* observedUnitTypes = nullptr;
	const TerrainType* terrainCells = nullptr;
	const Uint16* legacyTerrainCells = nullptr;
	const SimulationSnapshot::ResourceCell* resourceCells = nullptr;
	const SimulationSnapshot::OccupancyCell* occupancyCells = nullptr;
	const SimulationSnapshot::AreaCell* areaCells = nullptr;
	const Uint32* discoveredCells = nullptr;
	const Uint32* visibleCells = nullptr;
	int xMask = -1, yMask = -1;
	unsigned widthShift = 0;
	bool maskedGeometry = false, farmInputs = false;
	std::size_t cellCount = 0;
	TileView composeTile(std::size_t index) const;
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
	const AIPlanning::BuildingCapabilityTables& capabilities() const { return *lease.catalogs->capabilities; }
	Uint32 tick = 0;
	const int width, height;
	int totalPrestige = 0;
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
	const UnitType& unitType(int type, int level) const { return (*observedUnitTypes)[type][level]; }
	std::span<const TeamView> teams;
	std::span<const BuildingView> buildings;
	std::span<const UnitView> units;
	std::span<const BuildProjectView> buildProjects;
	class Tiles
	{
		const AIWorldView* world;
	public:
		explicit Tiles(const AIWorldView& value) : world(&value) {}
		std::size_t size() const { return world->cellCount; }
		TileView operator[](std::size_t index) const { return world->composeTile(index); }
		TileView at(std::size_t index) const
		{ if (index >= size()) throw std::out_of_range("snapshot tile index"); return world->composeTile(index); }
	} tiles;
	// Shared stock queries require a captured Teams component. Local stock is
	// part of the building's authoritative scalar record.
	std::span<const Sint32, MAX_NB_RESOURCES> buildingResources(const BuildingView& building) const
	{
		if (building.usesTeamResources) return teams[building.team].resources;
		return building.localResource;
	}
	std::span<const UnitRef> workers(const BuildingView& building) const
	{ return std::span<const UnitRef>(lease.entities->relationships).subspan(building.working.offset, building.working.count); }
	std::span<const UnitRef> occupants(const BuildingView& building) const
	{ return std::span<const UnitRef>(lease.entities->relationships).subspan(building.inside.offset, building.inside.count); }
	const BuildingView* building(BuildingRef identity) const;
	const UnitView* unit(UnitRef identity) const;
	const BuildingView* buildingAtSlot(Uint16 gid) const;
	const UnitView* unitAtSlot(Uint16 gid) const;
	EntitySlots<BuildingView> buildingSlots(int team) const
	{
		if (!lease.entities || team < 0 || std::size_t(team) >= lease.entities->buildingSlotIndices.size() / BuildingUtils::MAX_COUNT) return {};
		return {buildings, std::span<const Uint32>(lease.entities->buildingSlotIndices).subspan(std::size_t(team) * BuildingUtils::MAX_COUNT, BuildingUtils::MAX_COUNT)};
	}
	EntitySlots<UnitView> unitSlots(int team) const
	{
		if (!lease.entities || team < 0 || std::size_t(team) >= lease.entities->unitSlotIndices.size() / UnitUtils::MAX_COUNT) return {};
		return {units, std::span<const Uint32>(lease.entities->unitSlotIndices).subspan(std::size_t(team) * UnitUtils::MAX_COUNT, UnitUtils::MAX_COUNT)};
	}
	TileView tile(int x, int y) const;
	std::size_t tileIndex(int x, int y) const
	{
		if (maskedGeometry) return ((std::size_t(unsigned(y) & unsigned(yMask))) << widthShift)
			+ (unsigned(x) & unsigned(xMask));
		return std::size_t(normalizeY(y)) * width + normalizeX(x);
	}
	// Binding validates component sizes once. Scalar reads require the named
	// component and an index produced by this observation's geometry.
	SimulationSnapshot::TerrainCell terrainAt(std::size_t index) const
	{ return {terrainCells[index], legacyTerrainCells[index]}; }
	const SimulationSnapshot::ResourceCell& resourceAt(std::size_t index) const { return resourceCells[index]; }
	const SimulationSnapshot::OccupancyCell& occupancyAt(std::size_t index) const { return occupancyCells[index]; }
	const SimulationSnapshot::AreaCell& areasAt(std::size_t index) const { return areaCells[index]; }
	SimulationSnapshot::VisibilityCell visibilityAt(std::size_t index) const { return {discoveredCells[index], visibleCells[index]}; }
	bool canPaintFarmAt(std::size_t index) const;
	int normalizeX(int x) const { return xMask >= 0 ? unsigned(x) & unsigned(xMask) : wrapGeneral(x, width); }
	int normalizeY(int y) const { return yMask >= 0 ? unsigned(y) & unsigned(yMask) : wrapGeneral(y, height); }
	bool isUpgradeAvailable(const BuildingView& building) const;
	bool isHardSpaceForBuildingSite(const BuildingView& building, bool upgrade) const;
	int distanceSquared(int x1, int y1, int x2, int y2) const;
private:
	static int wrapGeneral(int coordinate, int size)
	{
		if (unsigned(coordinate) < unsigned(size)) return coordinate;
		const int value = coordinate % size;
		return value < 0 ? value + size : value;
	}
};
} // namespace AIEngine
