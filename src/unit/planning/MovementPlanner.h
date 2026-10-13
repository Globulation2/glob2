// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/snapshot/WorldSnapshot.h"
#include "UnitTiming.h"
#include <array>
#include <optional>
#include <vector>

namespace MovementPlanning
{
enum class Mode : Uint8 { Idle, Explore, Resource, Building, Clearing, Pursuit, Count };
enum class Disposition : Uint8 { Travel, GoalReached, NoRoute };
enum class Steering : Uint8 { Wander, Exploration, Geometric, Route };
inline constexpr Uint32 UnknownCost = std::numeric_limits<Uint32>::max();
struct Task
{
    UnitRef unit, targetUnit;
    BuildingRef attached, target;
    int team = 0, x = 0, y = 0, targetX = 0, targetY = 0, material = 0;
    UnitState::Activity activity{};
    UnitState::Medical medical{};
    UnitState::Displacement displacement{};
    UnitJobPurpose purpose{};
    Uint32 capabilities = 0;
    int walk = 0, swim = 0, fly = 0;
    int heading = 8; // observation input, not assignment identity
    bool operator==(const Task&) const = default;
};
struct Request
{
    Task task;
    Mode mode = Mode::Idle;
    Uint64 epoch = 0, actionGeneration = 0, sequence = 0;
    Uint32 observationTick = 0, expectedCompletionTick = 0, publicationTick = 0;
};
struct Destination { int x = 0, y = 0; BuildingRef supplier; };
struct Suggestion
{
    Request request;
    std::optional<Destination> destination;
    std::array<Uint8, 8> directions{0,1,2,3,4,5,6,7};
    Uint8 preferred = 0;
    Disposition disposition = Disposition::NoRoute;
    Steering steering = Steering::Route;
    Uint32 originCost = UnknownCost;
    std::array<Uint32,8> stepCosts{UnknownCost,UnknownCost,UnknownCost,UnknownCost,UnknownCost,UnknownCost,UnknownCost,UnknownCost};
    Uint8 structuralMask = 0, occupiedMask = 0;
    bool patrol = false;
};
inline constexpr std::array<std::array<int,2>,8> Directions{{
    {-1,-1}, {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}}};
Task taskOf(const SimulationSnapshot::UnitCoreView& unit);
std::optional<Mode> modeOf(const UnitCoreState& unit);
std::optional<Uint32> completionTicks(const UnitCoreState& unit);
bool validPermutation(const Suggestion& suggestion);
EntityRandom requestRandom(Uint32 seed, const Request& request);
// Snapshot-only structural legality. Mobile occupancy is deliberately optional.
bool towerThreat(const SimulationSnapshot::Handle& world, const Task& task, int x, int y);
bool legal(const MapState::View& view, const Task& task, int x, int y, bool occupancy);
bool legal(const SimulationSnapshot::Handle& world, const Task& task, int x, int y, bool occupancy);

class Planner
{
    struct Key
    {
        Task task;
        Mode mode;
        Uint64 world, terrain, resources, areas, configuration, structures;
        bool operator==(const Key&) const = default;
    };
    struct Field
    {
        Key key;
        std::vector<Uint32> costs, destinations;
        bool truncated = false;
    };
    std::vector<Field> cache;
    std::size_t budget;
    Field build(const SimulationSnapshot::Handle& world, const Request& request, const Key& key);
public:
    struct Metrics { Uint64 fields = 0, cacheHits = 0, visited = 0, unknown = 0, noGoals = 0, peakBytes = 0; } metrics;
    explicit Planner(std::size_t budget = 32 * 1024 * 1024) : budget(budget) {}
    Suggestion plan(const SimulationSnapshot::Handle& world, const Request& request, Uint32 seed);
    Uint64 retainedBytes() const;
};
}
