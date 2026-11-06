// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "Game.h"
#include "Unit.h"
#include "Team.h"
#include "Version.h"
#include "ActiveRouteRuntime.h"
#include "ClaimPolicyConfig.h"
#include "ResourceClaimProbe.h"
#include "gradient/DirectFlightDirection.h"
#include "online/Sha256.h"
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
template<class F> void recoveryFields(ResourceClaims::ClaimRecovery& r,F&& f)
{
    f(r.nextProbe,"nextProbe");f(r.pinnedAccess,"pinnedAccess");f(r.windowStart,"windowStart");
    f(r.backoffStart,"backoffStart");f(r.failureStreak,"failureStreak");
    f(r.material,"material");f(r.movement,"movement");f(r.replacement,"replacement");f(r.attempts,"attempts");
    f(r.recovering,"recovering");f(r.windowKnown,"windowKnown");f(r.backoffKnown,"backoffKnown");
    for(unsigned i=0;i<r.entries.size();++i) {
        // Indexed keys are stable in both binary and text streams.
        auto& e=r.entries[i];const auto key=std::to_string(i);
        f(e.access,("access"+key).c_str());f(e.tick,("tick"+key).c_str());f(e.ttl,("ttl"+key).c_str());
    }
}
RouteProfile profileOf(const Unit& unit)
{ return {accessClass(unit.movementAccess()), unit.owner->me, unit.gid, OccupancyPolicy::Static}; }
bool endsAtTarget(const Map& map, const Route& route, Uint32 start, Uint32 target, unsigned radius=1)
{
    if (route.status != RouteStatus::Found) return true;
    const auto end = route.cells.empty() ? start : route.cells.back();
    const auto dx = unsigned(std::abs(int(end % map.getW()) - int(target % map.getW())));
    const auto dy = unsigned(std::abs(int(end / map.getW()) - int(target / map.getW())));
    return std::min(dx, unsigned(map.getW()) - dx) <= radius && std::min(dy, unsigned(map.getH()) - dy) <= radius;
}
}

ResourceClaims::ClaimRecoverySelection Map::beginClaimRecoverySelection(const Unit& unit)
{
    using namespace ResourceClaims;
    if(activeClaimPolicy!=ActiveClaimPolicy::Experimental || !materialClaimPolicyConfig().usesRecovery())return {};
    if(!materialClaimRoutes)materialClaimRoutes=std::make_unique<ActiveRouteRuntime>(compute);
    auto& recovery=materialClaimRoutes->recovery[unitOwner(unit.gid,unit.scriptIdentity)];
    recovery.setContext(unit.destinationPurpose,accessClass(unit.movementAccess()));
    return {&recovery,game->stepCounter,recovery.recovering?recovery.nextProbe:0,recovery.recovering,
        !recovery.failureStreak || recovery.allowsAttempt(game->stepCounter)};
}
void Map::finishClaimRecoverySelection(const Unit& unit,unsigned probe,unsigned size)
{
    const auto view=beginClaimRecoverySelection(unit);if(!view.state)return;
    auto& recovery=materialClaimRoutes->recovery.at(ResourceClaims::unitOwner(unit.gid,unit.scriptIdentity));
    if(probe==size)recovery.exhausted();else recovery.selected(probe,size);
}
unsigned Map::recordClaimFailure(const Unit& unit,ResourceClaims::ClaimFailureKind kind)
{
    using namespace ResourceClaims;
    const auto view=beginClaimRecoverySelection(unit);if(!view.state)return 0;
    auto& recovery=materialClaimRoutes->recovery.at(unitOwner(unit.gid,unit.scriptIdentity));
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
    if(found!=materialClaimRoutes->recovery.end())found->second.harvested();
}
void Map::forgetClaimRecovery(ResourceClaims::Owner owner)
{if(materialClaimRoutes)materialClaimRoutes->recovery.erase(owner);}

