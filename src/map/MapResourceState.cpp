// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "gradient/ResourceSeedCache.h"
#include "gradient/GradientRuntime.h"
#include "Utilities.h"
#include "ExperimentalFeatures.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapInternal.h"
#include "TerrainResourceProperties.h"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <map>
#include <cstdlib>
#include <limits>

// Immutable resource catalogs, compact material stocks and compiled habitat properties

void Map::installResourceDefinitions(const std::string& json)
{
    if (game && !game->edit && game->stepCounter != 0)
        throw std::logic_error("Resource definitions are frozen during a match");
    const auto next=resourceRegistry().importJson(json);
    // Stage every allocating operation against the replacement catalog before
    // publishing it. A malformed import or allocation failure leaves the map
    // and its running readers on the original immutable snapshot.
    Map staged;
    staged.resourceRegistryValue=next;
    staged.terrainRegistryValue=terrainRegistryValue;
    staged.rebuildResourceHabitats();
    std::vector<Resource> deposits(tiles.size());
    std::vector<Uint32> stockIndices;
    std::vector<std::array<Uint16,MaterialCount>> stocks;
    std::array<size_t,MaterialCount> counts{};
    for (size_t i=0;i<tiles.size();++i)
    {
        auto r=tiles[i].resource;
        if (r.type==NO_RES_TYPE) { deposits[i]=r; continue; }
        const auto old=materialStocksAt(i);
        const auto oldMask=resourceProperties(r.type).materialMask;
        const auto& p=next->properties(static_cast<ResourceId>(r.type));
        const auto& yields=next->yields(static_cast<ResourceId>(r.type));
        std::array<Uint16,MaterialCount> replacement{};
        MaterialMask sources=0;
        r.amount=0;
        for (unsigned m=0;m<MaterialCount;++m)
        {
            const auto& y=yields[m];
            if (!y.capacity) continue;
            replacement[m]=(oldMask&(1u<<m)) ? std::min(old[m],y.capacity) : y.initial;
            r.amount+=replacement[m];
            if (replacement[m] || (p.growthRate && p.ecology!=ResourceEcology::None && y.growthRate)) sources|=MaterialMask(1u<<m);
        }
        if (!r.amount && !p.persistsWhenEmpty) { r.clear(); sources=0; }
        else if (!std::has_single_bit(p.materialMask))
        {
            if (stockIndices.empty()) stockIndices.assign(size,0);
            stocks.push_back(replacement); stockIndices[i]=stocks.size();
        }
        for (unsigned mask=sources;mask;mask&=mask-1) ++counts[std::countr_zero(mask)];
        deposits[i]=r;
    }
    gradientRuntime->pipeline.finish();
    resourceRegistryValue=next;
    resourceHabitatProfiles=std::move(staged.resourceHabitatProfiles);
    resourceHabitatPermissions=std::move(staged.resourceHabitatPermissions);
    resourceHabitatProfileCount=staged.resourceHabitatProfileCount;
    terrainMaterialPermissions=std::move(staged.terrainMaterialPermissions);
    terrainResourceAllowLists=std::move(staged.terrainResourceAllowLists);
    explicitTerrainMaterialPermissions=std::move(staged.explicitTerrainMaterialPermissions);
    terrainFarmResources=std::move(staged.terrainFarmResources);
    resourceStockIndices=std::move(stockIndices);
    resourceStocks=std::move(stocks);
    freeResourceStocks.clear();
    materialSourceCounts=counts;
    for (size_t i=0;i<tiles.size();++i) tiles[i].resource=deposits[i];
    growthCache.invalidate();
    invalidateResourceSeeds();
    bumpTopologyGeneration();
}

ExperimentSet Map::requiredResourceExperiments() const
{
    ExperimentSet result;
    const auto keys=resourceRegistry().experimentKeys();
    std::vector<bool> present(resourceRegistry().size(),false);
    for (const auto& tile:tiles) if (tile.resource.type!=NO_RES_TYPE) present[tile.resource.type]=true;
    for (unsigned id=0;id<present.size();++id)
        if (present[id])
        {
            const auto& key=resourceRegistry().requiredExperiment(static_cast<ResourceId>(id));
            if (!key.empty()) result.set(key,true,keys);
        }
    return result;
}

