// SPDX-License-Identifier: GPL-3.0-or-later
#include "SnapshotGradient.h"
#include "GradientExecutionAudit.h"
#include "ComputeExecutor.h"
#include "SeedCells.h"
#include "MapInternal.h"
#include "Building.h"
#include "Unit.h"
#include "BuildingType.h"
#include "field/RuntimeTerrainGradient.h"
#include "field/GradientDeviceService.h"
#include <array>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <mutex>

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
namespace {
// Keep the ground kernel independent of air state, including optimizer aliasing.
template<bool Air>
bool seedCachedMaterials(MaterialSeedCache& cache, const SimulationSnapshot::Handle& snapshot, int team, int material,
    int swim, Uint16* output, const Uint16* suppliers, bool rebuild)
{
    const auto view=snapshot.view();
    const size_t cells=size_t(view.width)*view.height, words=(cells+63)/64;
    const size_t required=cells*((Air?3:2)*sizeof(Uint16)+(Air?2:1)*sizeof(Uint16)+sizeof(Uint32))
        + words*sizeof(Uint64)*((Air?2:1)*MaterialCount+Team::MAX_COUNT+(Air?2:1))
        + snapshot.resources->stamps.chunks.size()*4*sizeof(Uint64)
        + view.resourceRegistry->size()*sizeof(Uint32) + view.terrainRegistry->size();
    if (cache.disabled || cells<=4096 || required>cache.budget) return false;
    try {
        if (rebuild) {
            const auto limit=cache.budget;
            const auto refreshed=cache.refreshedCells;
            cache=MaterialSeedCache{}; cache.budget=limit; cache.refreshedCells=refreshed;
            cache.resourceTraits.resize(view.resourceRegistry->size());
            for (size_t id=0; id<cache.resourceTraits.size(); ++id) {
                const auto& p=view.resourceProperties(id);
                cache.resourceTraits[id]=p.materialMask | (p.blocksGround?0x10000:0)
                    | (std::has_single_bit(p.materialMask)?0:0x20000);
            }
            for(unsigned movement=0;movement<2;++movement)cache.base[movement].resize(cells);
            cache.signatures.assign(cells,0);
            cache.forbiddenMasks.assign(cells,0);
            for (auto& bits:cache.goals) bits.assign(words,0);
            for (auto& bits:cache.forbidden) bits.assign(words,0);
            cache.buildings.assign(words,0);
            for (auto& values:cache.stamps) values.clear();
            cache.world=snapshot.worldIdentity; cache.width=view.width; cache.height=view.height;
            cache.terrainRegistry=snapshot.terrain->registry; cache.resourceRegistry=snapshot.catalogs->resources;
        }
        const bool initializeAir=Air && cache.airSignatures.empty();
        if(initializeAir) {
            cache.base[2].resize(cells);cache.airSignatures.assign(cells,0);
            for(auto& bits:cache.airGoals)bits.assign(words,0);
            cache.airBuildings.assign(words,0);
        }
        MapState::ChunkGeometry geometry;
        geometry.reset(view.width,view.height,view.wDec,view.wMask);
        const std::array<const std::vector<Uint64>*,4> current={&snapshot.terrain->stamps.chunks,
            &snapshot.resources->stamps.chunks,&snapshot.occupancy->stamps.chunks,&snapshot.areas->stamps.chunks};
        for (size_t chunk=0; chunk<geometry.count(); ++chunk) {
            const auto changed=[&](size_t component) {
                return rebuild || cache.stamps[component].size()!=geometry.count()
                    || current[component]->size()!=geometry.count()
                    || cache.stamps[component][chunk]!=(*current[component])[chunk];
            };
            const bool cellsChanged=initializeAir||changed(0)||changed(1)||changed(2), areasChanged=changed(3);
            if (!cellsChanged && !areasChanged) continue;
            geometry.forEachRow(chunk,[&](size_t begin,size_t length) {
                for (size_t i=begin; i<begin+length; ++i) {
                    if (cellsChanged) {
                        ++cache.refreshedCells;
                        const auto& cell=view.occupancy[i];
                        const auto& deposit=view.resources[i].resource;
                        const Uint32 traits=deposit.type==NO_RES_TYPE?0:cache.resourceTraits[deposit.type];
                        const bool mobile=cell.immobileUnit==IMMOBILE_UNIT_NONE;
                        const bool open=mobile && !(traits&0x10000);
                        const MaterialMask mask=!mobile?0:(traits&0x20000)?MapState::materialMaskAt(view,i)
                            :deposit.amount?MaterialMask(traits&AllMaterials):0;
                        const auto& rules=view.terrainProperties(i);
                        const Uint8 terrain=(rules.walkable?1:0)|((rules.walkable||rules.swimmable)?2:0);
                        // Only facts that affect seeds: moving units, animation and positive
                        // stock changes do not alter this template. All twelve materials fit.
                        static_assert(MaterialCount==12);
                        if constexpr (Air) {
                        const MaterialMask airMask=MapState::materialMaskAt(view,i);
                        const bool airOpen=rules.flyable && !MapState::resourceBlocksAir(view,i);
                        const Uint16 airSignature=airMask | (airOpen?0x1000:0)
                            | ((!MapState::resourceBlocksAir(view,i) && cell.building!=NOGBID)?0x2000:0);
                        const Uint16 oldAir=cache.airSignatures[i];
                        if(rebuild || oldAir!=airSignature) {
                            cache.base[2][i]=airOpen?GRADIENT_UNREACHABLE:GRADIENT_FORBIDDEN;
                            for(unsigned changed=(oldAir^airSignature)&AllMaterials;changed;changed&=changed-1) {
                                const unsigned bit=std::countr_zero(changed);
                                setSeedBit(cache.airGoals[bit],i,airMask&(1u<<bit));
                            }
                            setSeedBit(cache.airBuildings,i,airSignature&0x2000);
                            cache.airSignatures[i]=airSignature;
                        }
                        }
                        const Uint16 signature=mask | ((open && cell.building==NOGBID && (terrain&1))?0x1000:0)
                            | ((open && cell.building==NOGBID && (terrain&2))?0x2000:0)
                            | ((open && cell.building!=NOGBID)?0x4000:0);
                        const Uint16 old=cache.signatures[i];
                        if (rebuild || old!=signature) {
                            cache.base[0][i]=(signature&0x1000)?GRADIENT_UNREACHABLE:GRADIENT_FORBIDDEN;
                            cache.base[1][i]=(signature&0x2000)?GRADIENT_UNREACHABLE:GRADIENT_FORBIDDEN;
                            for (unsigned changed=(old^signature)&AllMaterials; changed; changed&=changed-1) {
                                const unsigned bit=std::countr_zero(changed);
                                setSeedBit(cache.goals[bit],i,mask&(1u<<bit));
                            }
                            setSeedBit(cache.buildings,i,signature&0x4000);
                            cache.signatures[i]=signature;
                        }
                    }
                    if (areasChanged) {
                        const Uint32 mask=view.areas[i].forbidden;
                        for (Uint32 changed=cache.forbiddenMasks[i]^mask; changed; changed&=changed-1) {
                            const unsigned bit=std::countr_zero(changed);
                            if (bit<Team::MAX_COUNT) setSeedBit(cache.forbidden[bit],i,mask&(Uint32(1)<<bit));
                        }
                        cache.forbiddenMasks[i]=mask;
                    }
                }
            });
        }
        for (size_t component=0; component<4; ++component) cache.stamps[component]=*current[component];
    } catch (const std::bad_alloc&) {
        // Allocation failure only disables this optional optimization.
        const size_t limit=cache.budget; cache=MaterialSeedCache{}; cache.budget=limit; cache.disabled=true;
        return false;
    }
    std::memcpy(output,cache.base[(Air && swim==AIR_MOVEMENT_CLASS)?2:swim>0].data(),cells*sizeof(*output));
    if (suppliers) visitSeedBits((Air && swim==AIR_MOVEMENT_CLASS)?cache.airBuildings:cache.buildings,[&](size_t i) {
        const unsigned id=unsigned(view.occupancy[i].building)-unsigned(team)*Building::MAX_COUNT;
        if (id<Building::MAX_COUNT && ((!Air || swim!=AIR_MOVEMENT_CLASS) || suppliers[id]!=GRADIENT_FORBIDDEN)) output[i]=suppliers[id];
    });
    const Uint32 mask=Uint32(1)<<team;
    visitSeedBits((Air && swim==AIR_MOVEMENT_CLASS)?cache.airGoals[material]:cache.goals[material],[&](size_t i) {
        if (!view.resourceProperties(view.resources[i].resource.type).visibleToHarvest || (snapshot.visibility->visible[i]&mask))
            output[i]=GRADIENT_AT_GOAL;
    });
    if (!Air || swim!=AIR_MOVEMENT_CLASS)
        visitSeedBits(cache.forbidden[team],[&](size_t i) { output[i]=GRADIENT_FORBIDDEN; });
    return true;
}
} // namespace

