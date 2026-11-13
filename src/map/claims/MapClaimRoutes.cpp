// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "gradient/MapRouting.h"
#include "Game.h"
#include "Unit.h"
#include "Team.h"
#include "Version.h"
#include "ActiveRouteRuntime.h"
#include "ClaimPolicy.h"
#include "ClaimPattern.h"
#include <Stream.h>
#include <BinaryStream.h>
#include <cstdlib>
#include <set>

namespace
{
using namespace ResourceClaims;
// A route search expands at most RouteMaximumExpansions cells, so neither a
// route's length nor its recorded expansions can exceed it.
constexpr unsigned MaximumStates = Team::MAX_COUNT * Unit::MAX_COUNT, MaximumRouteCells = Policy::RouteMaximumExpansions;
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
// One field order for save, load and both checksums. f(field,name,index)
// receives index -1 for scalar fields and the entry index for cache entries.
template<class R,class F> void visitRecoveryFields(R& r,F&& f)
{
    f(r.nextProbe,"nextProbe",-1);f(r.pinnedAccess,"pinnedAccess",-1);f(r.windowStart,"windowStart",-1);
    f(r.backoffStart,"backoffStart",-1);f(r.failureStreak,"failureStreak",-1);
    f(r.material,"material",-1);f(r.movement,"movement",-1);f(r.replacement,"replacement",-1);f(r.attempts,"attempts",-1);
    f(r.recovering,"recovering",-1);f(r.windowKnown,"windowKnown",-1);f(r.backoffKnown,"backoffKnown",-1);
    for(unsigned i=0;i<r.entries.size();++i) {
        auto& e=r.entries[i];
        f(e.access,"access",int(i));f(e.tick,"tick",int(i));f(e.ttl,"ttl",int(i));
    }
}
template<class F> void recoveryFields(ResourceClaims::ClaimRecovery& r,F&& f)
{
    visitRecoveryFields(r,[&](auto& field,const char* name,int index) {
        if(index<0){f(field,name);return;}
        // Indexed keys are stable in both binary and text streams.
        const auto key=name+std::to_string(index);f(field,key.c_str());
    });
}
// Checksums run every tick: hash the same fields without building names.
template<class F> void recoveryValues(const ResourceClaims::ClaimRecovery& r,F&& f)
{
    visitRecoveryFields(r,[&](const auto& field,const char*,int){f(field);});
}
RouteProfile profileOf(const Unit& unit)
{ return {accessClass(unit.movementAccess()), unit.owner->me, unit.gid, OccupancyPolicy::Static}; }
bool endsAtTarget(const Map& map, const Route& route, Uint32 start, Uint32 target)
{
    if (route.status != RouteStatus::Found) return true;
    const auto end = route.cells.empty() ? start : route.cells.back();
    const auto dx = unsigned(std::abs(int(end % map.getW()) - int(target % map.getW())));
    const auto dy = unsigned(std::abs(int(end / map.getW()) - int(target / map.getW())));
    return std::min(dx, unsigned(map.getW()) - dx) <= 1 && std::min(dy, unsigned(map.getH()) - dy) <= 1;
}
}

