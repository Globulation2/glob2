// SPDX-License-Identifier: GPL-3.0-or-later
#include "Map.h"
#include "Game.h"
#include "Unit.h"
#include <algorithm>
#include <bit>

namespace {
unsigned ownerTeam(ResourceClaims::Owner owner) { return Unit::GIDtoTeam(Uint16(owner)); }
}

void Map::invalidateMaterialClaimOwners(std::vector<ResourceClaims::Owner> owners,Uint32 teams)
{
    if (!activeClaimsEnabled()) return;
    std::sort(owners.begin(),owners.end());
    owners.erase(std::unique(owners.begin(),owners.end()),owners.end());
    for (const auto owner:owners) {
        const auto team=ownerTeam(owner);
        if (team>=Team::MAX_COUNT || !(teams&(Uint32(1)<<team))) continue;
        const auto gid=Uint16(owner);
        Unit* unit=game && team<unsigned(game->teamsCount()) ? game->teams[team]->myUnits[Unit::GIDtoID(gid)] : nullptr;
        if (unit && unit->scriptIdentity==Uint32(owner>>16) && unit->activeMaterialClaim)
            unit->releaseActiveMaterialClaim();
        else { cancelClaimRoute(owner);releaseMaterialClaim(owner); } // Never touch a replacement in the reused slot.
    }
}

void Map::invalidateMaterialClaimsAt(size_t index,bool source,bool access,Uint32 teams)
{
    if (!activeClaimsEnabled() || !materialClaims.size()) return;
    auto owners=source ? materialClaims.ownersAtSource(Uint32(index)) : std::vector<ResourceClaims::Owner>{};
    if (access) {
        const auto other=materialClaims.ownersAtAccess(Uint32(index));
        owners.insert(owners.end(),other.begin(),other.end());
    }
    // Invalidate the affected pursuits, rather than silently selecting which
    // overcommitted reservation survives a capacity decrease.
    invalidateMaterialClaimOwners(std::move(owners),teams);
}

std::vector<ResourceClaims::Owner> Map::farmMaterialClaimOwners(size_t index,Uint32 teams,MaterialMask materials)
{
    if (!activeClaimsEnabled() || !materialClaims.size() || !farmAreasEnabled()) return {};
    // Called before a removal: retain the complete old component, including
    // claims whose source and access are remote from a disappearing bridge.
    std::vector<ResourceClaims::Owner> affected;
    const auto append=[&](const auto& owners,unsigned team,unsigned material) {
        for (const auto owner:owners) {
            const auto* claim=materialClaims.find(owner);
            if (claim && claim->material==material && ownerTeam(owner)==team) affected.push_back(owner);
        }
    };
    if (farmFloodStamps.size()!=cellCount()) { farmFloodStamps.assign(cellCount(),0);farmFloodGeneration=0; }
    for (auto bits=areaCells[index].farm&teams;bits;bits&=bits-1) {
        const unsigned team=std::countr_zero(bits);
        if (team>=Team::MAX_COUNT) continue;
        const auto mask=Uint32(1)<<team;
        for (unsigned material=0;material<MaterialCount;++material) {
            if (!(materials&(MaterialMask(1)<<material))) continue;
            const auto belongs=[&](size_t i) {
                const auto type=resourceCells[i].resource.type;
                if (!(areaCells[i].farm&mask) || !isFarmableResourceByIndex(type) || !materialAmountAtSlot(i,material)) return false;
                const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(type))[material];
                return yield.consumption!=ResourceConsumption::All && !yield.destroysDeposit;
            };
            if (!belongs(index)) continue;
            if (++farmFloodGeneration==0) { std::fill(farmFloodStamps.begin(),farmFloodStamps.end(),0);farmFloodGeneration=1; }
            farmFloodQueue.clear();farmFloodQueue.push_back(index);farmFloodStamps[index]=farmFloodGeneration;
            for (size_t head=0;head<farmFloodQueue.size();++head) {
                const auto i=farmFloodQueue[head];
                append(materialClaims.ownersAtSource(Uint32(i)),team,material);
                append(materialClaims.ownersAtAccess(Uint32(i)),team,material);
                const int x=int(i&wMask),y=int(i>>wDec);
                for (int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    const auto next=coordToIndex(x+dx,y+dy);
                    if (farmFloodStamps[next]==farmFloodGeneration || !belongs(next)) continue;
                    farmFloodStamps[next]=farmFloodGeneration;farmFloodQueue.push_back(next);
                }
            }
        }
    }
    return affected;
}