bool MaterialSeedCache::trySeed(const SimulationSnapshot::Handle& snapshot, int team, int material,
    int swim, Uint16* output, const Uint16* suppliers)
{
    if (swim==WATER_ONLY_CLASS) return false;
    const bool rebuild=world!=snapshot.worldIdentity || width!=snapshot.width || height!=snapshot.height
        || terrainRegistry!=snapshot.terrain->registry || resourceRegistry!=snapshot.catalogs->resources;
    // A warm air template remains coherent on ground requests. A new world
    // starts ground-only again until its first actual air consumer.
    if (swim==AIR_MOVEMENT_CLASS || (!rebuild && !airSignatures.empty()))
        return seedCachedMaterials<true>(*this,snapshot,team,material,swim,output,suppliers,rebuild);
    return seedCachedMaterials<false>(*this,snapshot,team,material,swim,output,suppliers,rebuild);
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
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::Seed);
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
            if (b.identity.gid == request.consumer || (request.excludesShared && b.usesTeamResources)) continue;
            const auto supplies = ((request.modes & 1) ? type.runtimeSuppliesStockMask : 0)
                | ((request.modes & 2) ? type.runtimeSuppliesDirectStockMask : 0);
            if (!(supplies & b.availableSupplyMask & (1u << request.material))) continue;
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
    case Kind::Forbidden:
        for (size_t i=0; i<size; ++i) {
            const bool blocked=MapState::resourceBlocksGround(view,i) || view.occupancy[i].building!=NOGBID
                || view.occupancy[i].immobileUnit!=IMMOBILE_UNIT_NONE
                || !gradient_kernel::terrainAllowsGround(view.terrainProperties(i),request.swim);
            out[i]=blocked ? GRADIENT_FORBIDDEN : (view.areas[i].forbidden & mask) ? GRADIENT_UNREACHABLE : GRADIENT_AT_GOAL;
        }
        for (size_t i=0; i<size; ++i) if (out[i]==GRADIENT_UNREACHABLE)
            for (int d=0;d<8;++d) if (out[view.index((i & view.wMask)+tabClose[d][0],(i >> view.wDec)+tabClose[d][1])]==GRADIENT_AT_GOAL) {
                out[i]=GRADIENT_FORBIDDEN_BORDER; break;
            }
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
                if (request.swim==AIR_MOVEMENT_CLASS || (!(view.areas[i].forbidden & mask) && view.occupancy[i].immobileUnit == IMMOBILE_UNIT_NONE))
                    out[i] = std::max(out[i], value);
            }
        }
    if (request.kind != Kind::Guard || !request.crowding) return;
    scratch.seeds.clear();
    for (size_t i=0; i<size; ++i) if (out[i] == GRADIENT_AT_GOAL) scratch.seeds.push_back(i);
    if (scratch.seeds.empty()) return;
    scratch.positions.clear();
    for (const auto& u : snapshot.entities->units)
        if (u.team == request.team && !u.isDead && (u.capabilityFlags & UnitRuntimeTraits::GuardIdle) && u.displacement != Unit::DIS_INSIDE)
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
    bool allCells=!(request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers);
    std::uint32_t uniform=0;
    if(!(request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers)) {
        // Binary weighted swimming can vary across cells; proving it uniform
        // would need a map scan, so leave it unknown. The land-only class is
        // already uniform by construction.
        if(!gradient_kernel::weightedClass(request.swim))
            uniform=gradient_kernel::LAND_STEPS.cardinal|(gradient_kernel::LAND_STEPS.diagonal<<16);
    } else {
        const auto& profiles=terrain.rules->movement(request.swim).profiles;
        allCells=true;
        const auto first=profiles.empty() ? gradient_kernel::EntrySteps{} : profiles.front();
        bool same=!profiles.empty();
        // This replaces the existing validity pass: only compact cost profiles
        // are inspected, never cells or original seed values.
        for(const auto step:profiles) {
            allCells=allCells && step.cardinal && step.diagonal && step.cardinal<=65535 && step.diagonal<=65535;
            same=same && step.cardinal==first.cardinal && step.diagonal==first.diagonal;
        }
        if(allCells && same) uniform=first.cardinal|(first.diagonal<<16);
    }
    // Synchronous callers keep this borrowed graph alive only through packing.
    // Backend planes own packed costs and use content reuse; they must never
    // retain this transitive terrain/catalog graph. Async DTOs replace the owner
    // with their precharged minimal immutable cost view before submission.
    return {{},std::uint64_t(request.swim),terrain.revision,allCells,0,uniform};
}
}