ResourceClaims::ClaimRecoverySelection Map::beginClaimRecoverySelection(const Unit& unit)
{
    using namespace ResourceClaims;
    if(!activeClaimsEnabled())return {};
    if(!materialClaimRoutes){materialClaimRoutes=std::make_unique<ActiveRouteRuntime>(compute);claimRuntimeChanged();}
    const auto [entry,inserted]=materialClaimRoutes->recovery.try_emplace(unitOwner(unit.gid,unit.scriptIdentity));
    auto& recovery=entry->second;
    const auto material=std::uint8_t(unit.destinationPurpose),movement=std::uint8_t(accessClass(unit.movementAccess()));
    if(inserted || recovery.material!=material || recovery.movement!=movement)claimRuntimeChanged();
    recovery.setContext(material,movement);
    return {&recovery,game->stepCounter,recovery.recovering?recovery.nextProbe:0,recovery.recovering,
        !recovery.failureStreak || recovery.allowsAttempt(game->stepCounter)};
}
void Map::finishClaimRecoverySelection(const Unit& unit,unsigned probe,unsigned size)
{
    const auto view=beginClaimRecoverySelection(unit);if(!view.state)return;
    auto& recovery=materialClaimRoutes->recovery.at(ResourceClaims::unitOwner(unit.gid,unit.scriptIdentity));
    const auto before=recovery;
    if(probe==size)recovery.exhausted();else recovery.selected(probe,size);
    if(!(recovery==before))claimRuntimeChanged();
}
unsigned Map::recordClaimFailure(const Unit& unit,ResourceClaims::ClaimFailureKind kind)
{
    using namespace ResourceClaims;
    const auto view=beginClaimRecoverySelection(unit);if(!view.state)return 0;
    auto& recovery=materialClaimRoutes->recovery.at(unitOwner(unit.gid,unit.scriptIdentity));
    claimRuntimeChanged();
    if(kind==ClaimFailureKind::NoResource)recovery.recordFailure(game->stepCounter,ClaimRecovery::NoTile,0);
    else {
        const auto found=materialClaimRoutes->states.find(unitOwner(unit.gid,unit.scriptIdentity));
        if(found==materialClaimRoutes->states.end() || found->second.pending || found->second.awaitingAdmission ||
            found->second.route.status==RouteStatus::Found)return 0;
        const auto& state=found->second;
        // Normal successful cadence refreshes are not recovery attempts. On the
        // first terminal failure, account its original admission once; later
        // recovery submissions are counted before they enter the worker queue.
        if(!recovery.failureStreak && state.identity.request)
            recovery.admit(Uint32(state.due-1));
        recovery.recordFailure(game->stepCounter,state.target,
            state.route.status==RouteStatus::Unreachable?ClaimRecovery::TtlTicks:ClaimRecovery::BackoffTicks);
    }
    return recovery.failureStreak;
}
void Map::claimRecoveryHarvested(const Unit& unit)
{
    if(!materialClaimRoutes)return;
    const auto found=materialClaimRoutes->recovery.find(ResourceClaims::unitOwner(unit.gid,unit.scriptIdentity));
    if(found==materialClaimRoutes->recovery.end())return;
    const auto before=found->second;
    found->second.harvested();
    if(!(found->second==before))claimRuntimeChanged();
}
void Map::forgetClaimRecovery(ResourceClaims::Owner owner)
{if(materialClaimRoutes && materialClaimRoutes->recovery.erase(owner))claimRuntimeChanged();}

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
    // Route stepping advances cursors, waits and admissions in place.
    claimRuntimeChanged();
    auto& runtime = *materialClaimRoutes;
    auto found = runtime.states.find(owner);
    if (found != runtime.states.end() &&
        (found->second.identity.task != unit.materialClaimTask || found->second.target != target)) {
        cancelClaimRoute(owner); found = runtime.states.end();
    }
    const auto live = stateView();
    auto traffic = profileOf(unit); traffic.occupancy = OccupancyPolicy::FrozenUnits;
    // Static geometry only: a cell other units merely stand on is traffic,
    // not a reason to discard the route.
    const auto geometry = [&](Uint32 cell) {
        return gradient_kernel::terrainAllowsMovement(live.terrainProperties(cell), traffic.movement)
            && (traffic.movement == AIR_MOVEMENT_CLASS ? !MapState::resourceBlocksAir(live, cell)
                : !(live.areas[cell].forbidden & traffic.teamMask) && live.occupancy[cell].building == 0xffff
                    && !MapState::resourceBlocksGround(live, cell));
    };
    // Routes are computed off the owner thread and published on the next
    // tick. Rather than stand still meanwhile, the worker takes the ordinary
    // material gradient step for the claimed material, exactly as claims-off
    // movement would (demand, movement outcome and reactive refresh
    // included); the published route then rejoins from wherever it got to.
    const auto gradientWhileWaiting = [&]() {
        int gx = 0, gy = 0; bool stopWork = false;
        if (!routing().pathfindMaterial(unit.entityRandom, unit.owner->teamNumber, unit.activeMaterialClaim->material, unit.movementAccess(),
                unit.posX, unit.posY, &gx, &gy, &stopWork, nullptr, false) || (!gx && !gy)) return false;
        *dx = gx; *dy = gy; return true;
    };
    const auto request = [&](ActiveRouteRuntime::State& state, bool avoidUnits = false) {
        if (state.pending) return false;
        // Bounded recovery limits how often one worker may resubmit failed
        // routes; without admission the request is retried next decision.
        const auto selection = beginClaimRecoverySelection(unit);
        if (!runtime.pipeline.admissionAvailable() || !selection.mayRoute) {
            state.awaitingAdmission = state.route.status != RouteStatus::Found || state.route.cells.empty();
            return false;
        }
        auto& recovery = runtime.recovery.at(owner);
        if (recovery.failureStreak) recovery.admit(game->stepCounter);
        state.awaitingAdmission = false;
        using C = SimulationSnapshot::Component;
        const auto mask = SimulationSnapshot::bit(C::Catalogs) | SimulationSnapshot::bit(C::Terrain)
            | SimulationSnapshot::bit(C::Resources) | SimulationSnapshot::bit(C::Occupancy) | SimulationSnapshot::bit(C::Areas);
        // A traffic replan plans around the units sampled now (FrozenUnits);
        // ordinary routes ignore moving units, which will have moved on.
        RouteInput input{game->observation().project(mask), avoidUnits ? traffic : profileOf(unit), current, target};
        input.maximumExpansions = Policy::RouteMaximumExpansions;
        state.identity = runtime.pipeline.submit(owner, unit.materialClaimTask, game->stepCounter, std::move(input));
        state.due = Uint64(game->stepCounter) + 1; state.pending = true;
        return true;
    };
    if (found == runtime.states.end()) {
        if (runtime.states.size() >= MaximumStates) return ResourceClaims::StepResult::Pending;
        ActiveRouteRuntime::State state;
        state.target = target; state.start = current; state.pending = false; state.cacheRouteDigest();
        found = runtime.states.emplace(owner, std::move(state)).first;
        if (!request(found->second)) {
            runtime.states.erase(found);
            return gradientWhileWaiting() ? ResourceClaims::StepResult::Ready : ResourceClaims::StepResult::Pending;
        }
    }
    auto& state = found->second;
    const auto waiting = [&]() {
        if (!state.pending && !state.awaitingAdmission) return ResourceClaims::StepResult::Failed;
        return gradientWhileWaiting() ? ResourceClaims::StepResult::Ready : ResourceClaims::StepResult::Pending;
    };
    // Drop the route and plan a new one from the current cell.
    const auto replan = [&](bool avoidUnits = false) {
        state.route = {}; state.cacheRouteDigest(); state.cursor = 0; state.reanchor = false;
        request(state, avoidUnits);
        return waiting();
    };
    if (state.awaitingAdmission) request(state);
    // A completed failed route resolves here; recovery decides whether the
    // claim is released (see Unit::handleActiveMaterialMovement).
    if (state.route.status != RouteStatus::Found) return waiting();
    if (state.reanchor) {
        state.reanchor = false;
        if (current != state.start) {
            bool joined = false;
            const auto limit = std::min<std::size_t>(8, state.route.cells.size());
            // Prefer an exact prefix stance, then a single legal rejoin edge.
            for (unsigned i=0;i<limit;++i) if (state.route.cells[i]==current) {state.cursor=i+1;joined=true;break;}
            if (!joined) for (unsigned i=0;i<limit;++i) {
                const auto cell=state.route.cells[i];
                if (warpDistMax(unit.posX,unit.posY,cell%getW(),cell/getW())==1 && geometry(cell)) {
                    state.cursor=i; joined=true; break;
                }
            }
            if (!joined) return replan();
        }
    }
    // Consume a step only after the unit has actually moved. A blocked attempt
    // therefore retries the same step, without skipping cells in its path.
    while (state.cursor < state.route.cells.size() && state.route.cells[state.cursor] == current) ++state.cursor;
    if (state.cursor < state.route.cells.size() &&
        current != (state.cursor ? state.route.cells[state.cursor-1] : state.start)) {
        // After a traffic sidestep, rejoin at the furthest window cell that is
        // the current stance or one legal step away from it.
        const auto limit = std::min<std::size_t>(state.cursor + 8, state.route.cells.size());
        for (auto j = limit; j-- > state.cursor;) {
            const auto cell = state.route.cells[j];
            if (cell == current) {state.cursor = unsigned(j + 1); break;}
            if (warpDistMax(unit.posX,unit.posY,cell%getW(),cell/getW())==1) {state.cursor = unsigned(j); break;}
        }
    }
    const auto anchor = state.cursor ? state.route.cells[state.cursor-1] : state.start;
    const bool adjacentRejoin = state.cursor < state.route.cells.size() &&
        warpDistMax(unit.posX,unit.posY,state.route.cells[state.cursor]%getW(),state.route.cells[state.cursor]/getW())==1;
    if ((current != anchor && !adjacentRejoin) || state.cursor == state.route.cells.size()) return replan();
    const auto next = state.route.cells[state.cursor];
    if (!geometry(next)) return replan();
    if (!routePassable(live,next,traffic)) {
        // Blocked by traffic: step to a free legal neighbour that keeps route
        // progress (on, or adjacent to, a later cell of the 8-cell window,
        // furthest first) instead of queueing behind the blocker.
        const auto limit = std::min<std::size_t>(state.cursor + 8, state.route.cells.size());
        std::size_t best = 0; int bx = 0, by = 0;
        for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
            if (!ox && !oy) continue;
            const int cx = (unit.posX + ox + getW()) % getW(), cy = (unit.posY + oy + getH()) % getH();
            const auto cell = Uint32(coordToIndex(cx, cy));
            if (cell == next || !geometry(cell) || !routePassable(live, cell, traffic)) continue;
            for (auto j = limit; j-- > std::size_t(state.cursor) + 1;) {
                const auto later = state.route.cells[j];
                if (later == cell || warpDistMax(cx, cy, later % getW(), later / getW()) == 1) {
                    if (j > best) {best = j; bx = ox; by = oy;}
                    break;
                }
            }
        }
        if (best) {
            state.trafficWaits = 0;
            *dx = bx; *dy = by; return ResourceClaims::StepResult::Ready;
        }
        if (++state.trafficWaits >= Policy::TrafficWaitLimit) {
            state.trafficWaits = 0;
            return replan(true);
        }
        return ResourceClaims::StepResult::Pending;
    }
    state.trafficWaits = 0;
    int sx=int(next%getW())-unit.posX, sy=int(next/getW())-unit.posY;
    if (sx>1)sx-=getW();else if(sx< -1)sx+=getW();
    if (sy>1)sy-=getH();else if(sy< -1)sy+=getH();
    if (std::abs(sx)>1 || std::abs(sy)>1 || (!sx&&!sy)) return replan();
    *dx=sx;*dy=sy;return ResourceClaims::StepResult::Ready;
}

