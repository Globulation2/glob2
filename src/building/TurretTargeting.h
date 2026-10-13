// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/EntityRef.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <climits>
#include <vector>

class Building;
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
// These supplemental facts are captured at the same boundary as world. In
// particular offense includes glass-cannon and area effects, not raw damage.
struct OccupiedIndex;
struct IndexBudget;
struct Observation
{
    SimulationSnapshot::Handle world;
    std::vector<int> offense;
    bool blocksProjectiles = false;
    std::shared_ptr<const OccupiedIndex> index;
    std::shared_ptr<IndexBudget> indexBudget;
    unsigned rebuiltChunks = 0, sharedChunks = 0;
};
constexpr SimulationSnapshot::Requirements requirements =
    SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Entities) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Teams) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Catalogs) |
    SimulationSnapshot::bit(SimulationSnapshot::Component::Effects);
// Owner adapter: borrow the already captured union; never captures world data.
Observation observe(const Game&, const SimulationSnapshot::Handle&, const Observation* previous = nullptr);
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
};
Source source(const Building&);
// Both adapters use the same pure selection/ranking kernel. Neither mutates
// gameplay or consumes ammunition. capacity=0 requests the uncapped control.
Plan plan(const Building&, unsigned capacity, unsigned margin = 0);
Plan plan(const Source&, const Observation&, unsigned capacity, unsigned margin = 0);
// Shadow first-valid consumer. Starts at zero every time, performs no ranking
// and creates no hypothetical bullets.
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
}

namespace TurretTargeting
{
// Read-only producer trial. Published entries never replace normal firing.
struct Configuration { unsigned delay=1, refresh=1, capacity=8, margin=1; };
class Pipeline
{
    struct State;
    std::unique_ptr<State> state;
public:
    struct Statistics {
        Uint64 admissions=0, teamJobs=0, observations=0, publications=0;
        Uint64 indexNs=0, waitNs=0, failedGenerations=0;
        std::size_t pendingPeak=0, indexBytes=0, shortlistBytes=0;
    };
    Pipeline(Game&,Configuration);
    ~Pipeline();
    bool admissionPending(const Game&,bool paused) const;
    SimulationSnapshot::Requirements pendingRequirements(const Game&,bool paused) const;
    void admit(const Game&,const SimulationSnapshot::Handle&,bool paused);
    void publish(Game&);
    // Saving/pausing drains jobs but preserves deadlines and published order.
    void drain();
    const std::vector<Entry>& entries(const Building&);
    const Statistics& statistics() const;
    SimulationSnapshot::Handle pendingObservation() const;
};
}
