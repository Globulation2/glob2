// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "Game.h"
#include "Unit.h"
#include "Team.h"
#include "Version.h"
#include "ActiveRouteRuntime.h"
#include <Stream.h>
#include <BinaryStream.h>
#include <set>

namespace
{
using namespace ResourceClaims;
constexpr unsigned MaximumStates = Team::MAX_COUNT * Unit::MAX_COUNT, MaximumRouteCells = 4096;
void write64(GAGCore::OutputStream* out, Uint64 value)
{ out->writeUint32(Uint32(value), "low"); out->writeUint32(Uint32(value >> 32), "high"); }
Uint64 read64(GAGCore::InputStream* in)
{ const auto low = in->readUint32("low"); return Uint64(low) | (Uint64(in->readUint32("high")) << 32); }
void saveIdentity(GAGCore::OutputStream* out, const RouteIdentity& identity)
{
    out->writeEnterSection("owner"); write64(out, identity.unit); out->writeLeaveSection();
    out->writeEnterSection("task"); write64(out, identity.task); out->writeLeaveSection();
    out->writeEnterSection("request"); write64(out, identity.request); out->writeLeaveSection();
}
RouteIdentity loadIdentity(GAGCore::InputStream* in)
{
    RouteIdentity value;
    in->readEnterSection("owner"); value.unit = read64(in); in->readLeaveSection();
    in->readEnterSection("task"); value.task = read64(in); in->readLeaveSection();
    in->readEnterSection("request"); value.request = read64(in); in->readLeaveSection();
    if (!value.request) throw std::runtime_error("Invalid active route request");
    return value;
}
void saveRoute(GAGCore::OutputStream* out, const Route& route)
{
    out->writeUint8(unsigned(route.status), "status"); out->writeUint32(route.cost, "cost");
    out->writeUint32(route.expansions, "expansions"); out->writeUint32(route.cells.size(), "cells");
    for (unsigned i = 0; i < route.cells.size(); ++i) {
        out->writeEnterSection(i); out->writeUint32(route.cells[i], "cell"); out->writeLeaveSection();
    }
}
Route loadRoute(GAGCore::InputStream* in, const Map& map, Uint32 start)
{
    Route route;
    const auto status = in->readUint8("status");
    if (status > unsigned(RouteStatus::ExpansionLimit)) throw std::runtime_error("Invalid active route status");
    route.status = RouteStatus(status); route.cost = in->readUint32("cost");
    route.expansions = in->readUint32("expansions"); const auto count = in->readUint32("cells");
    if (count > MaximumRouteCells || route.expansions > MaximumRouteCells ||
        (route.status != RouteStatus::Found && count)) throw std::runtime_error("Invalid active route size");
    auto previous = start;
    const field::Grid grid(map.getW(), map.getH());
    for (unsigned i = 0; i < count; ++i) {
        in->readEnterSection(i); const auto cell = in->readUint32("cell"); in->readLeaveSection();
        bool adjacent = false;
        if (cell < map.cellCount()) grid.neighborIndices(previous, field::Surrounding, [&](int n) { adjacent |= unsigned(n) == cell; });
        if (!adjacent) throw std::runtime_error("Invalid active route continuity");
        route.cells.push_back(cell); previous = cell;
    }
    return route;
}
RouteProfile profileOf(const Unit& unit)
{ return {unit.movementClass(), unit.owner->me, unit.gid, OccupancyPolicy::Static}; }
bool endsAtTarget(const Map& map, const Route& route, Uint32 start, Uint32 target)
{
    if (route.status != RouteStatus::Found) return true;
    const auto end = route.cells.empty() ? start : route.cells.back();
    const auto dx = unsigned(std::abs(int(end % map.getW()) - int(target % map.getW())));
    const auto dy = unsigned(std::abs(int(end / map.getW()) - int(target / map.getW())));
    return std::min(dx, unsigned(map.getW()) - dx) <= 1 && std::min(dy, unsigned(map.getH()) - dy) <= 1;
}
}

