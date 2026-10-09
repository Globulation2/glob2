// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "WorldRecords.h"
#include "ObservationRecords.h"
#include "MapState.h"
#include "AreaEffects.h"
#include "Requirements.h"
#include "GameHeader.h"
#include "FertilityField.h"
#include "UnitType.h"
#include "BuildingCapabilities.h"
#include "ResourceRegistry.h"
#include "CellRules.h"
#include "MapAssetBundle.h"
#include "MapStateView.h"
#include "ResourcePlaneKey.h"
#include "MapChangeTracking.h"
#include <span>
#include <stdexcept>
#include <limits>

namespace SimulationSnapshot
{

struct Catalogs
{
	std::string buildingFingerprint;
	std::shared_ptr<const std::vector<BuildingKindView>> buildings;
	std::shared_ptr<const std::vector<BuildingType>> typeDefinitions;
	std::shared_ptr<const AIPlanning::BuildingCapabilityTables> capabilities;
	std::array<std::array<UnitType, NB_UNIT_LEVELS>, NB_UNIT_TYPE> unitTypes;
	// Immutable resource catalog, shared with the map. Habitats are compiled
	// into the terrain's cell rules.
	std::shared_ptr<const ResourceRegistry> resources;
    std::shared_ptr<const MapAssetBundle> assets;
};
// Which live chunks a pooled buffer mirrors (MapState::ChangeTracker stamps),
// so the next fill of the same buffer copies only chunks changed since.
struct ChunkStamps
{
	Uint64 worldIdentity = 0;
	int width = 0, height = 0;
	Uint32 filledTick = 0;
	std::vector<Uint64> chunks;
};
struct Annotations { std::vector<Uint16> scriptAreas; std::vector<std::string> areaNames; ChunkStamps stamps; };
struct Terrain
{
	std::shared_ptr<const TerrainRegistry> registry;
	// The rules every captured cell rule indexes; later captures may share it.
	std::shared_ptr<const CellRuleTable> rules;
	std::shared_ptr<const std::vector<TerrainType>> vertices;
	std::vector<Uint16> cellRules;
	Uint64 revision = 0;
	bool movementModifiers = false, airConstraints = false;
	ChunkStamps stamps;
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
	ChunkStamps stamps; // cells and stockIndices
};
using OccupancyCell = MapState::OccupancyCell;
struct Occupancy { std::vector<OccupancyCell> cells; ChunkStamps stamps; };
using AreaCell = MapState::AreaCell;
struct Areas { std::vector<AreaCell> cells; bool farmEnabled = false; ChunkStamps stamps; };
struct VisibilityCell { Uint32 discovered = 0, visible = 0; };
struct Visibility { std::vector<Uint32> discovered, visible; ChunkStamps stamps; };
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
// One captured plane, shared with earlier captures while its generation holds.
struct ResourceField
{
	Uint16 key = 0;
	Uint64 generation = 0;
	std::shared_ptr<const std::vector<Uint16>> values;
};
// The live planes at capture, in the map's publication order, with a dense
// key index so consumers look a plane up without hashing.
struct ResourceFields
{
	std::vector<ResourceField> planes;
	std::array<Uint16, MapState::PlaneCount> index{};
	const ResourceField* find(Uint16 key) const { const auto i = index[key]; return i ? &planes[i - 1] : nullptr; }
	void clear() { for (const auto& plane : planes) index[plane.key] = 0; planes.clear(); }
	void add(ResourceField field) { planes.push_back(std::move(field)); index[planes.back().key] = Uint16(planes.size()); }
};


// A handle owns only components explicitly leased to this consumer. Projection
// does not keep an umbrella snapshot alive through an incidental parent pointer.
struct Handle
{
	Uint32 tick = 0;
	Uint64 observationRevision = 0;
	int width = 0, height = 0;
	Requirements requirements = 0;
	std::shared_ptr<const Session> session;
	std::shared_ptr<const Effects> effects;
	std::shared_ptr<const Statistics> statistics;
	std::shared_ptr<const History> history;
	std::shared_ptr<const Telemetry> telemetry;
	std::shared_ptr<const EntityDiagnostics> entityDiagnostics;
	std::shared_ptr<const Annotations> annotations;
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
	std::shared_ptr<const BuildingAreaEffects::FertilitySnapshot> areaFertility;
	Uint64 worldIdentity = 0, configurationRevision = 0;
	std::array<Uint64, 5> mapGenerations{};
	Handle project(Requirements requested) const;
	// Scalar readers touch only their requested immutable component. Defaults
	// match a combined tile whose corresponding component was not captured.
	Uint16 cellRuleAt(std::size_t index) const
	{ checkTileIndex(index); return terrain ? terrain->cellRules.at(index) : Uint16(GRASS); }
	const TerrainProperties& terrainPropertiesAt(std::size_t index) const
	{ checkTileIndex(index); return terrain ? (*terrain->rules)[terrain->cellRules.at(index)].properties : terrainProperties(GRASS); }
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
//! Throw if a captured map array or entity list differs from the live game.
void verifyCapture(const Game& game, const Handle& handle);
} // namespace SimulationSnapshot
