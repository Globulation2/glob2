// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldRecords.h"
#include "MapState.h"
#include "Requirements.h"
#include "GameHeader.h"
#include "FertilityField.h"
#include "UnitType.h"
#include "BuildingCapabilities.h"
#include "ResourceRegistry.h"
#include "ResourceHabitats.h"
#include "MapStateView.h"
#include <span>
#include <map>
#include <tuple>
#include <stdexcept>
#include <limits>

namespace SimulationSnapshot
{

struct Catalogs
{
	std::shared_ptr<const std::vector<BuildingKindView>> buildings;
	std::shared_ptr<const AIPlanning::BuildingCapabilityTables> capabilities;
	std::array<std::array<UnitType, NB_UNIT_LEVELS>, NB_UNIT_TYPE> unitTypes;
	// Immutable resource catalog and compiled habitat permissions, shared with the map.
	std::shared_ptr<const ResourceRegistry> resources;
	std::shared_ptr<const ResourceHabitats> habitats;
};
struct TerrainCell { TerrainType type = GRASS; Uint16 legacy = 0; };
struct Terrain
{
	std::shared_ptr<const TerrainRegistry> registry;
	std::shared_ptr<const std::vector<TerrainType>> identity;
	std::vector<Uint16> legacy;
	Uint64 revision = 0;
	bool movementModifiers = false, airConstraints = false;
};
using ResourceCell = MapState::ResourceCell;
// Single-yield stock lives inline in each cell; multi-yield deposits index the
// stock sidecar. Both are covered by the one Resources generation.
struct Resources
{
	std::vector<ResourceCell> cells;
	std::vector<Uint32> stockIndices;
	std::vector<std::array<Uint16, MaterialCount>> stocks;
	std::array<Uint32, MaterialCount> materialSourceCounts{};
	Uint64 staticMaterialSourceGeneration = 0;
};
using OccupancyCell = MapState::OccupancyCell;
struct Occupancy { std::vector<OccupancyCell> cells; };
using AreaCell = MapState::AreaCell;
struct Areas { std::vector<AreaCell> cells; bool farmEnabled = false; };
struct VisibilityCell { Uint32 discovered = 0, visible = 0; };
struct Visibility { std::vector<Uint32> discovered, visible; };
static_assert(std::is_trivially_copyable_v<ResourceCell>);
static_assert(std::is_trivially_copyable_v<OccupancyCell>);
static_assert(std::is_trivially_copyable_v<AreaCell>);
static_assert(std::is_trivially_copyable_v<VisibilityCell>);
struct Entities
{
	static constexpr Uint32 NoRecord = std::numeric_limits<Uint32>::max();
	std::vector<BuildingView> buildings;
	std::vector<UnitView> units;
	// Global entity slots index the compact records directly. Captured once in
	// the entity extraction pass and shared by all readers; no controller table.
	std::vector<Uint32> buildingSlotIndices, unitSlotIndices;
	std::vector<UnitRef> relationships;
	std::vector<BuildProjectView> projects;
};
struct Teams { std::vector<TeamView> values; int totalPrestige = 0; };
struct Rules
{
	RuleView values;
	std::shared_ptr<const GameHeader> configuration;
	std::vector<std::pair<std::string, int>> named;
	std::vector<std::string> experiments;
};
using ResourceFieldKey = std::tuple<int, int, int, bool>;
struct ResourceField
{
	Uint64 generation = 0;
	std::shared_ptr<const std::vector<Uint16>> values;
};
struct ResourceFields { std::map<ResourceFieldKey, ResourceField> values; };


// A handle owns only components explicitly leased to this consumer. Projection
// does not keep an umbrella snapshot alive through an incidental parent pointer.
struct Handle
{
	Uint32 tick = 0;
	int width = 0, height = 0;
	Requirements requirements = 0;
	std::shared_ptr<const Catalogs> catalogs;
	std::shared_ptr<const Terrain> terrain;
	std::shared_ptr<const Resources> resources;
	std::shared_ptr<const Occupancy> occupancy;
	std::shared_ptr<const Areas> areas;
	std::shared_ptr<const Visibility> visibility;
	std::shared_ptr<const Entities> entities;
	std::shared_ptr<const Teams> teams;
	std::shared_ptr<const Rules> rules;
	std::shared_ptr<const ResourceFields> resourceFields;
	std::shared_ptr<const Fertility::GrowthCache> growth;
	Uint64 worldIdentity = 0, configurationRevision = 0;
	std::array<Uint64, 5> mapGenerations{};
	Handle project(Requirements requested) const;
	// Scalar readers touch only their requested immutable component. Defaults
	// match a combined tile whose corresponding component was not captured.
	TerrainCell terrainAt(std::size_t index) const
	{ checkTileIndex(index); return terrain ? TerrainCell{terrain->identity->at(index), terrain->legacy.at(index)} : TerrainCell{}; }
	ResourceCell resourceAt(std::size_t index) const
	{ checkTileIndex(index); return resources ? resources->cells.at(index) : ResourceCell{}; }
	OccupancyCell occupancyAt(std::size_t index) const
	{ checkTileIndex(index); return occupancy ? occupancy->cells.at(index) : OccupancyCell{}; }
	AreaCell areasAt(std::size_t index) const
	{ checkTileIndex(index); return areas ? areas->cells.at(index) : AreaCell{}; }
	VisibilityCell visibilityAt(std::size_t index) const
	{ checkTileIndex(index); return visibility ? VisibilityCell{visibility->discovered.at(index), visibility->visible.at(index)} : VisibilityCell{}; }
	// Borrowed view over the captured components; the same MapState queries
	// the live Map uses read these arrays directly. Missing components are empty.
	MapState::View view() const;
	bool canPaintFarmAt(std::size_t index) const;
	TileView tileAt(std::size_t index) const;
private:
	void checkTileIndex(std::size_t index) const
	{ if (index >= std::size_t(width) * height) throw std::out_of_range("snapshot tile index"); }
};

std::shared_ptr<const std::vector<BuildingKindView>> captureCatalog(const Game& game);
struct Storage;
Handle capture(const Game& game, std::shared_ptr<const std::vector<BuildingKindView>> catalog,
	Requirements requirements = All, const Handle* previous = nullptr, Storage* storage = nullptr);
} // namespace SimulationSnapshot