std::array<Uint16,MaterialCount> Map::materialStocksAt(size_t index) const
{
    std::array<Uint16,MaterialCount> stocks{};
    const auto& r=tiles[index].resource;
    if (r.type==NO_RES_TYPE) return stocks;
    const auto& p=resourceProperties(r.type);
    if (std::has_single_bit(p.materialMask)) stocks[materialIndex(p.primaryMaterial)]=r.amount;
    else if (!resourceStockIndices.empty() && resourceStockIndices[index]) stocks=resourceStocks[resourceStockIndices[index]-1];
    return stocks;
}

MaterialMask Map::resourceMaterialMaskAt(size_t index) const
{
    const auto& r = tiles[index].resource;
    if (r.type == NO_RES_TYPE) return 0;
    const auto& p = resourceProperties(r.type);
    if (std::has_single_bit(p.materialMask))
    {
        if (r.amount) return p.materialMask;
        const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(r.type))[materialIndex(p.primaryMaterial)];
        return p.growthRate && p.ecology!=ResourceEcology::None && yield.growthRate ? p.materialMask : 0;
    }
    auto result = materialMaskAt(index);
    if (p.growthRate && p.ecology!=ResourceEcology::None)
    {
        const auto& yields = resourceRegistry().yields(static_cast<ResourceId>(r.type));
        for (unsigned mask=p.materialMask; mask; mask&=mask-1)
        {
            const auto material=std::countr_zero(mask);
            if (yields[material].growthRate) result|=MaterialMask(1u<<material);
        }
    }
    return result;
}

void Map::releaseResourceStock(size_t index)
{
    if (!resourceStockIndices.empty() && resourceStockIndices[index])
    {
        freeResourceStocks.push_back(resourceStockIndices[index]);
        resourceStockIndices[index]=0;
    }
}

void Map::initializeResourceStock(size_t index)
{
    auto& r=tiles[index].resource;
    if (r.type==NO_RES_TYPE) { r.clear(); return; }
    if (!resourceRegistry().valid(r.type)) throw std::invalid_argument("Unknown resource identity");
    const auto& p=resourceProperties(r.type);
    const auto& yields=resourceRegistry().yields(static_cast<ResourceId>(r.type));
    if (std::has_single_bit(p.materialMask))
    {
        r.amount=std::min<Uint32>(r.amount,yields[materialIndex(p.primaryMaterial)].capacity);
        if (!r.amount && !p.persistsWhenEmpty) r.clear();
        return;
    }
    if (resourceStockIndices.empty()) resourceStockIndices.assign(size,0);
    Uint32 slot;
    if (freeResourceStocks.empty()) { resourceStocks.emplace_back(); slot=resourceStocks.size(); }
    else { slot=freeResourceStocks.back(); freeResourceStocks.pop_back(); }
    resourceStockIndices[index]=slot;
    auto& stocks=resourceStocks[slot-1];
    for (unsigned m=0; m<MaterialCount; ++m) stocks[m]=yields[m].initial;
    refreshResourceTotal(index);
}

void Map::refreshResourceTotal(size_t index)
{
    const auto slot=resourceStockIndices.empty() ? 0 : resourceStockIndices[index];
    if (!slot) return;
    Uint32 total=0;
    for (auto stock : resourceStocks[slot-1]) total+=stock;
    tiles[index].resource.amount=total;
}

void Map::materialStockChanged(size_t index, MaterialMask before)
{
    const auto after=resourceMaterialMaskAt(index);
    for (unsigned changed=before^after;changed;changed&=changed-1)
    {
        const auto material=std::countr_zero(changed);
        if (after&(1u<<material)) ++materialSourceCounts[material];
        else { assert(materialSourceCounts[material]); --materialSourceCounts[material]; }
    }
    resourceSeedChanged(index,ResourceSeedCache::Resource);
}

