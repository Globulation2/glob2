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
enum class Rejection : unsigned { Identity, Team, Damage, Occupancy, Aim, Range, Clearance, Arrival, Kind, Count };
struct Metrics
{
    Uint64 visits = 0, occupied = 0, clearance = 0, scored = 0;
    Uint64 queriedChunks = 0, temporaryChunks = 0;
    Uint64 examined = 0, rejected = 0;
    std::array<Uint64, unsigned(Rejection::Count)> rejectionReasons{};
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
struct IndexBudget;
struct Observation
{
    SimulationSnapshot::Handle world;
    bool blocksProjectiles = false;
    std::shared_ptr<const OccupiedIndex> index;
    std::shared_ptr<IndexBudget> indexBudget;
    unsigned rebuiltChunks = 0, sharedChunks = 0;
    Uint64 indexCpu=0,factsCpu=0,logicalDigest=0;
};
constexpr SimulationSnapshot::Requirements requirements =
    SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Entities) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Teams) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Catalogs);
// Effects contains render bullet/explosion records, not area attack modifiers.
// Those modifiers are already folded into captured unit attack strength.
// Owner adapter: borrow the already captured union; never captures world data.
Observation observe(const Game&, const SimulationSnapshot::Handle&, const Observation* previous = nullptr, bool profileCpu = false, bool deferIndex = false);
// Uses only captured occupancy and an immutable earlier index; safe in a job.
void prepareIndex(Observation&, const std::shared_ptr<const OccupiedIndex>& previous = {}, bool profileCpu = false);
// Research convenience adapter through the shared Store; not used by jobs.
Observation observe(const Game&, const Observation* previous = nullptr);
std::size_t retainedIndexBytes(const Observation&);
struct Source
{
    BuildingRef identity;
    int x = 0, y = 0, width = 0, height = 0, range = 0, speed = 0;
    Uint32 unitTeams = 0, buildingTeams = 0;
    int buildingDamage = 0;
    std::vector<int> damage;
    std::shared_ptr<const std::vector<int>> sharedDamage;
    Uint32 epoch=0;
    Uint64 damageDigest=0;
    int rhythm=0;
    std::size_t damageCount() const { return sharedDamage ? sharedDamage->size() : damage.size(); }
    int damageAt(unsigned i) const { return sharedDamage ? (*sharedDamage)[i] : damage[i]; }
};
Source source(const Building&, std::shared_ptr<const std::vector<int>> damage = {}, Uint64 damageDigest = 0);
// Both adapters use the same pure selection/ranking kernel. Neither mutates
// gameplay or consumes ammunition. capacity=0 requests the uncapped control.
Plan plan(const Building&, unsigned capacity, unsigned margin = 0);
Plan plan(const Source&, const Observation&, unsigned capacity, unsigned margin = 0);
// Read-only first-valid validation. Starts at zero every time and performs no
// ranking; the caller creates the normal bullet only after a successful result.
Target firstValid(const Building&, const std::vector<Entry>&, Metrics* = nullptr);
// Owner-thread research hook, after ammunition conversion and before cooldown.
// The observer must only read gameplay state. Nested scopes restore the caller.
using AttemptObserver = void (*)(void*, const Building&);
class ScopedAttemptObserver
{
public:
    ScopedAttemptObserver(AttemptObserver, void*);
    ~ScopedAttemptObserver();
    ScopedAttemptObserver(const ScopedAttemptObserver&) = delete;
    ScopedAttemptObserver& operator=(const ScopedAttemptObserver&) = delete;
private:
    AttemptObserver previous;
    void* previousContext;
};
void observeAttempt(const Building&);
// Number of turret steps before the existing routine can report ready. A
// positive cooldown which reaches zero on this step still returns false.
Uint64 stepsUntilReady(int cooldown, unsigned rhythm);
bool refreshNeeded(int cooldown, unsigned rhythm, unsigned delay, unsigned refresh);
// Research CPU accounting uses each executing thread, including executor jobs.
Uint64 threadCpuNs();
}

namespace TurretTargeting
{
// Fixed-boundary producer used by normal-game first-valid firing.
struct Configuration { unsigned delay=1, refresh=2, capacity=3, margin=1; bool profileCpu=false; };
class Pipeline
{
    struct State;
    std::unique_ptr<State> state;
public:
    struct Statistics {
        Uint64 admissions=0, teamJobs=0, observations=0, publications=0;
        Uint64 indexNs=0, waitNs=0, failedGenerations=0;
        Uint64 sourceCpu=0,indexCpu=0,planningCpu=0,publicationCpu=0,admissionCpu=0,joinCpu=0;
        Uint64 ownerPlanningCpu=0;
        Uint64 backgroundIndexCpu=0,ownerBackgroundIndexCpu=0;
        Uint64 rebuiltChunks=0,sharedChunks=0;
        Uint64 sources=0,skipped=0;
        std::size_t pendingPeak=0, indexBytes=0, shortlistBytes=0;
    };
    Pipeline(Game&,Configuration);
    ~Pipeline();
    bool admissionPending(const Game&,bool paused) const;
    void admit(const Game&,const SimulationSnapshot::Handle&,bool paused);
    void publish(Game&);
    void invalidateChangedSources(Game&);
    void forget(BuildingRef);
    // Saving/pausing drains jobs but preserves deadlines and published order.
    void drain();
    void save(const Game&,GAGCore::OutputStream*);
    bool load(const Game&,GAGCore::InputStream*);
    Uint32 checkSum(const Game&) const;
    const std::vector<Entry>& entries(const Building&);
    const Statistics& statistics() const;
    SimulationSnapshot::Handle pendingObservation() const;
};
}
