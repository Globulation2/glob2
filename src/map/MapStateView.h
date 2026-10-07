// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapState.h"
#include "Material.h"
#include "ResourceRegistry.h"
#include "ResourceHabitats.h"
#include "TerrainRegistry.h"
#include "FertilityField.h"
#include <array>
#include <bit>
#include <span>
#include <vector>

//! No global building identifier (duplicated from Map.h so this header stays light).
#ifndef NOGBID
#define NOGBID 0xFFFF
#endif

// A borrowed, read-only view over authoritative map records. The live Map and
// every engine snapshot expose the same view, so one inline implementation of
// each cell query reads the same arrays directly on both sides.
namespace MapState
{
struct View
{
	int width = 0, height = 0;
	unsigned wDec = 0;
	Uint32 wMask = 0, hMask = 0;
	std::span<const ResourceCell> resources;
	std::span<const OccupancyCell> occupancy;
	std::span<const AreaCell> areas;
	std::span<const TerrainType> terrainIds;
	std::span<const Uint16> legacyTerrain;
	// Multi-yield stock sidecar, referenced through its vectors: the live
	// sidecar grows at runtime, and a cached view must follow it.
	const std::vector<Uint32>* stockIndices = nullptr;
	const std::vector<std::array<Uint16, MaterialCount>>* stocks = nullptr;
	Uint32 stockSlot(std::size_t i) const { return stockIndices && !stockIndices->empty() ? (*stockIndices)[i] : 0; }
	std::span<const Uint32> materialSourceCounts;
	const TerrainRegistry* terrainRegistry = nullptr;
	const ResourceRegistry* resourceRegistry = nullptr;
	const ResourceHabitats* habitats = nullptr;
	const Fertility::GrowthCache* growth = nullptr;
	bool resourceGrowthDisabled = false;
	int resourceScarcityLevel = 0;