void Map::rebuildResourceState()
{
    resourceStockIndices.clear(); resourceStocks.clear(); freeResourceStocks.clear(); materialSourceCounts.fill(0);
    for (size_t i=0;i<tiles.size();++i)
    {
        initializeResourceStock(i);
        const auto mask=resourceMaterialMaskAt(i);
        for (unsigned m=0;m<MaterialCount;++m) if (mask&(1u<<m)) ++materialSourceCounts[m];
    }
}

void Map::setMaterialAmount(size_t index,MaterialId materialId,Uint16 amount)
{
    const int material=materialIndex(materialId);
    auto& r=tiles[index].resource;
    if (r.type==NO_RES_TYPE || material<0 || material>=int(MaterialCount)) return;
    const auto& p=resourceProperties(r.type);
    if (!(p.materialMask&(1u<<material))) return;
    const auto oldAmount=materialAmountAt(index,material);
    const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(r.type))[material];
    amount=std::min(amount,yield.capacity);
    if (amount==oldAmount) return;
    const bool crossesZero=(oldAmount==0)!=(amount==0);
    // Only availability transitions affect counters; capture renewable membership
    // before mutation, including an empty persistent stock that can regrow.
    const auto before=crossesZero ? resourceMaterialMaskAt(index) : MaterialMask(0);
    if (std::has_single_bit(p.materialMask)) r.amount=amount;
    else { resourceStocks[resourceStockIndices[index]-1][material]=amount; refreshResourceTotal(index); }
    if (crossesZero) materialStockChanged(index,before);
    if (!r.amount && !p.persistsWhenEmpty) replaceResource(index,Resource{});
}

void Map::setResourceAmount(size_t index, Uint32 amount)
{
    const auto& r=tiles[index].resource;
    if (r.type==NO_RES_TYPE) return;
    const auto& p=resourceProperties(r.type);
    if (!std::has_single_bit(p.materialMask)) throw std::invalid_argument("Set individual material stocks for multi-material resources");
    setMaterialAmount(index,materialIndex(p.primaryMaterial),std::min<Uint32>(amount,65535));
}

bool Map::harvestMaterial(size_t index, int material)
{
    const auto amount=materialAmountAt(index,material);
    if (!amount) return false;
    const auto& r=tiles[index].resource;
    const auto& p=resourceProperties(r.type);
    const auto& y=resourceRegistry().yields(static_cast<ResourceId>(r.type))[material];
    if (y.consumption==ResourceConsumption::All || y.destroysDeposit)
        replaceResource(index,Resource{});
    else if (y.consumption!=ResourceConsumption::Infinite)
    {
        setMaterialAmount(index,material,amount-1);
        if (!tiles[index].resource.amount && !p.persistsWhenEmpty) replaceResource(index,Resource{});
    }
    return true;
}

void Map::decResource(int x,int y)
{
    const auto index=coordToIndex(x,y);
    const auto id=tiles[index].resource.type;
    if (id==NO_RES_TYPE) return;
    const auto& p=resourceProperties(id);
    if (!p.clearable) return;
    if (p.clearConsumption==ResourceConsumption::All || !tiles[index].resource.amount)
    { replaceResource(index,Resource{}); return; }
    unsigned material=materialIndex(p.primaryMaterial);
    if (!materialAmountAt(index,material))
        for (material=0;material<MaterialCount && !materialAmountAt(index,material);++material) {}
    if (material<MaterialCount) setMaterialAmount(index,material,materialAmountAt(index,material)-1);
    if (!tiles[index].resource.amount) replaceResource(index,Resource{});
}

void Map::decResource(int x,int y,int material) { harvestMaterial(coordToIndex(x,y),material); }