void propagate(const Request& request, const SimulationSnapshot::Handle& snapshot, Uint16* out, GradientWorkspace& scratch)
{
    scratch.family = backendFamily(request.kind);
    gradient_kernel::propagateTerrainField(out, request.swim, gradient_kernel::COST_LIMIT,
        {snapshot.width, snapshot.height}, scratch,
        [rules=snapshot.terrain->cellRules.data()](size_t i) { return rules[i]; },
        (request.swim==AIR_MOVEMENT_CLASS ? snapshot.terrain->airConstraints : snapshot.terrain->movementModifiers), *snapshot.terrain->rules, request.terrainBuckets,
        snapshotCostIdentity(request, snapshot));
}

namespace
{
struct PropagationField {
    Request request;
    const SimulationSnapshot::Handle* snapshot;
    Uint16* output;
    GradientWorkspace* scratch;
};
gradient_kernel::EntrySteps batchCosts(const PropagationField &field, std::size_t cell)
{
    using namespace gradient_kernel;
    const auto &terrain = *field.snapshot->terrain;
    if (!(field.request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers))
        return weightedClass(field.request.swim) && terrain.rules->swimming(terrain.cellRules[cell])
                   ? entrySteps(WATER_STEP[field.request.swim])
                   : LAND_STEPS;
    const auto &movement = terrain.rules->movement(field.request.swim);
    return movement.profiles[movement.profileIds[terrain.cellRules[cell]]];
}
void validatePropagation(const PropagationField& field)
{
    using namespace gradient_kernel;
    runtime_terrain::validateQueueSize(field.request.terrainBuckets);
    const auto &terrain = *field.snapshot->terrain;
    if ((field.request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers))
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
}
// Keep the large CPU solver separate from validation and the trivial-field path.
// Removing the batch adapter must not pull all solver variants into that wrapper.
[[gnu::noinline]] void batchCPU(const PropagationField &field, Uint16 *out)
{
    using namespace gradient_kernel;
    const auto &terrain = *field.snapshot->terrain;
    const field::Grid grid(field.snapshot->width, field.snapshot->height);
    auto at = [&](std::size_t i) { return terrain.cellRules[i]; };
    if (!(field.request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers))
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
} // namespace
void propagateCPU(const Request& request,const SimulationSnapshot::Handle& snapshot,Uint16* output,GradientWorkspace& scratch)
{
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::Propagation);
    const PropagationField field{request,&snapshot,output,&scratch};
    validatePropagation(field);
    // Preserve the shared trivial-field shortcut and validation order without
    // constructing an accelerator request or rereading its configuration.
    if (!gradient_kernel::alreadyFixedGradient({output, std::size_t(snapshot.width)*snapshot.height}))
        batchCPU(field,output);
}