	std::size_t index(int x, int y) const { return (std::size_t(Uint32(y) & hMask) << wDec) + (Uint32(x) & wMask); }
	int normalizeX(int x) const { return int(Uint32(x) & wMask); }
	int normalizeY(int y) const { return int(Uint32(y) & hMask); }
	const TerrainProperties& terrainProperties(std::size_t i) const { return terrainRegistry->properties(terrainIds[i]); }
	const ResourceProperties& resourceProperties(unsigned type) const { return resourceRegistry->properties(static_cast<ResourceId>(type)); }
};

inline Uint16 materialAmountAt(const View& v, std::size_t i, int material)
{
	if (material < 0 || material >= int(MaterialCount)) return 0;
	const auto& r = v.resources[i].resource;
	if (r.type == NO_RES_TYPE) return 0;
	const auto& p = v.resourceProperties(r.type);
	if (!(p.materialMask & (1u << material))) return 0;
	if (std::has_single_bit(p.materialMask)) return static_cast<Uint16>(r.amount);
	const auto slot = v.stockSlot(i);
	return slot ? (*v.stocks)[slot - 1][material] : 0;
}
inline Uint16 materialAmountAt(const View& v, std::size_t i, MaterialId material) { return materialAmountAt(v, i, int(materialIndex(material))); }
inline bool hasMaterial(const View& v, std::size_t i, MaterialId material) { return materialAmountAt(v, i, material) > 0; }
inline bool hasMaterialSlot(const View& v, std::size_t i, int material) { return materialAmountAt(v, i, material) > 0; }
inline MaterialMask materialMaskAt(const View& v, std::size_t i)
{
	const auto& r = v.resources[i].resource;
	if (r.type == NO_RES_TYPE) return 0;
	const auto& p = v.resourceProperties(r.type);
	if (std::has_single_bit(p.materialMask)) return r.amount ? p.materialMask : 0;
	const auto slot = v.stockSlot(i);
	if (!slot) return 0;
	const auto& stocks = (*v.stocks)[slot - 1];
	MaterialMask result = 0;
	for (unsigned mask = p.materialMask; mask; mask &= mask - 1)
	{
		const auto material = std::countr_zero(mask);
		if (stocks[material]) result |= MaterialMask(1u << material);
	}
	return result;
}
inline std::array<Uint16, MaterialCount> materialStocksAt(const View& v, std::size_t i)
{
	std::array<Uint16, MaterialCount> stocks{};
	const auto& r = v.resources[i].resource;
	if (r.type == NO_RES_TYPE) return stocks;
	const auto& p = v.resourceProperties(r.type);
	if (std::has_single_bit(p.materialMask)) stocks[materialIndex(p.primaryMaterial)] = Uint16(r.amount);
	else if (const auto slot = v.stockSlot(i)) stocks = (*v.stocks)[slot - 1];
	return stocks;
}
inline bool hasMaterialSource(const View& v, int material)
{ return material >= 0 && material < int(MaterialCount) && !v.materialSourceCounts.empty() && v.materialSourceCounts[material] != 0; }
inline bool resourceBlocksGround(const View& v, std::size_t i)
{ const auto id = v.resources[i].resource.type; return id != NO_RES_TYPE && v.resourceProperties(id).blocksGround; }
inline bool resourceBlocksAir(const View& v, std::size_t i)
{ const auto id = v.resources[i].resource.type; return id != NO_RES_TYPE && v.resourceProperties(id).blocksAir; }
inline bool resourceBlocksBuilding(const View& v, std::size_t i)
{ const auto id = v.resources[i].resource.type; return id != NO_RES_TYPE && v.resourceProperties(id).blocksBuilding; }
inline bool resourceVisibleToHarvest(const View& v, std::size_t i)
{ const auto id = v.resources[i].resource.type; return id != NO_RES_TYPE && v.resourceProperties(id).visibleToHarvest; }
inline bool isFarmableResource(const View& v, int type)
{ return type != NO_RES_TYPE && v.resourceRegistry->valid(unsigned(type)) && v.resourceProperties(type).farmable; }
inline bool terrainSupportsResourceType(const View& v, TerrainType terrain, ResourceId resource)
{
	const auto id = resourceIndex(resource);
	if (!v.terrainRegistry->valid(terrain) || !v.resourceRegistry->valid(id)) return false;
	return v.habitats->supportsResource(terrain, v.terrainRegistry->propertyIndex(terrain), id);
}
inline bool terrainSupportsResource(const View& v, std::size_t i, ResourceId resource)
{
	const auto id = resourceIndex(resource);
	if (!v.resourceRegistry->valid(id)) return false;
	const auto terrain = v.terrainIds[i];
	return v.habitats->supportsResource(terrain, v.terrainRegistry->propertyIndex(terrain), id);
}
inline bool terrainSupportsResourceSlot(const View& v, std::size_t i, int type)
{ return type >= 0 && type < NO_RES_TYPE && terrainSupportsResource(v, i, static_cast<ResourceId>(type)); }
inline bool terrainSupportsMaterial(const View& v, std::size_t i, int material)
{
	if (material < 0 || material >= int(MaterialCount)) return false;
	const auto terrain = v.terrainIds[i];
	return (v.habitats->materialPermissions(terrain, v.terrainRegistry->propertyIndex(terrain)) & (1u << material)) != 0;
}
inline bool terrainSupportsMaterial(const View& v, std::size_t i, MaterialId material) { return terrainSupportsMaterial(v, i, int(materialIndex(material))); }
// Natural growth may place or extend a deposit on this cell.
inline bool resourcesMayGrow(const View& v, std::size_t i) { return v.resources[i].mayGrow && v.terrainProperties(i).resourcesGrow; }
// Expected growth opportunities per visit (over Fertility::kRateScale) for a
// deposit of this type here: zero off its habitat or where nothing grows.
inline std::uint32_t resourceGrowthRate(const View& v, std::size_t i, int resourceType)
{
	if (!v.growth || resourceType < 0 || !v.resourceRegistry->valid(unsigned(resourceType))
		|| !v.terrainProperties(i).resourcesGrow || !terrainSupportsResource(v, i, static_cast<ResourceId>(resourceType))) return 0;
	const auto& p = v.resourceProperties(resourceType);
	return v.growth->rate(i, p.ecology, p.growthRate);
}
inline std::uint32_t materialGrowthRate(const View& v, std::size_t i, int material)
{
	if (!v.growth || !v.resources[i].mayGrow || v.resourceGrowthDisabled) return 0;
	const auto& r = v.resources[i].resource;
	if (r.type == NO_RES_TYPE || material < 0 || material >= int(MaterialCount)) return 0;
	const auto& p = v.resourceProperties(r.type);
	const auto& y = v.resourceRegistry->yields(static_cast<ResourceId>(r.type))[material];
	if (!y.capacity || !y.growthRate || y.consumption != ResourceConsumption::One || y.destroysDeposit || materialAmountAt(v, i, material) >= y.capacity) return 0;
	auto rate = std::uint64_t(resourceGrowthRate(v, i, r.type)) * y.growthRate / ResourceRateScale;
	if (p.stockDependentGrowth) rate = rate * (p.stockBranchDivisor - std::min<Uint32>(r.amount, p.stockBranchDivisor)) / p.stockBranchDivisor;
	return std::uint32_t(std::min<std::uint64_t>(rate, 4u * ResourceRateScale));
}
inline std::uint64_t materialRenewalPotential(const View& v, std::size_t i, int material)
{
	if (!v.growth || v.resourceGrowthDisabled) return 0;
	const auto& r = v.resources[i].resource;
	if (r.type == NO_RES_TYPE || material < 0 || material >= int(MaterialCount)) return 0;
	const auto& p = v.resourceProperties(r.type);
	const auto& y = v.resourceRegistry->yields(static_cast<ResourceId>(r.type))[material];
	if (!y.capacity) return 0;
	const std::uint64_t ecology = resourceGrowthRate(v, i, r.type);
	const bool survives = y.capacity > 1 || p.persistsWhenEmpty || r.amount > materialAmountAt(v, i, material);
	const bool renewableLocal = v.resources[i].mayGrow && survives && y.consumption == ResourceConsumption::One && !y.destroysDeposit;
	const std::uint64_t local = renewableLocal ? ecology * y.growthRate / ResourceRateScale : 0;
	const std::uint64_t offspring = ecology * p.spreadRate / ResourceRateScale
		* ((y.consumption == ResourceConsumption::All || y.destroysDeposit) ? (y.initial ? 1u : 0u) : y.initial);
	if (!p.stockDependentGrowth) return local + offspring;
	const auto stock = std::min<Uint32>(r.amount, p.stockBranchDivisor);
	// Divide the combined branch expectation once: equal local/offspring yields
	// preserve their exact ecology rate at every stock, even for odd rates.
	return (local * (p.stockBranchDivisor - stock) + offspring * stock) / p.stockBranchDivisor;
}
inline std::uint64_t materialExpansionRate(const View& v, std::size_t i, int material)
{
	if (!v.growth || v.resourceGrowthDisabled) return 0;
	const auto& r = v.resources[i].resource;
	if (r.type == NO_RES_TYPE || material < 0 || material >= int(MaterialCount)) return 0;
	const auto& p = v.resourceProperties(r.type);
	const auto& y = v.resourceRegistry->yields(static_cast<ResourceId>(r.type))[material];
	if (!y.initial || !p.spreadRate) return 0;
	auto rate = std::uint64_t(resourceGrowthRate(v, i, r.type)) * p.spreadRate / ResourceRateScale;
	if (p.stockDependentGrowth) rate = rate * std::min<Uint32>(r.amount, p.stockBranchDivisor) / p.stockBranchDivisor;
	return rate * ((y.consumption == ResourceConsumption::All || y.destroysDeposit) ? 1u : y.initial);
}
inline std::uint32_t materialGrowthRate(const View& v, std::size_t i, MaterialId m) { return materialGrowthRate(v, i, int(materialIndex(m))); }
inline std::uint64_t materialRenewalPotential(const View& v, std::size_t i, MaterialId m) { return materialRenewalPotential(v, i, int(materialIndex(m))); }
inline std::uint64_t materialExpansionRate(const View& v, std::size_t i, MaterialId m) { return materialExpansionRate(v, i, int(materialIndex(m))); }
inline bool canResourceEverGrowHere(const View& v, int x, int y, int resourceType)
{ return resourceGrowthRate(v, v.index(x, y), resourceType) != 0; }
// The farmable source of the terrain's configured farm material, preferring a
// compatible living neighbor so the field keeps growing its existing crop.
inline int farmCropAt(const View& v, int x, int y)
{
	const auto i = v.index(x, y);
	const unsigned requested = v.terrainProperties(i).farmMaterial;
	if (requested >= MaterialCount) return NO_RES_TYPE;
	int nearby = NO_RES_TYPE;
	for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
	{
		const auto id = v.resources[v.index(x + dx, y + dy)].resource.type;
		if (id == NO_RES_TYPE) continue;
		const auto& p = v.resourceProperties(id);
		if (p.farmable && (p.materialMask & (1u << requested)) && terrainSupportsResourceSlot(v, i, id)) nearby = std::min<int>(nearby, id);
	}
	return nearby != NO_RES_TYPE ? nearby : v.habitats->farmResource(v.terrainIds[i]);
}
inline bool canPaintFarmArea(const View& v, int x, int y)
{
	const auto i = v.index(x, y);
	if (!resourcesMayGrow(v, i)) return false;
	const auto& r = v.resources[i].resource;
	if (r.type != NO_RES_TYPE && !v.resourceProperties(r.type).clearable && !isFarmableResource(v, r.type)) return false;
	const int crop = farmCropAt(v, x, y);
	return crop != NO_RES_TYPE && canResourceEverGrowHere(v, x, y, crop);
}
// Deposits, buildings and terrain only; units, fog and paint are caller checks.
inline bool hardSpaceForBuildingAt(const View& v, std::size_t i, Uint16 ignoreBuilding = NOGBID)
{
	if (resourceBlocksBuilding(v, i)) return false;
	const auto occupant = v.occupancy[i].building;
	return (occupant == NOGBID || occupant == ignoreBuilding) && v.terrainProperties(i).buildable;
}
} // namespace MapState