ResourceClaims::StepResult Map::nextClaimRouteStep(Unit& unit, int* dx, int* dy)
{
    using namespace ResourceClaims;
    if (!activeClaimsEnabled() || !unit.activeMaterialClaim || !game || !cellCount()) return ResourceClaims::StepResult::Failed;
    const auto owner = unitOwner(unit.gid, unit.scriptIdentity);
    const auto current = Uint32(coordToIndex(unit.posX, unit.posY));
    const auto target = unit.activeMaterialClaim->access;
    if (target >= cellCount()) return ResourceClaims::StepResult::Failed;
    if (warpDistMax(unit.posX, unit.posY, target % getW(), target / getW()) <= 1) return ResourceClaims::StepResult::Arrived;
    if (!materialClaimRoutes) materialClaimRoutes = std::make_unique<ActiveRouteRuntime>(compute);
    auto& runtime = *materialClaimRoutes;
    auto found = runtime.states.find(owner);
    if (found != runtime.states.end() &&
        (found->second.identity.task != unit.materialClaimTask || found->second.target != target)) {
        cancelClaimRoute(owner); found = runtime.states.end();
    }
    if (found == runtime.states.end()) {
        if (runtime.states.size() >= MaximumStates || !runtime.pipeline.admissionAvailable()) return ResourceClaims::StepResult::Failed;
        // Retain only route inputs. In particular, a pending route must not
        // keep old entity records and every published gradient plane alive.
        using C = SimulationSnapshot::Component;
        const auto mask = SimulationSnapshot::bit(C::Catalogs) | SimulationSnapshot::bit(C::Terrain)
            | SimulationSnapshot::bit(C::Resources) | SimulationSnapshot::bit(C::Occupancy) | SimulationSnapshot::bit(C::Areas);
        RouteInput input{game->observation().project(mask), profileOf(unit), current, target};
        const auto identity = runtime.pipeline.submit(owner, unit.materialClaimTask, game->stepCounter, std::move(input));
        runtime.states.emplace(owner, ActiveRouteRuntime::State{identity, current, target, 0, Uint64(game->stepCounter) + 1});
        return ResourceClaims::StepResult::Pending;
    }
    auto& state = found->second;
    if (state.pending) return ResourceClaims::StepResult::Pending;
    if (state.route.status != RouteStatus::Found) return ResourceClaims::StepResult::Failed;
    // Consume a step only after the unit has actually moved. A blocked attempt
    // therefore retries the same step, without skipping cells in its path.
    while (state.cursor < state.route.cells.size() && state.route.cells[state.cursor] == current) ++state.cursor;
    const auto anchor = state.cursor ? state.route.cells[state.cursor - 1] : state.start;
    if (current != anchor || state.cursor == state.route.cells.size()) {
        cancelClaimRoute(owner); return ResourceClaims::StepResult::Failed;
    }
    const auto next = state.route.cells[state.cursor];
    const auto live = stateView();
    const auto profile = profileOf(unit);
    // Immobile markers include harvesting workers and zero-step waiters. They
    // are traffic too; distinguish them from actual changes to map geometry.
    const bool geometry = gradient_kernel::terrainAllowsMovement(live.terrainProperties(next), profile.movement)
        && (profile.movement == AIR_MOVEMENT_CLASS ? !MapState::resourceBlocksAir(live, next)
            : !(live.areas[next].forbidden & profile.teamMask) && live.occupancy[next].building == 0xffff
                && !MapState::resourceBlocksGround(live, next));
    if (!geometry) {
        cancelClaimRoute(owner); return ResourceClaims::StepResult::Failed;
    }
    auto traffic = profile; traffic.occupancy = OccupancyPolicy::FrozenUnits;
    if (!routePassable(live, next, traffic)) return ResourceClaims::StepResult::Pending;
    int stepX = int(next % getW()) - unit.posX, stepY = int(next / getW()) - unit.posY;
    if (stepX > 1) stepX -= getW(); else if (stepX < -1) stepX += getW();
    if (stepY > 1) stepY -= getH(); else if (stepY < -1) stepY += getH();
    if (std::abs(stepX) > 1 || std::abs(stepY) > 1 || (!stepX && !stepY)) {
        cancelClaimRoute(owner); return ResourceClaims::StepResult::Failed;
    }
    *dx = stepX; *dy = stepY; return ResourceClaims::StepResult::Ready;
}

void Map::cancelClaimRoute(ResourceClaims::Owner owner)
{
    if (!materialClaimRoutes) return;
    materialClaimRoutes->pipeline.cancel(owner); materialClaimRoutes->states.erase(owner);
}
void Map::flushClaimRoutes()
{ if (materialClaimRoutes) materialClaimRoutes->pipeline.flush(); }
void Map::publishClaimRoutes()
{
    if (!materialClaimRoutes || !game) return;
    auto& runtime = *materialClaimRoutes;
    for (auto& output : runtime.pipeline.publish(game->stepCounter)) {
        const auto found = runtime.states.find(output.identity.unit);
        if (found == runtime.states.end() || found->second.identity != output.identity) continue;
        found->second.pending = false; found->second.route = std::move(output.route);
    }
}