void Map::revalidateMaterialClaimOwners(std::vector<ResourceClaims::Owner> owners,bool checkFarmConnectivity)
{
    if (!activeClaimsEnabled()) return;
    // Retain lower stable owner IDs when a source loses capacity. Releasing
    // only excess reservations keeps unaffected workers on their cached routes.
    std::sort(owners.begin(),owners.end(),std::greater<>());
    owners.erase(std::unique(owners.begin(),owners.end()),owners.end());
    for (const auto owner:owners) {
        const auto* stored=materialClaims.find(owner);
        if (!stored) continue;
        const auto claim=*stored;
        const unsigned team=ownerTeam(owner);
        const auto gid=Uint16(owner);
        Unit* unit=game && team<unsigned(game->teamsCount()) ? game->teams[team]->myUnits[Unit::GIDtoID(gid)] : nullptr;
        bool valid=unit && unit->scriptIdentity==Uint32(owner>>16) && unit->activeMaterialClaim
            && *unit->activeMaterialClaim==claim && unit->activeMaterialClaimValid()
            && (!checkFarmConnectivity || activeClaimFarmConnected(claim,unit->owner->me));
        if (valid) {
            const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(claim.resource))[claim.material];
            const bool exclusive=yield.consumption==ResourceConsumption::All || yield.destroysDeposit;
            const bool pooled=!exclusive && farmAreasEnabled() && (areaCells[claim.access].farm&unit->owner->me)
                && isFarmableResourceByIndex(claim.resource);
            const auto reserve=pooled && yield.consumption!=ResourceConsumption::Infinite && !game->gameHeader.isResourceGrowthDisabled() ? yield.seedReserve : 0;
            valid=claim.exclusive==exclusive && claim.reserve==reserve;
        }
        if (!valid) invalidateMaterialClaimOwners({owner});
    }
    // Remove semantically invalid floors first. Otherwise a disconnected old
    // seed reservation could evict a still-valid direct consumer unnecessarily.
    for (const auto owner:owners) {
        const auto* stored=materialClaims.find(owner);
        if (!stored) continue;
        const auto claim=*stored;
        const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(claim.resource))[claim.material];
        const auto amount=materialAmountAtSlot(claim.source,claim.material);
        if (!materialClaimAvailable(owner,claim,amount>claim.reserve?amount-claim.reserve:0,
            yield.consumption==ResourceConsumption::Infinite)) invalidateMaterialClaimOwners({owner});
    }
}

void Map::invalidateAllActiveMaterialClaims()
{
    if (!activeClaimsEnabled()) return;
    std::vector<ResourceClaims::Owner> owners;
    for (const auto& [owner,claim]:materialClaims.entries()) owners.push_back(owner);
    invalidateMaterialClaimOwners(std::move(owners));
}

void Map::checkActiveClaimRules()
{
    if (!activeClaimsEnabled()) { activeClaimRules.reset();return; }
    const std::array<Uint64,3> rules{Uint64(farmAreasEnabled()),Uint64(game && game->gameHeader.isResourceGrowthDisabled()),resourceRegistry().checksum()};
    if (activeClaimRules && *activeClaimRules!=rules) invalidateAllActiveMaterialClaims();
    activeClaimRules=rules;
}