bool Map::claimRouteComputing(const Unit& unit) const
{
    if (!materialClaimRoutes) return true;
    const auto found = materialClaimRoutes->states.find(ResourceClaims::unitOwner(unit.gid, unit.scriptIdentity));
    return found == materialClaimRoutes->states.end() || found->second.pending || found->second.awaitingAdmission;
}

void Map::cancelClaimRoute(ResourceClaims::Owner owner)
{
    if (!materialClaimRoutes) return;
    materialClaimRoutes->pipeline.cancel(owner);
    if (materialClaimRoutes->states.erase(owner)) claimRuntimeChanged();
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
        auto& state=found->second;
        claimRuntimeChanged();
        state.pending = false;
        // A failed replan keeps a still usable earlier route; recovery
        // accounting sees the failure only once that route is gone too.
        if (output.route.status==ResourceClaims::RouteStatus::Found || state.route.status!=ResourceClaims::RouteStatus::Found) {
            state.start=output.start; state.cursor=0; state.reanchor=true;
            state.route=std::move(output.route); state.cacheRouteDigest();
        }
    }
}

bool Map::claimSaveAudit=std::getenv("GLOB2_CLAIM_AUDIT")!=nullptr;

void Map::saveActiveClaimRoutes(GAGCore::OutputStream* stream) const
{
    using namespace ResourceClaims;
    // Opt-in (tests set GLOB2_CLAIM_AUDIT): fail here, where an invariant hole
    // is saved, rather than later as a save that cannot load.
    if (claimSaveAudit) auditActiveMaterialClaims();
    stream->writeEnterSection("activeMaterialClaims");
    stream->writeUint8(resourceClaimsEnabled, "enabled");
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
        saveRoute(stream, state.route);
        stream->writeUint8(state.reanchor, "reanchor");
        stream->writeUint8(state.awaitingAdmission, "awaitingAdmission");
        stream->writeUint32(state.trafficWaits, "trafficWaits");
        stream->writeLeaveSection();
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
    stream->writeEnterSection("claimRecovery");
    stream->writeUint32(materialClaimRoutes?materialClaimRoutes->recovery.size():0,"count");
    unsigned recoveryIndex=0;
    if(materialClaimRoutes)for(const auto& [owner,value]:materialClaimRoutes->recovery) {
        stream->writeEnterSection(recoveryIndex++);write64(stream,owner);
        auto copy=value;recoveryFields(copy,[&](auto& field,const char* name){stream->writeUint32(field,name);});
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
    stream->writeLeaveSection();
}

void Map::loadActiveClaimRoutes(GAGCore::InputStream* stream, Sint32 versionMinor)
{
    using namespace ResourceClaims;
    materialClaimRoutes.reset(); activeClaimRules.reset(); resourceClaimsEnabled = false; claimSetupTick.reset();
    claimRuntimeChanged();
    // Saves before format 213 have no claims and continue without them.
    if (versionMinor < FILE_FORMAT_VERSION_RESOURCE_CLAIMS) return;
    GAGCore::BinaryInputStream::CheckedReads checked(stream);
    stream->readEnterSection("activeMaterialClaims"); const auto enabled = stream->readUint8("enabled");
    if (enabled > 1 || (enabled && !routing().materialDestinationsEnabled()))
        throw std::runtime_error("Invalid saved resource claim setting");
    resourceClaimsEnabled = enabled;
    stream->readEnterSection("sequence"); const auto sequence = read64(stream); stream->readLeaveSection();
    stream->readEnterSection("sealed"); const auto hasSealed = stream->readUint8("has"); const auto sealed = read64(stream); stream->readLeaveSection();
    if (hasSealed > 1 || bool(sequence) != bool(hasSealed) || (!hasSealed && sealed) || (hasSealed && sealed > game->stepCounter)) throw std::runtime_error("Invalid saved route admission boundary");
    if (!enabled && sequence) throw std::runtime_error("Disabled active route sequence");
    stream->readEnterSection("rulesEpoch"); const auto hasRules = stream->readUint8("has");
    if (hasRules > 1 || bool(hasRules) != bool(enabled)) throw std::runtime_error("Invalid active claim rules epoch");
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
    if (count > MaximumStates || (count && !enabled)) throw std::runtime_error("Invalid active route state count");
    if (enabled) materialClaimRoutes = std::make_unique<ActiveRouteRuntime>(compute);
    stream->readEnterSection("records");
    for (unsigned i = 0; i < count; ++i) {
        stream->readEnterSection(i); ActiveRouteRuntime::State state; state.identity = loadIdentity(stream);
        state.start = stream->readUint32("start"); state.target = stream->readUint32("target");
        state.cursor = stream->readUint32("cursor"); const auto pending = stream->readUint8("pending");
        state.pending = pending;
        stream->readEnterSection("due"); state.due = read64(stream); stream->readLeaveSection();
        if (state.start >= cellCount() || state.target >= cellCount() || pending > 1 || !state.due ||
            state.due > Uint64(game->stepCounter) + 1) throw std::runtime_error("Invalid active route state");
        state.route = loadRoute(stream, *this, state.start); state.cacheRouteDigest();
        const auto reanchor = stream->readUint8("reanchor"), awaiting = stream->readUint8("awaitingAdmission");
        state.trafficWaits = stream->readUint32("trafficWaits");
        if (reanchor > 1 || awaiting > 1 || (awaiting && state.pending) || state.trafficWaits >= Policy::TrafficWaitLimit)
            throw std::runtime_error("Invalid active route progress");
        state.reanchor = reanchor; state.awaitingAdmission = awaiting;
        if (!state.identity.task || state.identity.request > sequence || state.cursor > state.route.cells.size() ||
            !endsAtTarget(*this, state.route, state.start, state.target) ||
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
        if (!pendingOwners.insert(output.identity.unit).second || found == materialClaimRoutes->states.end() || !found->second.pending
            || found->second.identity != output.identity || found->second.target != output.target || found->second.due != output.due)
            throw std::runtime_error("Mismatched saved active route");
        output.route = loadRoute(stream, *this, output.start);
        if (!endsAtTarget(*this, output.route, output.start, output.target)) throw std::runtime_error("Invalid active route terminal");
        pending.push_back(std::move(output)); stream->readLeaveSection();
    }
    stream->readLeaveSection();
    stream->readEnterSection("claimRecovery");
    const auto recoveryCount = stream->readUint32("count");
    if (recoveryCount > MaximumStates || (recoveryCount && !enabled)) throw std::runtime_error("Invalid recovery record count");
    for (unsigned i = 0; i < recoveryCount; ++i) {
        stream->readEnterSection(i); const auto owner = read64(stream); ClaimRecovery value;
        recoveryFields(value,[&](auto& field,const char* name){
            const auto next=stream->readUint32(name);using T=std::remove_reference_t<decltype(field)>;
            if(next>std::numeric_limits<T>::max())throw std::runtime_error("Invalid recovery field width");
            field=T(next);
        });stream->readLeaveSection();
        const auto team=Unit::GIDtoTeam(Uint16(owner)),slot=Unit::GIDtoID(Uint16(owner));
        const Unit* unit=team<Team::MAX_COUNT && game->teams[team]?game->teams[team]->myUnits[slot]:nullptr;
        if(!unit || unit->scriptIdentity!=Uint32(owner>>16) || value.replacement>=ClaimRecovery::CacheEntries ||
            value.attempts>ClaimRecovery::MaxAttempts || (!value.windowKnown && value.attempts) ||
            (value.pinnedAccess!=ClaimRecovery::NoTile && value.pinnedAccess>=cellCount()) ||
            value.nextProbe>Pattern::Entries.size() || value.movement>=MOVEMENT_CLASS_COUNT || value.material>=MaterialSlotCount)
            throw std::runtime_error("Invalid recovery owner or context");
        for(const auto& e:value.entries)if(e.ttl>ClaimRecovery::TtlTicks || (e.access!=ClaimRecovery::NoTile && e.access>=cellCount()))
            throw std::runtime_error("Invalid recovery exclusion");
        if(!materialClaimRoutes->recovery.emplace(owner,value).second)throw std::runtime_error("Duplicate recovery owner");
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

// One layout for both claim checksums. Map::checkSum (activeClaimRouteChecksum)
// hashes every route cell, plus the pending route outputs on its heavy pass;
// the per-tick snapshot checksum (activeClaimSnapshotChecksum) hashes each
// route's cached digest and never joins pending work.
Uint32 Map::claimRuntimeChecksum(bool hashRouteCells, bool includePendingOutputs) const
{
    const auto pending = hashRouteCells && includePendingOutputs && materialClaimRoutes ? materialClaimRoutes->pipeline.exportPending() : std::vector<ResourceClaims::RoutePublication>{};
    Uint32 hash = 2166136261u;
    const auto mix = [&](Uint64 value) { hash = (hash ^ Uint32(value)) * 16777619u; hash = (hash ^ Uint32(value >> 32)) * 16777619u; };
    const auto route = [&](const ResourceClaims::Route& value) {
        mix(unsigned(value.status)); mix(value.cost); mix(value.expansions); mix(value.cells.size());
        for (auto cell : value.cells) mix(cell);
    };
    mix(activeClaimRules.has_value());
    if (activeClaimRules) for (auto rule : *activeClaimRules) mix(rule);
    mix(materialClaimRoutes ? materialClaimRoutes->pipeline.requestSequence() : 0);
    const auto sealed = materialClaimRoutes ? materialClaimRoutes->pipeline.sealedSubmissionTick() : std::optional<Uint64>{};
    mix(sealed.has_value()); mix(sealed.value_or(0));
    mix(materialClaimRoutes ? materialClaimRoutes->states.size() : 0);
    if (!materialClaimRoutes) { mix(0); return hash; }
    for (const auto& [owner, state] : materialClaimRoutes->states) {
        mix(owner); mix(state.identity.task); mix(state.identity.request); mix(state.start); mix(state.target);
        mix(state.cursor); mix(state.due); mix(state.pending);
        if (hashRouteCells) route(state.route); else mix(state.routeDigest);
        mix(state.reanchor); mix(state.trafficWaits);
    }
    for (const auto& output : pending) {
        mix(output.identity.unit); mix(output.identity.task); mix(output.identity.request); mix(output.due);
        route(output.route); mix(output.start); mix(output.target);
    }
    mix(materialClaimRoutes->recovery.size());
    for (const auto& [owner,value]:materialClaimRoutes->recovery) {
        mix(owner); recoveryValues(value,[&](auto field){mix(field);});
    }
    for (const auto& [owner,state]:materialClaimRoutes->states) mix(state.awaitingAdmission);
    return hash;
}

Uint32 Map::activeClaimRouteChecksum(bool includePending) const { return claimRuntimeChecksum(true, includePending); }
Uint32 Map::activeClaimSnapshotChecksum() const
{
    auto& cache = claimSnapshotChecksumCache;
    const auto sequence = materialClaimRoutes ? materialClaimRoutes->pipeline.requestSequence() : 0;
    const auto sealed = materialClaimRoutes ? materialClaimRoutes->pipeline.sealedSubmissionTick() : std::optional<Uint64>{};
    if (cache.valid && cache.sequence == sequence && cache.sealed == sealed && cache.rules == activeClaimRules) return cache.value;
    cache = {true, claimRuntimeChecksum(false, false), sequence, sealed, activeClaimRules};
    return cache.value;
}
