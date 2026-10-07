#include "ResourceRegistry.h"
#include <map>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include "ScriptQueryStorage.h"
#include "TerrainType.h"
#include "Version.h"
#include "ai/observation/AIWorldView.h"
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
		unsigned short type = 65535;
		unsigned char variety = 0;
		unsigned amount = 0;
		bool known = false;
	};
	Game &game; // Scenario-owner fallback only; bound AI queries never access it.
	mutable const AIEngine::AIWorldView* view = nullptr;
	mutable std::shared_ptr<const TerrainRegistry> terrainDefinitionRegistry;
	mutable Value terrainDefinitions;
	mutable std::shared_ptr<const ResourceRegistry> terrainResourceDefinitionRegistry;
	mutable std::shared_ptr<const ResourceRegistry> resourceDefinitionRegistry;
	mutable Value resourceDefinitions;
	int team;
	unsigned profile = 1;
	// Lazily allocated indexed chunks avoid tree lookups without allocating an
	// entire map per controller before it has explored any terrain.
	using Chunk = std::array<RememberedTile, 256>;
	std::vector<std::unique_ptr<Chunk>> remembered;
	std::map<unsigned, std::array<Uint16, MaterialCount>> rememberedStocks;
	unsigned knownTiles = 0;
	const RememberedTile *lookup(unsigned index) const;
	RememberedTile &remember(unsigned index);
	unsigned lastTick = 0xffffffffu;
	Value ref(const AIEngine::UnitView *unit) const;
	Value ref(const AIEngine::BuildingView *building) const;
	Value unit(const AIEngine::UnitView &unit) const;
	Value building(const AIEngine::BuildingView &building) const;
	Value tile(int x, int y) const;

  public:
    std::uint64_t retainedQueryVectorBytes() const {
        std::uint64_t bytes=remembered.capacity()*sizeof(std::unique_ptr<Chunk>);
        for(const auto& chunk:remembered) if(chunk) bytes+=sizeof(Chunk);
        return bytes+retainedValueVectorBytes(terrainDefinitions);
    }
	struct Cell
	{
		unsigned tick = 0;
		unsigned short terrain = 0, fertility = 0;
		TerrainType terrainType = GRASS;
		unsigned short resource = 65535;
		unsigned amount = 0;
		bool known = false, visible = false, forbidden = false, building = false;
		bool operator==(const Cell &) const = default;
	};
	Cell cell(int x, int y) const;
	unsigned materialStock(int x, int y, MaterialId material) const;
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
	Observations(Game &game, int team);
	// A complete view is borrowed only during this scope. Remembered terrain and
    // spatial fields remain controller-owned values, never retain the world.
    class ObservationScope
    {
        const Observations& observations;
        const AIEngine::AIWorldView* previous;
        std::shared_ptr<const AIEngine::AIWorldView> owned;
      public:
        ObservationScope(const Observations&, const AIEngine::AIWorldView&);
        explicit ObservationScope(const Observations&);
        ~ObservationScope();
        ObservationScope(const ObservationScope&) = delete;
        ObservationScope& operator=(const ObservationScope&) = delete;
    };
    ObservationScope bindObservation(const AIEngine::AIWorldView& value) const { return ObservationScope(*this, value); }
    ObservationScope captureObservation() const { return ObservationScope(*this); }
    bool hasObservation() const { return view != nullptr; }
    const AIEngine::AIWorldView& world() const;
	void observe();
	Value query(const std::string &name, const std::vector<Value> &args,
				const QueryBudget &budget = {}) const;
	void save(GAGCore::OutputStream *stream) const;
	void load(GAGCore::InputStream *stream, int version = VERSION_MINOR);
};
} // namespace Script