namespace {
std::atomic<Uint64> minimalAttempted{0},minimalConstructed{0},minimalReused{0},minimalDeclined{0},minimalCopied{0};
std::atomic<Uint64> minimalBudgetDeclines{0},minimalAllocationDeclines{0},minimalRepresentationDeclines{0};
enum class MinimalDecline {Budget,Allocation,Representation};
void noteMinimalDecline(MinimalDecline reason) noexcept {
    ++minimalDeclined;
    if(reason==MinimalDecline::Budget)++minimalBudgetDeclines;
    else if(reason==MinimalDecline::Allocation)++minimalAllocationDeclines;
    else ++minimalRepresentationDeclines;
}
// A recovery view has no transitive simulation/catalog ownership. Its selected
// profile numbering and prepared tables are copied unchanged from the source.
struct MinimalCosts {
    std::size_t leasedBytes;
    int swim;
    bool modified;
    std::array<gradient_kernel::EntrySteps,256> storage{};
    std::span<const gradient_kernel::EntrySteps> profiles;
    // The indirect branch is never selected, but remains a valid expression
    // in the generic runtime kernel's DirectProfiles conditional.
    std::array<std::uint8_t,256> profileIds{};
    std::optional<TerrainRegistry::Movement::Prepared> prepared;
    std::unique_ptr<std::uint8_t[]> cells;
    MinimalCosts(std::size_t bytes,int swim,bool modified):leasedBytes(bytes),swim(swim),modified(modified){}
    MinimalCosts(const MinimalCosts&)=delete;
    ~MinimalCosts(){cells.reset();gradient_kernel::releaseOpenCLHostBytes(leasedBytes);}
    gradient_kernel::EntrySteps at(std::size_t i)const {
        if(modified)return profiles[cells[i]];
        return gradient_kernel::weightedClass(swim)&&cells[i]
            ?gradient_kernel::entrySteps(gradient_kernel::WATER_STEP[swim]):gradient_kernel::LAND_STEPS;
    }
};
struct MinimalCostCache {
    static constexpr unsigned Count=64;
    struct Entry {
        std::weak_ptr<const SimulationSnapshot::Terrain> source;
        std::weak_ptr<const MinimalCosts> view;
        int width=0,height=0,swim=0;
        Uint64 revision=0;
        bool modified=false;
    };
    // Weak source leases retain only control/object allocation storage after
    // destruction, never source vectors/registries. Weak make_shared view
    // controls also retain their coallocated fixed object storage. Charge both
    // conservatively as well as cache/control metadata before construction.
    static constexpr std::size_t Bytes=sizeof(std::mutex)+sizeof(Entry)*Count
        +Count*(sizeof(SimulationSnapshot::Terrain)+sizeof(MinimalCosts)+512)+512;
    std::mutex mutex;
    std::array<Entry,Count> entries;
    unsigned next=0;
    ~MinimalCostCache(){for(auto& e:entries)e={};gradient_kernel::releaseOpenCLHostBytes(Bytes);}
};
std::shared_ptr<MinimalCostCache> minimalCache(MinimalDecline& reason) noexcept {
    static std::mutex mutex;
    static std::shared_ptr<MinimalCostCache> cache;
    std::lock_guard lock(mutex);
    if(cache)return cache;
    if(!gradient_kernel::reserveOpenCLHostBytes(MinimalCostCache::Bytes))return {};
    try {cache=std::make_shared<MinimalCostCache>();}
    catch(...) {reason=MinimalDecline::Allocation;gradient_kernel::releaseOpenCLHostBytes(MinimalCostCache::Bytes);}
    return cache;
}
bool sameSource(const std::weak_ptr<const SimulationSnapshot::Terrain>& a,
                const std::shared_ptr<const SimulationSnapshot::Terrain>& b) {
    return !a.owner_before(b)&&!b.owner_before(a);
}
std::shared_ptr<const MinimalCosts> minimalView(const Request& request,const SimulationSnapshot::Handle& snapshot) noexcept {
    const auto decline=[](MinimalDecline reason)->std::shared_ptr<const MinimalCosts>{noteMinimalDecline(reason);return {};};
    try {
        if(!snapshot.terrain || !snapshot.terrain->rules || request.swim<0 ||
            unsigned(request.swim)>=MOVEMENT_CLASS_COUNT)return decline(MinimalDecline::Representation);
        const auto& terrain=*snapshot.terrain;
        const auto& movement=terrain.rules->movement(request.swim);
        if((request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers)&&(movement.profiles.empty()||movement.profiles.size()>256||!movement.prepared))return decline(MinimalDecline::Representation);
        auto reason=MinimalDecline::Budget;auto cache=minimalCache(reason);if(!cache)return decline(reason);
        const auto matches=[&](const MinimalCostCache::Entry& e){return sameSource(e.source,snapshot.terrain)
            &&e.width==snapshot.width&&e.height==snapshot.height&&e.swim==request.swim
            &&e.revision==terrain.revision&&e.modified==(request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers);};
        {
            std::lock_guard lock(cache->mutex);
            for(const auto& e:cache->entries)if(matches(e))if(auto view=e.view.lock()){++minimalReused;return view;}
        }
        const auto count=std::size_t(snapshot.width)*snapshot.height;
        const bool plane=(request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers)||gradient_kernel::weightedClass(request.swim);
        if(snapshot.width<=0||snapshot.height<=0||terrain.cellRules.size()!=count)return decline(MinimalDecline::Representation);
        const auto payload=plane?count:0;
        if(payload>gradient_kernel::OpenCLHostBudget-sizeof(MinimalCosts)-256)return decline(MinimalDecline::Budget);
        const auto bytes=sizeof(MinimalCosts)+256+payload;
        if(!gradient_kernel::reserveOpenCLHostBytes(bytes))return decline(MinimalDecline::Budget);
        std::shared_ptr<MinimalCosts> view;
        try {view=std::make_shared<MinimalCosts>(bytes,request.swim,(request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers));}
        catch(...) {gradient_kernel::releaseOpenCLHostBytes(bytes);return decline(MinimalDecline::Allocation);}
        if((request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers)) {
            std::copy(movement.profiles.begin(),movement.profiles.end(),view->storage.begin());
            view->profiles={view->storage.data(),movement.profiles.size()};
            view->prepared=movement.prepared;
        }
        if(plane) {
            view->cells=std::make_unique<std::uint8_t[]>(count);
            for(std::size_t i=0;i<count;++i) {
                const auto rule=terrain.cellRules[i];
                if((request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers)) {
                    if(rule>=movement.profileIds.size()||movement.profileIds[rule]>=movement.profiles.size())return decline(MinimalDecline::Representation);
                    view->cells[i]=movement.profileIds[rule];
                }else view->cells[i]=terrain.rules->swimming(rule);
            }
            minimalCopied.fetch_add(count,std::memory_order_relaxed);
        }
        ++minimalConstructed;
        {
            std::lock_guard lock(cache->mutex);
            // Another worker may have built the same immutable view. Prefer
            // that shared owner so backend cache identity does not fragment.
            for(const auto& e:cache->entries)if(matches(e))if(auto existing=e.view.lock()){++minimalReused;return existing;}
            auto& e=cache->entries[cache->next++%MinimalCostCache::Count];
            e={snapshot.terrain,view,snapshot.width,snapshot.height,request.swim,terrain.revision,(request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers)};
        }
        return view;
    }catch(const std::bad_alloc&){return decline(MinimalDecline::Allocation);}
    catch(...){return decline(MinimalDecline::Representation);}
}
std::shared_ptr<gradient_kernel::OwnedGradientField> minimalHolder(std::size_t count,MinimalDecline& reason) noexcept {
    using namespace gradient_kernel;
    constexpr auto fixed=sizeof(OwnedGradientField)+256;
    if(count>(OpenCLHostBudget-fixed)/sizeof(Uint16))return {};
    const auto bytes=fixed+count*sizeof(Uint16);
    if(!reserveOpenCLHostBytes(bytes))return {};
    // Cover holder/control and original output ownership before allocation or
    // transfer. The deleter releases only after members and object are gone.
    struct Admission {std::size_t bytes;~Admission(){if(bytes)releaseOpenCLHostBytes(bytes);}} admission{bytes};
    auto dispose=[bytes](OwnedGradientField* field){delete field;releaseOpenCLHostBytes(bytes);};
    try {
        std::unique_ptr<OwnedGradientField,decltype(dispose)> allocated(new OwnedGradientField,dispose);
        admission.bytes=0;
        return std::shared_ptr<OwnedGradientField>(std::move(allocated));
    }catch(...){reason=MinimalDecline::Allocation;return {};}
}
}
MinimalCostViewMetrics minimalCostViewMetrics() noexcept {
    return {minimalAttempted.load(std::memory_order_relaxed),minimalConstructed.load(std::memory_order_relaxed),minimalReused.load(std::memory_order_relaxed),
        minimalDeclined.load(std::memory_order_relaxed),minimalCopied.load(std::memory_order_relaxed),
        minimalBudgetDeclines.load(std::memory_order_relaxed),minimalAllocationDeclines.load(std::memory_order_relaxed),minimalRepresentationDeclines.load(std::memory_order_relaxed)};
}
std::shared_ptr<gradient_kernel::OwnedGradientField> ownPropagation(
    const Request& request,const SimulationSnapshot::Handle& snapshot,std::unique_ptr<Uint16[]>& data,
    std::shared_ptr<gradient_kernel::BackendSession> session,gradient_kernel::PlanDecision decision,std::uint64_t due)
{
    using namespace gradient_kernel;
    runtime_terrain::validateQueueSize(request.terrainBuckets);
    const auto& terrain=*snapshot.terrain;
    if((request.swim==AIR_MOVEMENT_CLASS ? terrain.airConstraints : terrain.movementModifiers)) {
        const auto& movement=terrain.rules->movement(request.swim);
        if(std::any_of(movement.profiles.begin(),movement.profiles.end(),[&](auto cost) {
            return !cost.cardinal || !cost.diagonal || cost.cardinal>=request.terrainBuckets || cost.diagonal>=request.terrainBuckets;
        })) {
            const PropagationField field{request,&snapshot,data.get(),nullptr};
            for(std::size_t cell=0;cell<std::size_t(snapshot.width)*snapshot.height;++cell)
                if(data[cell]) runtime_terrain::validateQueueEdge(batchCosts(field,cell),request.terrainBuckets);
        }
    }
    ++minimalAttempted;
    const auto view=minimalView(request,snapshot);if(!view)return {};
    auto reason=MinimalDecline::Budget;
    auto owned=minimalHolder(std::size_t(snapshot.width)*snapshot.height,reason);
    if(!owned){noteMinimalDecline(reason);return {};}
    if(!owned->requiredControl){noteMinimalDecline(MinimalDecline::Budget);return {};}
    owned->inputs=view;owned->session=std::move(session);owned->grid={snapshot.width,snapshot.height};
    owned->family=backendFamily(request.kind);owned->cpuBuckets=request.terrainBuckets;
    owned->decision=decision;owned->due=due;
    // Reuse the bounded validity/uniform proof, but replace its graph owner
    // and retained byte estimate before anything escapes to the backend.
    owned->identity=snapshotCostIdentity(request,snapshot);
    owned->identity.owner=view;owned->identity.retainedBytes=view->leasedBytes;
    owned->costAt=[](const OwnedGradientField& field,std::size_t i){return static_cast<const MinimalCosts*>(field.inputs.get())->at(i);};
    owned->cpu=[](OwnedGradientField& field) {
        const auto& costs=*static_cast<const MinimalCosts*>(field.inputs.get());
        GradientWorkspace workspace;workspace.backendSession=field.session;
        if(!costs.modified) {
            propagateFieldCPU(field.data.get(),costs.swim,COST_LIMIT,field.grid,workspace,
                [&](std::size_t i){return costs.cells&&costs.cells[i];});
        }else {
            const auto at=[&](std::size_t i){return costs.cells[i];};
            if(field.cpuBuckets==64)runtime_terrain::propagate<64,true,false>(field.data.get(),COST_LIMIT,field.grid,workspace,at,costs);
            else if(field.cpuBuckets==128)runtime_terrain::propagate<128,true,false>(field.data.get(),COST_LIMIT,field.grid,workspace,at,costs);
            else runtime_terrain::propagate<256,true,false>(field.data.get(),COST_LIMIT,field.grid,workspace,at,costs);
        }
    };
    owned->data=std::move(data);return owned;
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
    for (unsigned material=0; material<MaterialCount; ++material)
        seed.clearingMaterials[material]=building.clearsMaterial(material);
    return seed;
}

BuildingSeedResult seedBuilding(const BuildingSeed& building, const SimulationSnapshot::Handle& snapshot, Uint16* out)
{
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::Seed);
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