ResourceClaims::StepResult Map::nextClaimRouteStep(Unit& unit, int* dx, int* dy)
{
    using namespace ResourceClaims;
    if (!activeClaimsEnabled() || !unit.activeMaterialClaim || !game || !cellCount()) return ResourceClaims::StepResult::Failed;
    const auto owner = unitOwner(unit.gid, unit.scriptIdentity);
    const auto* observer=resourceClaimProbe();
    const auto record=[&](Probe::LiveEvent event){if(observer)observer->routeEvent(owner,unit.materialClaimTask,game->stepCounter,event);};
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
    if (activeClaimPolicy == ActiveClaimPolicy::Experimental) {
        const auto& config = materialClaimPolicyConfig();
        const auto ordinal = unit.materialClaimPolicyState.decisionOrdinal;
        const auto live = stateView();
        auto traffic = profileOf(unit); traffic.occupancy = OccupancyPolicy::FrozenUnits;
        const auto geometry = [&](Uint32 cell) {
            return gradient_kernel::terrainAllowsMovement(live.terrainProperties(cell), traffic.movement)
                && (traffic.movement == AIR_MOVEMENT_CLASS ? !MapState::resourceBlocksAir(live, cell)
                    : !(live.areas[cell].forbidden & traffic.teamMask) && live.occupancy[cell].building == 0xffff
                        && !MapState::resourceBlocksGround(live, cell));
        };
        // pending_step=gradient: the ordinary natural-material gradient step,
        // exactly as claims-off movement takes it (demand, outcome, refresh).
        const auto gradientWhileWaiting = [&]() {
            if (config.pendingStep != ClaimPolicyConfig::PendingStep::Gradient) return false;
            int gx = 0, gy = 0; bool stopWork = false;
            if (!pathfindMaterial(unit.entityRandom, unit.owner->teamNumber, unit.activeMaterialClaim->material, unit.movementAccess(),
                    unit.posX, unit.posY, &gx, &gy, &stopWork, nullptr, false) || (!gx && !gy)) return false;
            if (observer) observer->policyExtension(Probe::PolicyExtension::PendingGradientStep);
            *dx = gx; *dy = gy; return true;
        };
        const auto request = [&](ActiveRouteRuntime::State& state, bool direct) {
            if (state.pending) return false;
            if(config.usesRecovery()) {
                const auto selection=beginClaimRecoverySelection(unit);
                if(!runtime.pipeline.admissionAvailable() || !selection.mayRoute) {state.awaitingAdmission=state.route.status!=RouteStatus::Found || state.route.cells.empty();return false;}
                auto& recovery=runtime.recovery.at(owner);
                if(recovery.failureStreak)recovery.admit(game->stepCounter);
            } else if(!runtime.pipeline.admissionAvailable())return false;
            state.awaitingAdmission=false;
            if(direct)state.probeFinished=true;
            Uint32 waypoint = target; unsigned radius = 1;
            if (!state.probeFinished && config.routeTarget == ClaimPolicyConfig::RouteTarget::ProbeThenGradient &&
                unit.materialClaimPolicyState.probe < cellCount() && unit.materialClaimPolicyState.probe != target) {
                waypoint = unit.materialClaimPolicyState.probe; radius = 0;
            }
            using C = SimulationSnapshot::Component;
            const auto mask = SimulationSnapshot::bit(C::Catalogs) | SimulationSnapshot::bit(C::Terrain)
                | SimulationSnapshot::bit(C::Resources) | SimulationSnapshot::bit(C::Occupancy) | SimulationSnapshot::bit(C::Areas);
            RouteInput input{game->observation().project(mask), profileOf(unit), current, waypoint};
            input.radius = radius; input.maximumExpansions = config.maximumExpansions;
            input.presentTerrainHeuristic = true; input.diagnostics = observer!=nullptr;
            const auto reason=!state.identity.request?Probe::LiveEvent::RouteInitial:direct?Probe::LiveEvent::RouteRepair:Probe::LiveEvent::RouteRefresh;
            state.identity = runtime.pipeline.submit(owner, unit.materialClaimTask, game->stepCounter, std::move(input));
            record(reason);
            state.pendingStart = current; state.pendingTarget = waypoint; state.pendingRadius = radius;
            state.due = Uint64(game->stepCounter) + 1; state.pending = true; state.lastRefreshOrdinal = ordinal;
            return true;
        };
        if (found == runtime.states.end()) {
            if (runtime.states.size() >= MaximumStates) return config.usesRecovery()?ResourceClaims::StepResult::Pending:ResourceClaims::StepResult::Failed;
            ActiveRouteRuntime::State state;
            state.target = target; state.start = current; state.pending = false;state.cacheRouteDigest();
            found = runtime.states.emplace(owner, std::move(state)).first;
            if (!request(found->second, false)) {
                runtime.states.erase(found);
                return gradientWhileWaiting() ? ResourceClaims::StepResult::Ready : ResourceClaims::StepResult::Pending;
            }
        }
        auto& state = found->second;
        const auto waiting = [&]() {
            if (!state.pending) {
                if (!state.awaitingAdmission) return ResourceClaims::StepResult::Failed;
                return gradientWhileWaiting() ? ResourceClaims::StepResult::Ready : ResourceClaims::StepResult::Pending;
            }
            record(Probe::LiveEvent::PendingPath);
            if (config.pendingStep == ClaimPolicyConfig::PendingStep::DirectHeading) {
                const auto step = direct_flight::select(unit.posX, unit.posY,
                    state.pendingTarget % getW(), state.pendingTarget / getW(), getW(), getH(),
                    [&](int x,int y) {return routePassable(live, coordToIndex(x,y), traffic);});
                if (step.dx || step.dy) {*dx=step.dx; *dy=step.dy; return ResourceClaims::StepResult::Ready;}
            }
            if (gradientWhileWaiting()) return ResourceClaims::StepResult::Ready;
            return ResourceClaims::StepResult::Pending;
        };
        if(state.awaitingAdmission)request(state,true);
        // A completed failed route resolves before cadence can issue another
        // request: J=1 must not pin an unreachable reservation indefinitely.
        if (state.route.status != RouteStatus::Found) {
            if (!state.pending && state.pendingRadius==0) {record(Probe::LiveEvent::RouteProbeFallback);request(state,true);}
            return waiting();
        }
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
                if(joined)record(Probe::LiveEvent::ReanchorJoined);
                if (!joined) {record(Probe::LiveEvent::ReanchorFailed);state.route={};state.cacheRouteDigest(); state.cursor=0; state.reanchor=false; state.followingGradient=false; request(state,true); return waiting();}
            }
        }
        while (state.cursor < state.route.cells.size() && state.route.cells[state.cursor] == current) ++state.cursor;
        if (state.followingGradient || (state.pathRadius == 0 && current == state.pathTarget)) {
            state.followingGradient = true; state.probeFinished=true;
            const auto tail = nextClaimGradientStep(unit,dx,dy);
            if (tail != ResourceClaims::StepResult::Failed) return tail;
            record(Probe::LiveEvent::ProbeTailFailed);record(Probe::LiveEvent::RouteProbeFallback);
            state.followingGradient=false; state.route={};state.cacheRouteDigest(); state.cursor=0; state.reanchor=false; state.followingGradient=false; request(state,true); return waiting();
        }
        // A refresh never discards a still usable route while its worker runs.
        if (!state.pending && !state.followingGradient && config.routeRefreshJ && ordinal - state.lastRefreshOrdinal >= config.routeRefreshJ)
            request(state, false);
        const auto anchor = state.cursor ? state.route.cells[state.cursor-1] : state.start;
        const bool adjacentRejoin = state.cursor < state.route.cells.size() &&
            warpDistMax(unit.posX,unit.posY,state.route.cells[state.cursor]%getW(),state.route.cells[state.cursor]/getW())==1;
        if ((current != anchor && !adjacentRejoin) || state.cursor == state.route.cells.size()) {
            state.route={};state.cacheRouteDigest(); state.cursor=0; state.reanchor=false; state.followingGradient=false; request(state,true); return waiting();
        }
        const auto next = state.route.cells[state.cursor];
        if (!geometry(next)) {record(Probe::LiveEvent::GeometryContradiction);state.route={};state.cacheRouteDigest(); state.cursor=0; state.reanchor=false; state.followingGradient=false; request(state,true); return waiting();}
        if (!routePassable(live,next,traffic)) {record(Probe::LiveEvent::TrafficWait);return ResourceClaims::StepResult::Pending;}
        int sx=int(next%getW())-unit.posX, sy=int(next/getW())-unit.posY;
        if (sx>1)sx-=getW();else if(sx< -1)sx+=getW();
        if (sy>1)sy-=getH();else if(sy< -1)sy+=getH();
        if (std::abs(sx)>1 || std::abs(sy)>1 || (!sx&&!sy)) {state.route={};state.cacheRouteDigest(); state.cursor=0; state.reanchor=false; state.followingGradient=false;request(state,true);return waiting();}
        *dx=sx;*dy=sy;return ResourceClaims::StepResult::Ready;
    }
    if (found == runtime.states.end()) {
        if (runtime.states.size() >= MaximumStates || !runtime.pipeline.admissionAvailable()) return ResourceClaims::StepResult::Failed;
        // Retain only route inputs. In particular, a pending route must not
        // keep old entity records and every published gradient plane alive.
        using C = SimulationSnapshot::Component;
        const auto mask = SimulationSnapshot::bit(C::Catalogs) | SimulationSnapshot::bit(C::Terrain)
            | SimulationSnapshot::bit(C::Resources) | SimulationSnapshot::bit(C::Occupancy) | SimulationSnapshot::bit(C::Areas);
        RouteInput input{game->observation().project(mask), profileOf(unit), current, target};
        input.diagnostics = observer!=nullptr;
        const auto identity = runtime.pipeline.submit(owner, unit.materialClaimTask, game->stepCounter, std::move(input));
        record(Probe::LiveEvent::RouteInitial);record(Probe::LiveEvent::PendingPath);
        auto inserted=runtime.states.emplace(owner, ActiveRouteRuntime::State{identity, current, target, 0, Uint64(game->stepCounter) + 1});
        inserted.first->second.cacheRouteDigest();
        return ResourceClaims::StepResult::Pending;
    }
    auto& state = found->second;
    if (state.pending) {record(Probe::LiveEvent::PendingPath);return ResourceClaims::StepResult::Pending;}
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
        record(Probe::LiveEvent::GeometryContradiction);
        cancelClaimRoute(owner); return ResourceClaims::StepResult::Failed;
    }
    auto traffic = profile; traffic.occupancy = OccupancyPolicy::FrozenUnits;
    if (!routePassable(live, next, traffic)) {record(Probe::LiveEvent::TrafficWait);return ResourceClaims::StepResult::Pending;}
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
        auto& state=found->second;
        state.pending = false;
        if (activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental) {
            // A refresh submitted before reaching the probe can become obsolete
            // while the old path enters its gradient tail. Retire its scheduled
            // request without pulling the unit back to an already reached probe.
            if (state.followingGradient && state.pendingRadius==0) {
                if(const auto* observer=resourceClaimProbe())observer->routeEvent(output.identity.unit,output.identity.task,game->stepCounter,ResourceClaims::Probe::LiveEvent::ObsoleteProbe);
                continue;
            }
            if (output.route.status==ResourceClaims::RouteStatus::Found || state.route.status!=ResourceClaims::RouteStatus::Found) {
                state.start=output.start; state.pathTarget=output.target; state.pathRadius=state.pendingRadius;
                state.cursor=0; state.reanchor=true; state.followingGradient=false; state.route=std::move(output.route);state.cacheRouteDigest();
            }
        } else {state.route = std::move(output.route);state.cacheRouteDigest();}
    }
    if(const auto* observer=resourceClaimProbe())observer->routeWork(runtime.pipeline.takeDiagnostics());
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
    if (VERSION_MINOR >= PolicyConfigFormat) {
        stream->writeEnterSection("materialClaimPolicyConfig");
        const bool configured=activeClaimPolicy==ActiveClaimPolicy::Experimental;
        stream->writeUint8(configured,"has");
        if (configured) {
            if (!materialClaimConfiguration) throw std::logic_error("experimental claim policy has no configuration");
            const auto json=materialClaimConfiguration->canonicalJson();
            if (json.size()>4096) throw std::length_error("claim policy configuration exceeds save allowance");
            stream->writeUint32(json.size(),"bytes"); stream->write(json.data(),json.size(),"json");
        }
        stream->writeLeaveSection();
    }
    stream->writeUint32(materialClaimRoutes ? materialClaimRoutes->states.size() : 0, "states");
    stream->writeEnterSection("records");
    unsigned i = 0;
    if (materialClaimRoutes) for (const auto& [owner, state] : materialClaimRoutes->states) {
        stream->writeEnterSection(i++); saveIdentity(stream, state.identity);
        stream->writeUint32(state.start, "start"); stream->writeUint32(state.target, "target");
        stream->writeUint32(state.cursor, "cursor"); stream->writeUint8(state.pending, "pending");
        stream->writeEnterSection("due"); write64(stream, state.due); stream->writeLeaveSection();
        saveRoute(stream, state.route);
        if(activeClaimPolicy==ActiveClaimPolicy::Experimental) {
            stream->writeEnterSection("experimentalRoute");
            stream->writeUint32(state.pathTarget,"pathTarget"); stream->writeUint8(state.pathRadius,"pathRadius");
            stream->writeUint32(state.pendingStart,"pendingStart"); stream->writeUint32(state.pendingTarget,"pendingTarget");
            stream->writeUint8(state.pendingRadius,"pendingRadius");
            stream->writeEnterSection("refreshOrdinal");write64(stream,state.lastRefreshOrdinal);stream->writeLeaveSection();
            stream->writeUint8(state.reanchor,"reanchor"); stream->writeUint8(state.followingGradient,"followingGradient");
            stream->writeUint8(state.probeFinished,"probeFinished");
            stream->writeLeaveSection();
        }
        stream->writeEnterSection("claimRecoveryRoute");
        stream->writeUint8(state.awaitingAdmission,"awaitingAdmission");stream->writeLeaveSection();
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
    materialClaimRoutes.reset(); activeClaimRules.reset(); materialClaimConfiguration.reset();
    materialClaimConfigurationDigest={}; activeClaimPolicy = ActiveClaimPolicy::Disabled;
    if (versionMinor < ActiveClaimsFormat) return;
    GAGCore::BinaryInputStream::CheckedReads checked(stream);
    stream->readEnterSection("activeMaterialClaims"); const auto policy = stream->readUint8("policy");
    const auto maximumPolicy=versionMinor>=PolicyConfigFormat ? ActiveClaimPolicy::Experimental : ActiveClaimPolicy::PrototypeStationaryV1;
    if (policy > unsigned(maximumPolicy) || (policy && !materialDestinationsEnabled()))
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
    if (versionMinor>=PolicyConfigFormat) {
        stream->readEnterSection("materialClaimPolicyConfig"); const auto has=stream->readUint8("has");
        if (has>1 || bool(has)!=(activeClaimPolicy==ActiveClaimPolicy::Experimental))
            throw std::runtime_error("saved claim configuration does not match policy");
        if (has) {
            const auto length=stream->readUint32("bytes");
            if (!length || length>4096) throw std::runtime_error("invalid saved claim configuration length");
            std::string json(length,'\0'); stream->read(json.data(),length,"json");
            auto config=ClaimPolicyConfig::parse(json);
            materialClaimConfigurationDigest=Online::Sha256::digest(config.canonicalJson());
            materialClaimConfiguration=std::make_unique<const ClaimPolicyConfig>(std::move(config));
        }
        stream->readLeaveSection();
    }
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
        state.route = loadRoute(stream, *this, state.start);state.cacheRouteDigest();
        const bool experimental=activeClaimPolicy==ActiveClaimPolicy::Experimental;
        state.pathTarget=state.target; state.pendingStart=state.start; state.pendingTarget=state.target;
        if(experimental) {
            stream->readEnterSection("experimentalRoute");
            state.pathTarget=stream->readUint32("pathTarget"); state.pathRadius=stream->readUint8("pathRadius");
            state.pendingStart=stream->readUint32("pendingStart"); state.pendingTarget=stream->readUint32("pendingTarget");
            state.pendingRadius=stream->readUint8("pendingRadius");
            stream->readEnterSection("refreshOrdinal");state.lastRefreshOrdinal=read64(stream);stream->readLeaveSection();
            const auto reanchor=stream->readUint8("reanchor"), tail=stream->readUint8("followingGradient");
            const auto finished=stream->readUint8("probeFinished");
            state.reanchor=reanchor;state.followingGradient=tail;state.probeFinished=finished;
            stream->readLeaveSection();
            if(state.pathTarget>=cellCount() || state.pendingStart>=cellCount() || state.pendingTarget>=cellCount() ||
                state.pathRadius>1 || state.pendingRadius>1 || reanchor>1 || tail>1 || finished>1 || (tail && !finished) ||
                (state.pendingRadius==1 && state.pendingTarget!=state.target) ||
                (state.route.status==RouteStatus::Found && state.pathRadius==1 && state.pathTarget!=state.target) ||
                ((state.pendingRadius==0 || state.pathRadius==0) && materialClaimPolicyConfig().routeTarget!=ClaimPolicyConfig::RouteTarget::ProbeThenGradient) ||
                (tail && (reanchor || state.pathRadius!=0 || state.route.status!=RouteStatus::Found || state.cursor!=state.route.cells.size())) ||
                !endsAtTarget(*this,state.route,state.start,state.pathTarget,state.pathRadius))
                throw std::runtime_error("Invalid experimental route state");
        }
        if(versionMinor>=ClaimRecoveryFormat) {
            stream->readEnterSection("claimRecoveryRoute");const auto awaiting=stream->readUint8("awaitingAdmission");stream->readLeaveSection();
            if(awaiting>1 || (awaiting && (!experimental || !materialClaimPolicyConfig().usesRecovery() || state.pending)))
                throw std::runtime_error("Invalid deferred recovery route");
            state.awaitingAdmission=awaiting;
        }
        if (!state.identity.task || state.identity.request > sequence || state.cursor > state.route.cells.size() ||
            (!experimental && ((!state.pending && !endsAtTarget(*this, state.route, state.start, state.target)) || (state.pending && (state.cursor || !state.route.cells.empty())))) ||
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
            (activeClaimPolicy==ActiveClaimPolicy::Experimental ?
                found->second.pendingStart != output.start || found->second.pendingTarget != output.target :
                found->second.start != output.start || found->second.target != output.target) || found->second.due != output.due)
            throw std::runtime_error("Mismatched saved active route");
        output.route = loadRoute(stream, *this, output.start);
        if (!endsAtTarget(*this, output.route, output.start, output.target, activeClaimPolicy==ActiveClaimPolicy::Experimental?found->second.pendingRadius:1)) throw std::runtime_error("Invalid active route terminal");
        pending.push_back(std::move(output)); stream->readLeaveSection();
    }
    stream->readLeaveSection();
    if(versionMinor>=ClaimRecoveryFormat) {
        stream->readEnterSection("claimRecovery");const auto count=stream->readUint32("count");
        if(count>MaximumStates || (count && (activeClaimPolicy!=ActiveClaimPolicy::Experimental || !materialClaimPolicyConfig().usesRecovery())))
            throw std::runtime_error("Invalid recovery record count");
        const auto maximumCursor=materialClaimPolicyConfig().sourceVersion>=4?materialClaimPolicyConfig().maximumRecoveryCandidateCount():65536u;
        for(unsigned i=0;i<count;++i) {
            stream->readEnterSection(i);const auto owner=read64(stream);ClaimRecovery value;
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
                value.nextProbe>maximumCursor || value.movement>=MOVEMENT_CLASS_COUNT || value.material>=MaterialSlotCount)
                throw std::runtime_error("Invalid recovery owner or context");
            for(const auto& e:value.entries)if(e.ttl>ClaimRecovery::TtlTicks || (e.access!=ClaimRecovery::NoTile && e.access>=cellCount()))
                throw std::runtime_error("Invalid recovery exclusion");
            if(!materialClaimRoutes->recovery.emplace(owner,value).second)throw std::runtime_error("Duplicate recovery owner");
        }
        stream->readLeaveSection();
    }
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
    if (activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental)
        for (auto byte:materialClaimConfigurationDigest) mix(byte);
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
            if(activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental) {
                mix(state.pathTarget);mix(state.pathRadius);mix(state.pendingStart);mix(state.pendingTarget);mix(state.pendingRadius);
                mix(state.lastRefreshOrdinal);mix(state.reanchor);mix(state.followingGradient);mix(state.probeFinished);
            }
        }
        for (const auto& output : pending) {
            mix(output.identity.unit); mix(output.identity.task); mix(output.identity.request); mix(output.due); route(output.route);
            if(activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental) {mix(output.start);mix(output.target);}
        }
    }
    if(activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental && materialClaimPolicyConfig().usesRecovery()) {
        mix(ResourceClaims::ClaimRecovery::SourceVersion);
        mix(materialClaimRoutes?materialClaimRoutes->recovery.size():0);
        if(materialClaimRoutes) {
            for(const auto& [owner,value]:materialClaimRoutes->recovery) {
                mix(owner);auto copy=value;recoveryFields(copy,[&](auto& field,const char*){mix(field);});
            }
            for(const auto& [owner,state]:materialClaimRoutes->states)mix(state.awaitingAdmission);
        }
    }
    return hash;
}