void Map::rebuildResourceHabitats()
{
    // Compile compact distinct permission profiles. Historical resource-name
    // restrictions are normalized by TerrainRegistry's import adapter.
    struct Habitat { unsigned mask; bool growth,permanent; bool operator==(const Habitat&) const = default; };
    std::vector<Habitat> profiles;
    resourceHabitatProfiles.resize(resourceRegistry().size());
    for (unsigned id=0;id<resourceRegistry().size();++id)
    {
        const auto& p=resourceProperties(id);
        const Habitat profile{p.habitatMask,p.requiresGrowthTerrain,p.requiresPermanentDepositsTerrain};
        auto it=std::find(profiles.begin(),profiles.end(),profile);
        if (it==profiles.end()) { resourceHabitatProfiles[id]=profiles.size(); profiles.push_back(profile); }
        else resourceHabitatProfiles[id]=it-profiles.begin();
    }
    resourceHabitatProfileCount=profiles.size();
    const auto& terrains=terrainRegistry().propertyProfiles();
    resourceHabitatPermissions.assign(terrains.size()*profiles.size(),0);
    for (size_t t=0;t<terrains.size();++t)
    {
        const auto& terrain=terrains[t];
        unsigned habitats=0;
        if (terrain.walkable && !terrain.shoreline) habitats|=ResourceLand;
        if (terrain.swimmable) habitats|=ResourceAquatic;
        if (terrain.shoreline) habitats|=ResourceShore|ResourceDesert;
        for (size_t p=0;p<profiles.size();++p)
            resourceHabitatPermissions[t*profiles.size()+p]=bool(habitats&profiles[p].mask) &&
                (!profiles[p].growth || terrain.resourcesGrow) && (!profiles[p].permanent || terrain.nonGrowingResources);
    }
    std::vector<MaterialMask> habitatMaterials(profiles.size(),0);
    std::vector<std::array<int,MaterialCount>> habitatCrops(profiles.size());
    for (auto& crops:habitatCrops) crops.fill(NO_RES_TYPE);
    for (unsigned id=0;id<resourceRegistry().size();++id)
    {
        const auto& p=resourceProperties(id);
        const auto profile=resourceHabitatProfiles[id];
        habitatMaterials[profile]|=p.materialMask;
        if (p.farmable)
            for (unsigned m=0;m<MaterialCount;++m)
                if ((p.materialMask&(1u<<m)) && habitatCrops[profile][m]==NO_RES_TYPE) habitatCrops[profile][m]=id;
    }
    terrainMaterialPermissions.assign(terrains.size(),0);
    std::vector<int> profileCrops(terrains.size(),NO_RES_TYPE);
    for (size_t t=0;t<terrains.size();++t)
        for (size_t p=0;p<profiles.size();++p)
            if (resourceHabitatPermissions[t*profiles.size()+p])
            {
                terrainMaterialPermissions[t]|=habitatMaterials[p];
                const unsigned crop=terrains[t].farmMaterial;
                if (crop<MaterialCount) profileCrops[t]=std::min(profileCrops[t],habitatCrops[p][crop]);
            }
    terrainResourceAllowLists.assign(terrainRegistry().size(),nullptr);
    explicitTerrainMaterialPermissions.assign(terrainRegistry().size(),0);
    terrainFarmResources.resize(terrainRegistry().size());
    std::map<std::vector<Uint64>,std::shared_ptr<const std::vector<Uint64>>> uniqueLists;
    for (unsigned t=0;t<terrainRegistry().size();++t)
    {
        const auto terrain=static_cast<TerrainType>(t);
        terrainFarmResources[t]=profileCrops[terrainRegistry().propertyIndex(terrain)];
        const auto& keys=terrainRegistry().resourceKeys(terrain);
        if (!keys) continue;
        terrainFarmResources[t]=NO_RES_TYPE;
        const unsigned crop=terrainRegistry().properties(terrain).farmMaterial;
        auto bits=std::make_shared<std::vector<Uint64>>((resourceRegistry().size()+63)/64,0);
        MaterialMask materials=0;
        for (const auto& key:*keys)
        {
            const auto id=resourceRegistry().find(key);
            if (!id) throw std::invalid_argument("Unknown resource in terrain whitelist: "+key);
            const auto n=resourceIndex(*id);
            if (!resourceHabitatPermissions[size_t(terrainRegistry().propertyIndex(terrain))*profiles.size()+resourceHabitatProfiles[n]]) continue;
            (*bits)[n/64]|=Uint64(1)<<(n%64);
            const auto& p=resourceProperties(n);
            materials|=p.materialMask;
            if (crop<MaterialCount && p.farmable && (p.materialMask&(1u<<crop))) terrainFarmResources[t]=std::min<int>(terrainFarmResources[t],n);
        }
        auto [it,inserted]=uniqueLists.emplace(*bits,bits);
        terrainResourceAllowLists[t]=it->second;
        explicitTerrainMaterialPermissions[t]=materials;
    }
}

