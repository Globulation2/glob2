// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/EntityRef.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <climits>
#include <vector>

class Building;
namespace GAGCore { class InputStream; class OutputStream; }
namespace TurretTargeting
{
enum class Kind : Uint8 { None, Building, Unit };
// The publication payload contains no score or borrowed simulation pointer.
struct Entry
{
    Kind kind = Kind::None;
    Uint16 gid = 0xffff;
    Uint32 incarnation = 0;
    int offsetX = 0, offsetY = 0;
    bool operator==(const Entry&) const = default;
};
struct Target
{
    Entry entry;
    int x = 0, y = 0, ticks = 0, score = INT_MIN, priority = 0;
    bool found() const { return entry.kind != Kind::None; }
};
struct Metrics
{
    Uint64 visits = 0, occupied = 0, clearance = 0, scored = 0;
    Uint64 queriedChunks = 0, temporaryChunks = 0;
};
struct Plan
{
    Target preferred;
    std::vector<Entry> entries;
    Metrics metrics;
};
// The snapshot owns combat facts, including effective attack strength after
// glass-cannon and area modifiers. The index contains only occupancy.
struct OccupiedIndex;
struct Observation
{
    SimulationSnapshot::Handle world;
    bool blocksProjectiles = false;
    std::shared_ptr<const OccupiedIndex> index;
    unsigned rebuiltChunks = 0, sharedChunks = 0;
};
constexpr SimulationSnapshot::Requirements requirements =
    SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Entities) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Teams) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Catalogs);
// Owner adapter: borrow the already captured union; never captures world data.
Observation observe(const Game&, const SimulationSnapshot::Handle&, const Observation* previous = nullptr, bool deferIndex = false);
// Uses only captured occupancy and an immutable earlier index; safe in a job.
void prepareIndex(Observation&, const std::shared_ptr<const OccupiedIndex>& previous = {});
// Fixture convenience through the shared Store; not used by jobs.
Observation observe(const Game&, const Observation* previous = nullptr);
struct Source
{
    BuildingRef identity;
    int x = 0, y = 0, width = 0, height = 0, range = 0, speed = 0;
    Uint32 unitTeams = 0, buildingTeams = 0;
    int buildingDamage = 0;
    std::vector<int> damage;
    std::shared_ptr<const std::vector<int>> sharedDamage;
    std::size_t damageCount() const { return sharedDamage ? sharedDamage->size() : damage.size(); }
    int damageAt(unsigned i) const { return sharedDamage ? (*sharedDamage)[i] : damage[i]; }
};
Source source(const Building&, std::shared_ptr<const std::vector<int>> damage = {});
// Both adapters use the same pure selection/ranking kernel; the live one is the
// test reference. capacity=0 requests the uncapped ranking.
Plan plan(const Building&, unsigned capacity, unsigned margin = 0);
Plan plan(const Source&, const Observation&, unsigned capacity, unsigned margin = 0);
// Read-only first-valid validation against live state. Lists need no
// invalidation: every entry is rechecked before the normal bullet is created.
Target firstValid(const Building&, const std::vector<Entry>&);
// Number of turret steps before the existing routine can report ready. A
// positive cooldown which reaches zero on this step still returns false.
Uint64 stepsUntilReady(int cooldown, unsigned rhythm);
bool refreshNeeded(int cooldown, unsigned rhythm, unsigned delay, unsigned refresh);
}

namespace TurretTargeting
{
// Fixed-boundary producer used by normal-game first-valid firing.
struct Configuration { unsigned delay=1, refresh=2, capacity=3, margin=1; };
class Pipeline
{
    struct State;
    std::unique_ptr<State> state;
public:
    struct Statistics { Uint64 admissions=0, teamJobs=0, publications=0, sources=0, skipped=0; };
    Pipeline(Game&,Configuration);
    ~Pipeline();
    bool admissionPending(const Game&,bool paused) const;
    void admit(const Game&,const SimulationSnapshot::Handle&,bool paused);
    void publish(Game&);
    void forget(BuildingRef);
    // Saving/pausing drains jobs but preserves deadlines and published order.
    void drain();
    void save(const Game&,GAGCore::OutputStream*);
    bool load(const Game&,GAGCore::InputStream*,int versionMinor);
    const std::vector<Entry>& entries(const Building&);
    const Statistics& statistics() const;
};
}