void Map::saveActiveClaimRoutes(GAGCore::OutputStream* stream) const
{
    using namespace ResourceClaims;
    stream->writeEnterSection("activeMaterialClaims");
    stream->writeUint8(unsigned(activeClaimPolicy), "policy");
    const auto pending = materialClaimRoutes ? materialClaimRoutes->pipeline.exportPending() : std::vector<RoutePublication>{};
    stream->writeEnterSection("sequence"); write64(stream, materialClaimRoutes ? materialClaimRoutes->pipeline.requestSequence() : 0); stream->writeLeaveSection();
    const auto sealed = materialClaimRoutes ? materialClaimRoutes->pipeline.sealedSubmissionTick() : std::optional<Uint64>{};
    stream->writeEnterSection("sealed"); stream->writeUint8(sealed.has_value(), "has"); write64(stream, sealed.value_or(0)); stream->writeLeaveSection();
    stream->writeEnterSection("rulesEpoch"); stream->writeUint8(activeClaimRules.has_value(), "has");
    if (activeClaimRules) for (unsigned i = 0; i < activeClaimRules->size(); ++i) {
        stream->writeEnterSection(i); write64(stream, (*activeClaimRules)[i]); stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
    stream->writeUint32(materialClaimRoutes ? materialClaimRoutes->states.size() : 0, "states");
    stream->writeEnterSection("records");
    unsigned i = 0;
    if (materialClaimRoutes) for (const auto& [owner, state] : materialClaimRoutes->states) {
        stream->writeEnterSection(i++); saveIdentity(stream, state.identity);
        stream->writeUint32(state.start, "start"); stream->writeUint32(state.target, "target");
        stream->writeUint32(state.cursor, "cursor"); stream->writeUint8(state.pending, "pending");
        stream->writeEnterSection("due"); write64(stream, state.due); stream->writeLeaveSection();
        saveRoute(stream, state.route); stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
    stream->writeUint32(pending.size(), "pendingRoutes");
    stream->writeEnterSection("outputs");
    for (i = 0; i < pending.size(); ++i) {
        const auto& output = pending[i]; stream->writeEnterSection(i); saveIdentity(stream, output.identity);
        stream->writeEnterSection("due"); write64(stream, output.due); stream->writeLeaveSection();
        stream->writeUint32(output.start, "start"); stream->writeUint32(output.target, "target");
        saveRoute(stream, output.route); stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
    stream->writeLeaveSection();
}

void Map::loadActiveClaimRoutes(GAGCore::InputStream* stream, Sint32 versionMinor)
{
    using namespace ResourceClaims;
    materialClaimRoutes.reset(); activeClaimRules.reset(); activeClaimPolicy = ActiveClaimPolicy::Disabled;
    if (versionMinor < ActiveClaimsFormat) return;
    GAGCore::BinaryInputStream::CheckedReads checked(stream);
    stream->readEnterSection("activeMaterialClaims"); const auto policy = stream->readUint8("policy");
    if (policy > unsigned(ActiveClaimPolicy::PrototypeStationaryV1) || (policy && !materialDestinationsEnabled()))
        throw std::runtime_error("Invalid saved active material policy");
    activeClaimPolicy = ActiveClaimPolicy(policy);
    stream->readEnterSection("sequence"); const auto sequence = read64(stream); stream->readLeaveSection();
    stream->readEnterSection("sealed"); const auto hasSealed = stream->readUint8("has"); const auto sealed = read64(stream); stream->readLeaveSection();
    if (hasSealed > 1 || bool(sequence) != bool(hasSealed) || (!hasSealed && sealed) || (hasSealed && sealed > game->stepCounter)) throw std::runtime_error("Invalid saved route admission boundary");
    if (!policy && sequence) throw std::runtime_error("Disabled active route sequence");
    stream->readEnterSection("rulesEpoch"); const auto hasRules = stream->readUint8("has");
    if (hasRules > 1 || bool(hasRules) != bool(policy)) throw std::runtime_error("Invalid active claim rules epoch");
    if (hasRules) {
        activeClaimRules.emplace();
        for (unsigned i = 0; i < activeClaimRules->size(); ++i) {
            stream->readEnterSection(i); (*activeClaimRules)[i] = read64(stream); stream->readLeaveSection();
        }
        if ((*activeClaimRules)[0] > 1 || (*activeClaimRules)[1] > 1 || (*activeClaimRules)[2] > UINT32_MAX)
            throw std::runtime_error("Invalid active claim rules epoch values");
    }
    stream->readLeaveSection();
    const auto count = stream->readUint32("states");
    if (count > MaximumStates || (count && !policy)) throw std::runtime_error("Invalid active route state count");
    if (policy) materialClaimRoutes = std::make_unique<ActiveRouteRuntime>(compute);
    stream->readEnterSection("records");
    for (unsigned i = 0; i < count; ++i) {
        stream->readEnterSection(i); ActiveRouteRuntime::State state; state.identity = loadIdentity(stream);
        state.start = stream->readUint32("start"); state.target = stream->readUint32("target");
        state.cursor = stream->readUint32("cursor"); const auto pending = stream->readUint8("pending");
        state.pending = pending;
        stream->readEnterSection("due"); state.due = read64(stream); stream->readLeaveSection();
        if (state.start >= cellCount() || state.target >= cellCount() || pending > 1 || !state.due ||
            state.due > Uint64(game->stepCounter) + 1) throw std::runtime_error("Invalid active route state");
        state.route = loadRoute(stream, *this, state.start);
        if (!state.identity.task || state.identity.request > sequence || state.cursor > state.route.cells.size() ||
            (!state.pending && !endsAtTarget(*this, state.route, state.start, state.target)) || (state.pending && (state.cursor || !state.route.cells.empty())) ||
            !materialClaimRoutes->states.emplace(state.identity.unit, std::move(state)).second)
            throw std::runtime_error("Invalid duplicate active route state");
        stream->readLeaveSection();
    }
    stream->readLeaveSection();
    const auto pendingCount = stream->readUint32("pendingRoutes");
    if (pendingCount > count) throw std::runtime_error("Invalid active pending count");
    std::vector<RoutePublication> pending;
    std::set<ResourceClaims::Owner> pendingOwners;
    stream->readEnterSection("outputs");
    for (unsigned i = 0; i < pendingCount; ++i) {
        stream->readEnterSection(i); RoutePublication output; output.identity = loadIdentity(stream);
        stream->readEnterSection("due"); output.due = read64(stream); stream->readLeaveSection();
        output.start = stream->readUint32("start"); output.target = stream->readUint32("target");
        output.width = getW(); output.height = getH();
        const auto found = materialClaimRoutes->states.find(output.identity.unit);
        if (!pendingOwners.insert(output.identity.unit).second || found == materialClaimRoutes->states.end() || !found->second.pending || found->second.identity != output.identity ||
            found->second.start != output.start || found->second.target != output.target || found->second.due != output.due)
            throw std::runtime_error("Mismatched saved active route");
        output.route = loadRoute(stream, *this, output.start);
        if (!endsAtTarget(*this, output.route, output.start, output.target)) throw std::runtime_error("Invalid active route terminal");
        pending.push_back(std::move(output)); stream->readLeaveSection();
    }
    stream->readLeaveSection();
    if (materialClaimRoutes) {
        const auto expected = std::count_if(materialClaimRoutes->states.begin(), materialClaimRoutes->states.end(), [](const auto& entry) { return entry.second.pending; });
        if (std::size_t(expected) != pending.size()) throw std::runtime_error("Missing pending active route");
        materialClaimRoutes->pipeline.restore(std::move(pending));
        materialClaimRoutes->pipeline.restoreControl(sequence, hasSealed ? std::optional<Uint64>{sealed} : std::nullopt);
    }
    stream->readLeaveSection();
}

Uint32 Map::activeClaimRouteChecksum(bool includePending) const
{
    const auto pending = includePending && materialClaimRoutes ? materialClaimRoutes->pipeline.exportPending() : std::vector<ResourceClaims::RoutePublication>{};
    Uint32 hash = 2166136261u;
    const auto mix = [&](Uint64 value) { hash = (hash ^ Uint32(value)) * 16777619u; hash = (hash ^ Uint32(value >> 32)) * 16777619u; };
    mix(unsigned(activeClaimPolicy));
    mix(activeClaimRules.has_value());
    if (activeClaimRules) for (auto rule : *activeClaimRules) mix(rule);
    mix(materialClaimRoutes ? materialClaimRoutes->pipeline.requestSequence() : 0);
    const auto sealed = materialClaimRoutes ? materialClaimRoutes->pipeline.sealedSubmissionTick() : std::optional<Uint64>{};
    mix(sealed.has_value()); mix(sealed.value_or(0));
    mix(materialClaimRoutes ? materialClaimRoutes->states.size() : 0);
    const auto route = [&](const ResourceClaims::Route& value) {
        mix(unsigned(value.status)); mix(value.cost); mix(value.expansions); mix(value.cells.size());
        for (auto cell : value.cells) mix(cell);
    };
    if (materialClaimRoutes) {
        for (const auto& [owner, state] : materialClaimRoutes->states) {
            mix(owner); mix(state.identity.task); mix(state.identity.request); mix(state.start); mix(state.target);
            mix(state.cursor); mix(state.due); mix(state.pending); route(state.route);
        }
        for (const auto& output : pending) {
            mix(output.identity.unit); mix(output.identity.task); mix(output.identity.request); mix(output.due); route(output.route);
        }
    }
    return hash;
}