Uint32 Map::activeClaimSnapshotChecksum() const
{
    Uint32 hash = 2166136261u;
    const auto mix = [&](Uint64 value) { hash = (hash ^ Uint32(value)) * 16777619u; hash = (hash ^ Uint32(value >> 32)) * 16777619u; };
    mix(unsigned(activeClaimPolicy));
    if (activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental)
        for (auto byte:materialClaimConfigurationDigest) mix(byte);
    mix(activeClaimRules.has_value());
    if (activeClaimRules) for (auto rule : *activeClaimRules) mix(rule);
    mix(materialClaimRoutes ? materialClaimRoutes->pipeline.requestSequence() : 0);
    const auto sealed = materialClaimRoutes ? materialClaimRoutes->pipeline.sealedSubmissionTick() : std::optional<Uint64>{};
    mix(sealed.has_value()); mix(sealed.value_or(0));
    mix(materialClaimRoutes ? materialClaimRoutes->states.size() : 0);
    if (materialClaimRoutes) {
        for (const auto& [owner, state] : materialClaimRoutes->states) {
            mix(owner); mix(state.identity.task); mix(state.identity.request); mix(state.start); mix(state.target);
            mix(state.cursor); mix(state.due); mix(state.pending); mix(state.routeDigest);
            if(activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental) {
                mix(state.pathTarget);mix(state.pathRadius);mix(state.pendingStart);mix(state.pendingTarget);mix(state.pendingRadius);
                mix(state.lastRefreshOrdinal);mix(state.reanchor);mix(state.followingGradient);mix(state.probeFinished);
            }
        }
    }
    if(activeClaimPolicy==ResourceClaims::ActiveClaimPolicy::Experimental && materialClaimPolicyConfig().usesRecovery()) {
        mix(ResourceClaims::ClaimRecovery::SourceVersion);
        mix(materialClaimRoutes?materialClaimRoutes->recovery.size():0);
        if(materialClaimRoutes) {
            for(const auto& [owner,value]:materialClaimRoutes->recovery) {
                mix(owner);auto copy=value;recoveryFields(copy,[&](auto& field,const char*){mix(field);});
            }
            for(const auto& [owner,state]:materialClaimRoutes->states)mix(state.awaitingAdmission);
        }
    }
    return hash;
}
