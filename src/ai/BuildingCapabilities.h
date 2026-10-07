// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "UnitConsts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

class BuildingsTypes;
struct BuildingType;
class GameHeader;
class Game;
class Team;
class Order;

namespace AIPlanning
{
// Strategic demands, not exclusive building categories. One concrete variant
// can fulfill any number of these demands.
enum class BuildingIntent : unsigned
{
	ProduceWorker, ProduceExplorer, ProduceWarrior,
	Feed, Heal,
	TrainWalk, TrainSwim, TrainFly, TrainBuild, TrainHarvest,
	TrainAttackSpeed, TrainAttackStrength, TrainAirAttack, TrainBombing,
	TrainCreateWood, TrainCreateWheat, TrainCreateAlgae, TrainArmor, TrainHealth,
	ProjectileDefense, AttractWorkers, AttractExplorers, AttractWarriors,
	ClearResources, ExchangeResources, TrainConstruction,
	Count
};

struct BuildingCandidate
{
	int placementType = -1;
	int completedType = -1;
	bool operator==(const BuildingCandidate&) const = default;
};

// Retiring one attraction must preserve unrelated services and other unit roles.
bool hasIndependentAttractionUse(const BuildingType& type,unsigned retiringUnitMask);

// Cold strategy fallback for a nonzero production demand whose output class has
// no current or pending provider. Pending placement IDs include queued AI plans.
std::shared_ptr<Order> missingProductionOrder(Game& game, Team& team,
    const std::array<int, NB_UNIT_TYPE>& desired, int workers, int futureWorkers,
    const std::vector<int>& pendingPlacements = {});

// Immutable cached capability data, shared by the authoritative catalog index
// and snapshot readers. Contains values only, with no live catalog binding.
class BuildingCapabilityTables
{
    friend class BuildingCapabilityIndex;
    static constexpr std::size_t IntentCount = static_cast<std::size_t>(BuildingIntent::Count);
    using ServiceMasks = std::array<unsigned, IntentCount>;
    std::vector<ServiceMasks> masks_;
    std::vector<std::uint64_t> intentMasks_;
    std::vector<int> lineageRoots_, lineagePositions_;
    std::array<std::vector<int>, IntentCount> providers_;
    std::array<std::vector<BuildingCandidate>, IntentCount> placements_, placementsByCost_;
public:
    explicit BuildingCapabilityTables(std::size_t count)
        : masks_(count), intentMasks_(count), lineageRoots_(count, -1), lineagePositions_(count, 0) {}
    const std::vector<int>& providers(BuildingIntent intent) const;
    const std::vector<BuildingCandidate>& placements(BuildingIntent intent) const;
    const std::vector<BuildingCandidate>& placementsByCost(BuildingIntent intent) const;
    bool matches(int type, BuildingIntent intent, int unit = -1) const;
    std::uint64_t intentMask(int type) const
    { return type >= 0 && static_cast<std::size_t>(type) < intentMasks_.size() ? intentMasks_[type] : 0; }
    int lineageRoot(int type) const;
    int lineagePosition(int type) const
    { return type >= 0 && static_cast<std::size_t>(type) < lineagePositions_.size() ? lineagePositions_[type] : 0; }
    std::size_t capacityBytes() const
    {
        std::size_t bytes=masks_.capacity()*sizeof(ServiceMasks)+intentMasks_.capacity()*sizeof(std::uint64_t)
            +(lineageRoots_.capacity()+lineagePositions_.capacity())*sizeof(int);
        for(std::size_t i=0;i<IntentCount;++i) bytes+=providers_[i].capacity()*sizeof(int)
            +(placements_[i].capacity()+placementsByCost_[i].capacity())*sizeof(BuildingCandidate);
        return bytes;
    }
};

// Build once after the game's catalog is installed. The catalog must outlive
// this index and remain immutable; rebuild the index when replacing a catalog.
// Lookups never scan all definitions, allocate, consume RNG or resolve names.
class BuildingCapabilityIndex
{
public:
	explicit BuildingCapabilityIndex(const BuildingsTypes& catalog);
	const std::vector<int>& providers(BuildingIntent intent) const { return data_->providers(intent); }
	const std::vector<BuildingCandidate>& placements(BuildingIntent intent) const { return data_->placements(intent); }
	const std::vector<BuildingCandidate>& placementsByCost(BuildingIntent intent) const { return data_->placementsByCost(intent); }
	// unitType == -1 accepts any supported recipient/output/target. Otherwise
	// admission and the service's own eligibility must both permit that unit.
	bool matches(int completedType, BuildingIntent intent, int unitType = -1) const { return data_->matches(completedType,intent,unitType); }
	// Rules gate the requested operation, never every service of its building.
	bool available(int completedType, BuildingIntent intent, const GameHeader& rules,
		int unitType = -1) const;
	bool available(const BuildingCandidate& candidate, BuildingIntent intent,
		const GameHeader& rules, int unitType = -1) const;
	// Forward paths start at explicitly placeable variants. Repair links do not
	// define upgrade ancestry. Shared descendants use the smallest starting ID.
	std::uint64_t intentMask(int type) const
	{
		return data_->intentMask(type);
	}
	int lineageRoot(int type) const { return data_->lineageRoot(type); }
	int lineagePosition(int type) const
	{
		return data_->lineagePosition(type);
	}
	std::shared_ptr<const BuildingCapabilityTables> frozenTables() const { return data_; }
	static bool allowed(BuildingIntent intent, const GameHeader& rules);
	static int trainingAbility(BuildingIntent intent);

private:
	static constexpr std::size_t IntentCount = static_cast<std::size_t>(BuildingIntent::Count);
	static_assert(IntentCount <= 64, "Building intents must fit the cached uint64_t mask");
	const BuildingsTypes& catalog_;
	std::shared_ptr<BuildingCapabilityTables> data_;
};
}
