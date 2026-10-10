// SPDX-License-Identifier: GPL-3.0-or-later
#include "SnapshotGradient.h"
#include "ComputeExecutor.h"
#include "SeedCells.h"
#include "MapInternal.h"
#include "Building.h"
#include "Unit.h"
#include "BuildingType.h"
#include "field/RuntimeTerrainGradient.h"
#include <array>
#include <bit>
#include <cstring>

namespace gradient_preparation
{
namespace {
void setSeedBit(MaterialSeedCache::Bits& bits, size_t i, bool value)
{
    const Uint64 bit=Uint64(1)<<(i%64);
    bits[i/64]=(bits[i/64]&~bit)|(value?bit:0);
}
template<class Fn> void visitSeedBits(const MaterialSeedCache::Bits& bits, Fn fn)
{
    for (size_t word=0; word<bits.size(); ++word)
        for (Uint64 remaining=bits[word]; remaining; remaining&=remaining-1)
            fn(word*64+std::countr_zero(remaining));
}
}
bool MaterialSeedCache::trySeed(const SimulationSnapshot::Handle& snapshot, int team, int material,
    int swim, Uint16* output, const Uint16* suppliers)
{
    const auto view=snapshot.view();
    const size_t cells=size_t(view.width)*view.height, words=(cells+63)/64;
    const size_t required=cells*(2*sizeof(Uint16)+sizeof(Uint16)+sizeof(Uint32))
        + words*sizeof(Uint64)*(MaterialCount+Team::MAX_COUNT+1)
        + snapshot.resources->stamps.chunks.size()*4*sizeof(Uint64)
        + view.resourceRegistry->size()*sizeof(Uint32) + view.terrainRegistry->size();
    if (disabled || cells<=4096 || required>budget) return false;
    const bool rebuild=world!=snapshot.worldIdentity || width!=view.width || height!=view.height
        || terrainRegistry!=snapshot.terrain->registry || resourceRegistry!=snapshot.catalogs->resources;
    try {
        if (rebuild) {
            const auto limit=budget;
            const auto refreshed=refreshedCells;
            *this=MaterialSeedCache{}; budget=limit; refreshedCells=refreshed;
            resourceTraits.resize(view.resourceRegistry->size());
            for (size_t id=0; id<resourceTraits.size(); ++id) {
                const auto& p=view.resourceProperties(id);
                resourceTraits[id]=p.materialMask | (p.blocksGround?0x10000:0)
                    | (std::has_single_bit(p.materialMask)?0:0x20000);
            }
            for (auto& values:base) values.resize(cells);
            signatures.assign(cells,0);
            forbiddenMasks.assign(cells,0);
            for (auto& bits:goals) bits.assign(words,0);
            for (auto& bits:forbidden) bits.assign(words,0);
            buildings.assign(words,0);
            for (auto& values:stamps) values.clear();
            world=snapshot.worldIdentity; width=view.width; height=view.height;
            terrainRegistry=snapshot.terrain->registry; resourceRegistry=snapshot.catalogs->resources;
        }
        MapState::ChunkGeometry geometry;
        geometry.reset(view.width,view.height,view.wDec,view.wMask);
        const std::array<const std::vector<Uint64>*,4> current={&snapshot.terrain->stamps.chunks,
            &snapshot.resources->stamps.chunks,&snapshot.occupancy->stamps.chunks,&snapshot.areas->stamps.chunks};
        for (size_t chunk=0; chunk<geometry.count(); ++chunk) {
            const auto changed=[&](size_t component) {
                return rebuild || stamps[component].size()!=geometry.count()
                    || current[component]->size()!=geometry.count()
                    || stamps[component][chunk]!=(*current[component])[chunk];
            };
            const bool cellsChanged=changed(0)||changed(1)||changed(2), areasChanged=changed(3);
            if (!cellsChanged && !areasChanged) continue;
            geometry.forEachRow(chunk,[&](size_t begin,size_t length) {
                for (size_t i=begin; i<begin+length; ++i) {
                    if (cellsChanged) {
                        ++refreshedCells;
                        const auto& cell=view.occupancy[i];
                        const auto& deposit=view.resources[i].resource;
                        const Uint32 traits=deposit.type==NO_RES_TYPE?0:resourceTraits[deposit.type];
                        const bool mobile=cell.immobileUnit==IMMOBILE_UNIT_NONE;
                        const bool open=mobile && !(traits&0x10000);
                        const MaterialMask mask=!mobile?0:(traits&0x20000)?MapState::materialMaskAt(view,i)
                            :deposit.amount?MaterialMask(traits&AllMaterials):0;
                        const auto& rules=view.terrainProperties(i);
                        const Uint8 terrain=(rules.walkable?1:0)|((rules.walkable||rules.swimmable)?2:0);
                        // Only facts that affect seeds: moving units, animation and positive
                        // stock changes do not alter this template. All twelve materials fit.
                        static_assert(MaterialCount==12);
                        const Uint16 signature=mask | ((open && cell.building==NOGBID && (terrain&1))?0x1000:0)
                            | ((open && cell.building==NOGBID && (terrain&2))?0x2000:0)
                            | ((open && cell.building!=NOGBID)?0x4000:0);
                        const Uint16 old=signatures[i];
                        if (rebuild || old!=signature) {
                            base[0][i]=(signature&0x1000)?GRADIENT_UNREACHABLE:GRADIENT_FORBIDDEN;
                            base[1][i]=(signature&0x2000)?GRADIENT_UNREACHABLE:GRADIENT_FORBIDDEN;
                            for (unsigned changed=(old^signature)&AllMaterials; changed; changed&=changed-1) {
                                const unsigned bit=std::countr_zero(changed);
                                setSeedBit(goals[bit],i,mask&(1u<<bit));
                            }
                            setSeedBit(buildings,i,signature&0x4000);
                            signatures[i]=signature;
                        }
                    }
                    if (areasChanged) {
                        const Uint32 mask=view.areas[i].forbidden;
                        for (Uint32 changed=forbiddenMasks[i]^mask; changed; changed&=changed-1) {
                            const unsigned bit=std::countr_zero(changed);
                            if (bit<Team::MAX_COUNT) setSeedBit(forbidden[bit],i,mask&(Uint32(1)<<bit));
                        }
                        forbiddenMasks[i]=mask;
                    }
                }
            });
        }
        for (size_t component=0; component<4; ++component) stamps[component]=*current[component];
    } catch (const std::bad_alloc&) {
        // Allocation failure only disables this optional optimization.
        const size_t limit=budget; *this=MaterialSeedCache{}; budget=limit; disabled=true;
        return false;
    }
    std::memcpy(output,base[swim>0].data(),cells*sizeof(*output));
    if (suppliers) visitSeedBits(buildings,[&](size_t i) {
        const unsigned id=unsigned(view.occupancy[i].building)-unsigned(team)*Building::MAX_COUNT;
        if (id<Building::MAX_COUNT) output[i]=suppliers[id];
    });
    const Uint32 mask=Uint32(1)<<team;
    visitSeedBits(goals[material],[&](size_t i) {
        if (!view.resourceProperties(view.resources[i].resource.type).visibleToHarvest || (snapshot.visibility->visible[i]&mask))
            output[i]=GRADIENT_AT_GOAL;
    });
    visitSeedBits(forbidden[team],[&](size_t i) { output[i]=GRADIENT_FORBIDDEN; });
    return true;
}

SimulationSnapshot::Requirements Request::requirements() const
{
    using namespace SimulationSnapshot;
    auto result = bit(Component::Catalogs) | bit(Component::Terrain) | bit(Component::Resources)
        | bit(Component::Occupancy) | bit(Component::Areas);
    if (kind == Kind::Materials || kind == Kind::Markets) result |= bit(Component::Visibility);
    if (kind == Kind::Markets) result |= bit(Component::Entities);
    if (kind == Kind::Guard && crowding) result |= bit(Component::Entities);
    return result;
}

void boxSum(Uint16* grid, int w, int h, CrowdingScratch& scratch)
{
	const int wMask=w-1, hMask=h-1;
	// A window wider than the map would count a cell twice.
	const int r = std::min<int>(GUARD_CROWD_RADIUS, std::min((int)w - 1, (int)h - 1) / 2);
	auto &crowdRows = scratch.rows;
	auto &crowdColumnSums = scratch.columnSums;
	crowdRows.resize(size_t(w)*h); // every cell is written below
	// Rows: window [x-r, x+r] slides right; entering x+r+1, leaving x-r.
	for (int y = 0; y < (int)h; y++)
	{
		const size_t row = size_t(y)*w;
		int sum = 0;
		for (int dx = -r; dx <= r; dx++)
			sum += grid[row | (size_t)(dx & (int)wMask)];
		for (int x = 0; x < (int)w; x++)
		{
			crowdRows[row | (size_t)x] = (Uint16)sum;
			sum += grid[row | (size_t)((x + r + 1) & (int)wMask)] - grid[row | (size_t)((x - r) & (int)wMask)];
		}
	}
	// Columns, the same way over the row sums: crowdColumnSums[x] is the running
	// window sum of column x, advanced one whole row at a time so the sweep
	// stays row-major and the inner loops vectorise.
	crowdColumnSums.assign(w, 0);
	for (int dy = -r; dy <= r; dy++)
	{
		const Uint16 *in = &crowdRows[size_t(dy & hMask)*w];
		for (int x = 0; x < (int)w; x++)
			crowdColumnSums[x] += in[x];
	}
	for (int y = 0; y < (int)h; y++)
	{
		Uint16 *o = grid + (size_t(y)*w);
		const Uint16 *entering = &crowdRows[size_t((y+r+1) & hMask)*w];
		const Uint16 *leaving = &crowdRows[size_t((y-r) & hMask)*w];
		for (int x = 0; x < (int)w; x++)
		{
			o[x] = (Uint16)crowdColumnSums[x];
			crowdColumnSums[x] += entering[x] - leaving[x];
		}
	}
}

void seed(const Request& request, const SimulationSnapshot::Handle& snapshot, Uint16* out, CrowdingScratch& scratch)
{
    const auto view = snapshot.view();
    const auto size = size_t(view.width) * view.height;
    const Uint32 mask = Uint32(1) << request.team;
    std::array<Uint16, Building::MAX_COUNT> suppliers;
    if (request.kind == Kind::Markets)
    {
        suppliers.fill(GRADIENT_FORBIDDEN);
        for (const auto& b : snapshot.entities->buildings)
        {
            if (b.team != request.team || b.buildingState != Building::ALIVE) continue;
            const auto& type = snapshot.catalogs->buildings->at(b.typeNum).resolvedType;
            if (!(type.runtimeSuppliesStockMask & b.availableSupplyMask & (1u << request.material))) continue;
            suppliers[Building::GIDtoID(b.identity.gid)] = std::max<int>(GRADIENT_UNREACHABLE+1,
                GRADIENT_AT_GOAL - type.semantics.market.pickupPenalty * GRADIENT_STEP);
        }
    }
    const auto run=[size](auto fn) { fn(0, size); };
    switch (request.kind) {
    case Kind::Materials: case Kind::Markets:
        if (!scratch.materials.trySeed(snapshot,request.team,request.material,request.swim,out,
            request.kind==Kind::Markets ? suppliers.data() : nullptr))
            materialCells(view, snapshot.visibility->visible.data(), request.team, request.material, request.swim,
                out, request.kind==Kind::Markets ? suppliers.data() : nullptr, run);
        break;
    case Kind::Clear:
        clearCells(view, snapshot.areas->farmEnabled, request.team, request.swim, out, run);
        break;
    case Kind::Guard:
        guardCells(view, request.allies, request.team, request.swim, out, run);
        break;
    }
    if (request.kind == Kind::Markets)
        for (const auto& b : snapshot.entities->buildings)
        {
            if (b.team != request.team) continue;
            const auto value = suppliers[Building::GIDtoID(b.identity.gid)];
            const auto& type = snapshot.catalogs->buildings->at(b.typeNum).resolvedType;
            if (value <= GRADIENT_UNREACHABLE || type.semantics.occupiesGround) continue;
            for (int y=0; y<type.height; ++y) for (int x=0; x<type.width; ++x)
            {
                const auto i = view.index(b.posX+x, b.posY+y);
                if (!(view.areas[i].forbidden & mask) && view.occupancy[i].immobileUnit == IMMOBILE_UNIT_NONE)
                    out[i] = std::max(out[i], value);
            }
        }
    if (request.kind != Kind::Guard || !request.crowding) return;
    scratch.seeds.clear();
    for (size_t i=0; i<size; ++i) if (out[i] == GRADIENT_AT_GOAL) scratch.seeds.push_back(i);
    if (scratch.seeds.empty()) return;
    scratch.positions.clear();
    for (const auto& u : snapshot.entities->units)
        if (u.team == request.team && !u.isDead && u.typeNum == WARRIOR && u.displacement != Unit::DIS_INSIDE)
            scratch.positions.push_back(view.index(u.posX, u.posY));
    if (scratch.positions.empty()) return;
    scratch.warriors.assign(size, 0);
    for (auto i : scratch.positions) ++scratch.warriors[i];
    boxSum(scratch.warriors.data(), view.width, view.height, scratch);
    scratch.paint.assign(size, 0);
    for (auto i : scratch.seeds) scratch.paint[i] = 1;
    boxSum(scratch.paint.data(), view.width, view.height, scratch);
    for (auto i : scratch.seeds)
    {
        const int cost = GUARD_CROWD_COST_PER_WARRIOR * scratch.warriors[i] * GUARD_CROWD_REFERENCE_AREA / std::max<int>(1, scratch.paint[i]);
        out[i] = Uint16(GRADIENT_AT_GOAL - std::min(GUARD_CROWD_COST_MAX, cost));
    }
}

namespace
{
gradient_kernel::CostIdentity snapshotCostIdentity(const Request& request, const SimulationSnapshot::Handle& snapshot)
{
    const auto& terrain = *snapshot.terrain;
    // Snapshot lookups cover every cell. Valid terrain costs do not depend on
    // this field's obstacle mask, even when movement modifiers are enabled.
    const bool allCells = !terrain.movementModifiers || std::all_of(
        terrain.rules->movement(request.swim).profiles.begin(), terrain.rules->movement(request.swim).profiles.end(),
        [](const auto step) { return step.cardinal && step.diagonal && step.cardinal <= 65535 && step.diagonal <= 65535; });
    return {snapshot.terrain, std::uint64_t(request.swim), terrain.revision, allCells};
}
}

void propagate(const Request& request, const SimulationSnapshot::Handle& snapshot, Uint16* out, GradientWorkspace& scratch)
{
    scratch.family = backendFamily(request.kind);
    gradient_kernel::propagateTerrainField(out, request.swim, gradient_kernel::COST_LIMIT,
        {snapshot.width, snapshot.height}, scratch,
        [rules=snapshot.terrain->cellRules.data()](size_t i) { return rules[i]; },
        snapshot.terrain->movementModifiers, *snapshot.terrain->rules, request.terrainBuckets,
        snapshotCostIdentity(request, snapshot));
}

namespace
{
gradient_kernel::EntrySteps batchCosts(const PropagationField &field, std::size_t cell)
{
    using namespace gradient_kernel;
    const auto &terrain = *field.snapshot->terrain;
    if (!terrain.movementModifiers)
        return weightedClass(field.request.swim) && terrain.rules->swimming(terrain.cellRules[cell])
                   ? entrySteps(WATER_STEP[field.request.swim])
                   : LAND_STEPS;
    const auto &movement = terrain.rules->movement(field.request.swim);
    return movement.profiles[movement.profileIds[terrain.cellRules[cell]]];
}
void batchCPU(const PropagationField &field, Uint16 *out)
{
    using namespace gradient_kernel;
    const auto &terrain = *field.snapshot->terrain;
    const field::Grid grid(field.snapshot->width, field.snapshot->height);
    auto at = [&](std::size_t i) { return terrain.cellRules[i]; };
    if (!terrain.movementModifiers)
    {
        propagateFieldCPU(out, field.request.swim, COST_LIMIT, grid, *field.scratch,
                          [&](std::size_t i) { return terrain.rules->swimming(at(i)); });
        return;
    }
    const auto &movement = terrain.rules->movement(field.request.swim);
    if (field.request.terrainBuckets == 64)
        runtime_terrain::propagate<64, false, false>(out, COST_LIMIT, grid, *field.scratch, at, movement);
    else if (field.request.terrainBuckets == 128)
        runtime_terrain::propagate<128, false, false>(out, COST_LIMIT, grid, *field.scratch, at, movement);
    else
        runtime_terrain::propagate<256, false, false>(out, COST_LIMIT, grid, *field.scratch, at, movement);
}
void batchCPUGroup(std::span<const gradient_kernel::BackendRequest* const> requests,
                   std::span<Uint16* const> destinations)
{
    const auto& first = *static_cast<const PropagationField*>(requests.front()->context);
    first.executor->run(requests.size(), [&](std::size_t i) {
        requests[i]->cpu(requests[i]->context, destinations[i]);
    });
}
} // namespace
void propagateBatch(std::span<const PropagationField> fields)
{
    using namespace gradient_kernel;
    std::vector<BackendRequest> requests;
    requests.reserve(fields.size());
    for (const auto &field : fields)
    {
        try
        {
            runtime_terrain::validateQueueSize(field.request.terrainBuckets);
            const auto &terrain = *field.snapshot->terrain;
            if (terrain.movementModifiers)
            {
                const auto &movement = terrain.rules->movement(field.request.swim);
                if (std::any_of(movement.profiles.begin(), movement.profiles.end(),
                                [&](auto cost)
                                {
                                    return !cost.cardinal || !cost.diagonal ||
                                           cost.cardinal >= field.request.terrainBuckets ||
                                           cost.diagonal >= field.request.terrainBuckets;
                                }))
                    for (std::size_t cell = 0;
                         cell < std::size_t(field.snapshot->width) * field.snapshot->height; ++cell)
                        if (field.output[cell])
                            runtime_terrain::validateQueueEdge(batchCosts(field, cell),
                                                               field.request.terrainBuckets);
            }
            requests.push_back(
                {field.output,
                 COST_LIMIT,
                 {field.snapshot->width, field.snapshot->height},
                 *field.scratch->backendSession,
                 const_cast<PropagationField *>(&field),
                 [](void *context, std::size_t cell)
                 { return batchCosts(*static_cast<PropagationField *>(context), cell); },
                 [](void *context, Uint16 *out) { batchCPU(*static_cast<PropagationField *>(context), out); },
                 snapshotCostIdentity(field.request, *field.snapshot),
                 backendFamily(field.request.kind), field.executor ? batchCPUGroup : nullptr});
        }
        catch (...)
        {
            if (field.error)
                *field.error = std::current_exception();
            else
                throw;
        }
    }
    // Ready fields run immediately; publication still keeps its fixed deadline.
    if (requests.size() == 1 ? accelerator && accelerator(requests.front(), backend())
                             : batchAccelerator && batchAccelerator(requests, backend()))
        return;
    for (const auto &request : requests)
    {
        const auto &field = *static_cast<const PropagationField *>(request.context);
        try
        {
            batchCPU(field, field.output);
        }
        catch (...)
        {
            if (field.error)
                *field.error = std::current_exception();
            else
                throw;
        }
    }
}

SimulationSnapshot::Requirements buildingRequirements()
{
    using namespace SimulationSnapshot;
    return bit(Component::Catalogs) | bit(Component::Terrain) | bit(Component::Resources)
        | bit(Component::Occupancy) | bit(Component::Areas);
}

BuildingSeed captureBuildingSeed(const Building& building, int swim, BuildingRoute route)
{
    BuildingSeed seed;
    seed.posX=building.posX; seed.posY=building.posY;
    seed.width=building.type->width; seed.height=building.type->height;
    seed.unitStayRange=building.unitStayRange; seed.swim=swim;
    seed.gid=Uint16(building.gid);
    seed.route=building.resolveRoute(route);
    seed.occupiesGround=building.type->semantics.occupiesGround;
    seed.teamMask=building.owner->me; seed.allies=building.owner->allies;
    std::copy(std::begin(building.clearingMaterials), std::end(building.clearingMaterials), seed.clearingMaterials.begin());
    return seed;
}

BuildingSeedResult seedBuilding(const BuildingSeed& building, const SimulationSnapshot::Handle& snapshot, Uint16* out)
{
    const auto view=snapshot.view();
    const auto size=size_t(view.width)*view.height;
    return buildingCells(view, building, out, [size](auto fn) { fn(0, size); });
}

BuildingSeedResult buildBuilding(const BuildingSeed& building, const SimulationSnapshot::Handle& snapshot,
    const BuildingGradientSearch::Inputs& inputs, Uint16* out, BuildingGradientSearch& search, int depthTarget)
{
    const auto result=seedBuilding(building, snapshot, out);
    if (result.locked) return result;
    search.begin(inputs, out, building.swim, snapshot.width, snapshot.height);
    // resolveToCost records no owner telemetry; COST_LIMIT drains every queue.
    search.resolveToCost(depthTarget);
    return result;
}
}