bool Map::terrainSupportsMaterialAt(int x,int y,int material) const
{
    if (material<0 || material>=int(MaterialCount)) return false;
    const auto i=coordToIndex(x,y), terrain=static_cast<size_t>(terrainTypeAt(x,y));
    const auto mask=terrainResourceAllowLists[terrain] ? explicitTerrainMaterialPermissions[terrain] : terrainMaterialPermissions[terrainPropertyIndices[i]];
    return (mask&(1u<<material))!=0;
}

std::uint32_t Map::materialGrowthRateAt(size_t index,int material) const
{
    if (!tiles[index].canResourcesGrow || (game && game->gameHeader.isResourceGrowthDisabled())) return 0;
    const auto& r=tiles[index].resource;
    if (r.type==NO_RES_TYPE || material<0 || material>=int(MaterialCount)) return 0;
    const auto& p=resourceProperties(r.type);
    const auto& y=resourceRegistry().yields(static_cast<ResourceId>(r.type))[material];
    if (!y.capacity || !y.growthRate || y.consumption!=ResourceConsumption::One || y.destroysDeposit || materialAmountAt(index,material)>=y.capacity) return 0;
    auto rate=std::uint64_t(resourceGrowthField().rate(index,r.type))*y.growthRate/ResourceRateScale;
    if (p.stockDependentGrowth) rate=rate*(p.stockBranchDivisor-std::min<Uint32>(r.amount,p.stockBranchDivisor))/p.stockBranchDivisor;
    return std::min<std::uint64_t>(rate,4u*ResourceRateScale);
}

std::uint64_t Map::materialExpansionRateAt(size_t index,int material) const
{
    if (game && game->gameHeader.isResourceGrowthDisabled()) return 0;
    const auto& r=tiles[index].resource;
    if (r.type==NO_RES_TYPE || material<0 || material>=int(MaterialCount)) return 0;
    const auto& p=resourceProperties(r.type);
    const auto& y=resourceRegistry().yields(static_cast<ResourceId>(r.type))[material];
    if (!y.initial || !p.spreadRate) return 0;
    auto rate=std::uint64_t(resourceGrowthField().rate(index,r.type))*p.spreadRate/ResourceRateScale;
    if (p.stockDependentGrowth) rate=rate*std::min<Uint32>(r.amount,p.stockBranchDivisor)/p.stockBranchDivisor;
    return rate*((y.consumption==ResourceConsumption::All || y.destroysDeposit) ? 1u : y.initial);
}

bool Map::terrainSupportsResourceAt(size_t index,ResourceId resourceId) const
{
    const int resourceType=resourceIndex(resourceId);
    if (resourceType<0 || !resourceRegistry().valid(unsigned(resourceType))) return false;
    const auto& allowed=terrainResourceAllowLists[static_cast<size_t>(terrainTypeAt(index))];
    if (allowed) return ((*allowed)[resourceType/64]&(Uint64(1)<<(resourceType%64)))!=0;
    return resourceHabitatPermissions[size_t(terrainPropertyIndices[index])*resourceHabitatProfileCount+resourceHabitatProfiles[resourceType]];
}

