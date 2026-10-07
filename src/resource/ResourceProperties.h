// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Material.h"
#include <array>
#include <cstdint>

enum class ResourceId : std::uint16_t {};
inline constexpr ResourceId NoResource = static_cast<ResourceId>(65535);
constexpr unsigned resourceIndex(ResourceId resource) { return static_cast<unsigned>(resource); }

enum class ResourceEcology : std::uint8_t { None, Land, Shore, Uniform };
enum class ResourceConsumption : std::uint8_t { One, All, Infinite };
enum ResourceHabitat : std::uint16_t { ResourceLand = 1, ResourceAquatic = 2, ResourceShore = 4, ResourceDesert = 8 };
// Exact denominator shared with Fertility::kRateScale. Integer authoring avoids
// host-dependent floating-point rounding and preserves the one-third wheat rate.
inline constexpr std::uint32_t ResourceRateScale = 3u * 65536u;

struct YieldProperties
{
    std::uint16_t capacity = 0;
    std::uint16_t initial = 0;
    std::uint16_t seedReserve = 1;
    std::uint16_t placementMaximum = 0; // Zero selects exactly initial stock.
    std::uint32_t growthRate = ResourceRateScale;
    ResourceConsumption consumption = ResourceConsumption::One;
    bool destroysDeposit = false;
    bool operator==(const YieldProperties&) const = default;
};
using ResourceYields = std::array<YieldProperties, MaterialCount>;

// Hot resource-wide traits. Yield arrays, strings and sprites live separately so
// mobility/availability queries do not fetch an inventory-sized configuration.
struct ResourceProperties
{
    std::uint32_t growthRate = 0;
    std::uint32_t spreadRate = 0;
    MaterialMask materialMask = 0;
    std::uint16_t habitatMask = ResourceLand;
    std::uint16_t stockBranchDivisor = 8;
    MaterialId primaryMaterial = MaterialId::Wood;
    ResourceEcology ecology = ResourceEcology::None;
    ResourceConsumption clearConsumption = ResourceConsumption::All;
    bool blocksGround = true;
    bool blocksAir = false;
    bool blocksBuilding = true;
    bool clearable = true;
    bool visibleToHarvest = false;
    bool persistsWhenEmpty = false;
    bool farmable = false;
    bool stockDependentGrowth = true;
    bool smoothPlacement = false;
    bool requiresGrowthTerrain = false;
    bool requiresPermanentDepositsTerrain = false;
    bool operator==(const ResourceProperties&) const = default;
};
static_assert(sizeof(ResourceProperties) <= 32);
