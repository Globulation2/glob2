#include "ResourceRegistry.h"
#include <map>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include "ScriptQueryStorage.h"
#include "TerrainType.h"
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
// A colony's memory of tiles it has seen: what each looked like when last visible.
struct ObservationMemory
{
	struct Tile
	{
		unsigned tick = 0;
		unsigned short fertility = 0;
		// Corner terrains: top-left, top-right, bottom-left, bottom-right.
		std::array<TerrainType, 4> corners{GRASS, GRASS, GRASS, GRASS};
		unsigned short type = 65535;
		unsigned char variety = 0;
		unsigned amount = 0;
		bool known = false;
	};
	// Lazily allocated indexed chunks avoid tree lookups without allocating an
	// entire map per controller before it has explored any terrain.
	using Chunk = std::array<Tile, 256>;
	std::vector<std::unique_ptr<Chunk>> remembered;
	std::map<unsigned, std::array<Uint16, MaterialCount>> rememberedStocks;
	unsigned knownTiles = 0;
	ObservationMemory() = default;
	ObservationMemory(const ObservationMemory &other); // a deep copy
	ObservationMemory(ObservationMemory &&) = default;
	ObservationMemory &operator=(ObservationMemory &&) = default;
	ObservationMemory &operator=(const ObservationMemory &) = delete;
	const Tile *lookup(unsigned index) const;
};
// Rule tables built once per immutable registry and handed out as read-only values.
struct ObservationDefinitions
{
	std::shared_ptr<const TerrainRegistry> terrainRegistry;
	std::shared_ptr<const ResourceRegistry> terrainResourceRegistry;
	Value terrain;
	std::shared_ptr<const ResourceRegistry> resourceRegistry;
	Value resources;
};
struct ObservationCell
{
	unsigned tick = 0;
	unsigned short fertility = 0;
	std::array<TerrainType, 4> corners{GRASS, GRASS, GRASS, GRASS};
	unsigned short resource = 65535;
	unsigned amount = 0;
	bool known = false, visible = false, forbidden = false, building = false;
	bool operator==(const ObservationCell &) const = default;
};
struct ObservationSpatialEntity
{
	int team, type, x, y, hp, attack;
	bool isVirtual;
	int buildingType = -1;
};
// The read-only half of a colony's observations: one immutable world view and the
// colony's memory, read as the team sees them. It touches nothing else, so a reader
// over a detached view and a copy of the memory can run on any thread.
class ObservationReader
{
	const AIEngine::AIWorldView *view;
	const ObservationMemory *memory;
	int team;
	unsigned profile;
	ObservationDefinitions *definitions;
	Value ref(const AIEngine::UnitView *unit) const;
	Value ref(const AIEngine::BuildingView *building) const;
	Value unit(const AIEngine::UnitView &unit, bool extended = false) const;
	Value building(const AIEngine::BuildingView &building) const;
	Value tile(int x, int y) const;

  public:
	ObservationReader(const AIEngine::AIWorldView &view, const ObservationMemory &memory, int team, unsigned profile,
					  ObservationDefinitions &definitions)
		: view(&view), memory(&memory), team(team), profile(profile), definitions(&definitions)
	{
	}
	const AIEngine::AIWorldView &world() const { return *view; }
	// Every query but the scenario owner's objectives and hints, which read the game.
	Value query(const std::string &name, const std::vector<Value> &args, const QueryBudget &budget = {}) const;
	ObservationCell cell(int x, int y) const;
	unsigned materialStock(int x, int y, MaterialId material) const;
	void visitSpatialEntities(bool units, int filter, const std::function<void(const ObservationSpatialEntity &)> &visit,
							  const QueryBudget &budget) const;
};
// A reader's inputs, owned: a captured world view and a copy of the colony's memory.
// Taken at a simulation boundary and then read anywhere, e.g. on a worker thread.
class DetachedObservation
{
	std::shared_ptr<const AIEngine::AIWorldView> view;
	ObservationMemory memory;
	int team;
	unsigned profile;
	mutable ObservationDefinitions definitions;

  public:
	DetachedObservation(std::shared_ptr<const AIEngine::AIWorldView> view, ObservationMemory memory, int team,
						unsigned profile)
		: view(std::move(view)), memory(std::move(memory)), team(team), profile(profile)
	{
	}
	const AIEngine::AIWorldView &world() const { return *view; }
	ObservationReader reader() const { return {*view, memory, team, profile, definitions}; }
};
class Observations
{
	Game &game; // Scenario-owner fallback only; bound AI queries never access it.
	mutable const AIEngine::AIWorldView* view = nullptr;
	mutable ObservationDefinitions definitions;
	int team;
	unsigned profile = 1;
	ObservationMemory memory;
	ObservationMemory::Tile &remember(unsigned index);
	unsigned lastTick = 0xffffffffu;
	ObservationReader reader() const { return {world(), memory, team, profile, definitions}; }
	std::shared_ptr<const AIEngine::AIWorldView> captureView() const;

  public:
    std::uint64_t retainedQueryVectorBytes() const {
        std::uint64_t bytes=memory.remembered.capacity()*sizeof(std::unique_ptr<ObservationMemory::Chunk>);
        for(const auto& chunk:memory.remembered) if(chunk) bytes+=sizeof(ObservationMemory::Chunk);
        return bytes+retainedValueVectorBytes(definitions.terrain);
    }
	using Cell = ObservationCell;
	Cell cell(int x, int y) const;
	unsigned materialStock(int x, int y, MaterialId material) const;
	using SpatialEntity = ObservationSpatialEntity;
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
	// observe() on a freshly captured view, then that view and a copy of the memory,
	// for reading off the simulation thread.
	DetachedObservation observeAndDetach();
	Value query(const std::string &name, const std::vector<Value> &args,
				const QueryBudget &budget = {}) const;
	void save(GAGCore::OutputStream *stream) const;
	void load(GAGCore::InputStream *stream); // current format
	void load(GAGCore::InputStream *stream, int version);
};
} // namespace Script
