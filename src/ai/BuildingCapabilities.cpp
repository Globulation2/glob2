// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingCapabilities.h"

#include "BuildingType.h"
#include "GameHeader.h"
#include "Game.h"
#include "Building.h"
#include "TeamStat.h"
#include "AIRuleOrders.h"
#include <tuple>
#include <algorithm>
#include <stdexcept>

namespace AIPlanning
{
bool hasIndependentAttractionUse(const BuildingType& type,unsigned retiringUnitMask)
{
    const auto& spec=type.semantics;
    if (type.runtimeAttractionRoles & ~retiringUnitMask) return true;
    return spec.feeding.enabled || spec.healing.enabled || type.shootingRange>0
        || spec.market.interTeamFruitExchange || spec.market.suppliesStock || spec.market.suppliesDirectStock
        || !spec.production.enabledUnits.empty()
        || std::any_of(spec.training.begin(),spec.training.end(),[](const auto& r){return r.enabled;});
}

namespace
{
// Production intent bits deliberately share the unit-class positions so a
// provider's cached mask can answer a multi-class request with one bitwise AND.
static_assert(static_cast<unsigned>(BuildingIntent::ProduceWorker) == WORKER);
static_assert(static_cast<unsigned>(BuildingIntent::ProduceExplorer) == EXPLORER);
static_assert(static_cast<unsigned>(BuildingIntent::ProduceWarrior) == WARRIOR);
static_assert(NB_UNIT_TYPE == 3, "Extend production intents when adding unit classes");
constexpr unsigned ProductionIntentMask = (1u << NB_UNIT_TYPE) - 1;
}

std::shared_ptr<Order> missingProductionOrder(Game& game, Team& team,
    const std::array<int, NB_UNIT_TYPE>& desired, int workers, int futureWorkers,
    const std::vector<int>& pendingPlacements)
{
    const auto& index = game.buildingCapabilities();
    unsigned required = 0, provided = 0;
    for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
        if (desired[unit] > 0 && BuildingCapabilityIndex::allowed(static_cast<BuildingIntent>(unit), game.gameHeader))
            required |= 1u << unit;
    if (!required) return {};

    Building* anchor = nullptr;
    auto includeProvider = [&](int type) {
        if (type < 0 || std::size_t(type) >= game.buildingsTypes.size()) return;
        const auto* descriptor = game.buildingsTypes.get(type);
        if (descriptor->isBuildingSite) type = descriptor->nextLevel;
        provided |= unsigned(index.intentMask(type)) & ProductionIntentMask;
    };
    for (int id = 0; id < Building::MAX_COUNT; ++id)
    {
        auto* building = team.myBuildings[id];
        if (!building || building->buildingState != Building::ALIVE) continue;
        includeProvider(building->type->isBuildingSite
            ? building->getConstructionCompletionTypeNum() : building->typeNum);
        if (!anchor || (index.intentMask(building->typeNum) & ProductionIntentMask)) anchor = building;
        if ((provided & required) == required) return {};
    }
    for (int type : pendingPlacements) includeProvider(type);
    if ((provided & required) == required || !anchor) return {};

    for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
    {
        if (!(required & (1u << unit)) || (provided & (1u << unit))) continue;
        const auto intent = static_cast<BuildingIntent>(unit);
        for (const auto& candidate : index.placementsByCost(intent))
        {
            const auto* type = game.buildingsTypes.get(candidate.placementType);
            int eligible = 0;
            for (int level = type->semantics.requiredWorkerLevel; level < NB_UNIT_LEVELS; ++level)
                eligible += team.stats.getWorkersLevel(level);
            if (!index.available(candidate, intent, game.gameHeader) || eligible == 0) continue;
            // Try each provider in cached cost/ID order. An obstructed large
            // footprint must not hide a smaller usable alternative.
            for (int radius = 1; radius <= 32; ++radius)
                for (int dx = -radius; dx <= radius; ++dx)
                    for (int dy = -radius; dy <= radius; ++dy)
                    {
                        if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                        const int x = game.map.normalizeX(anchor->posX + dx);
                        const int y = game.map.normalizeY(anchor->posY + dy);
                        if (!game.map.isMapDiscovered(x, y, team.allies)
                            || !game.checkRoomForBuilding(x, y, type, team.teamNumber)) continue;
                        return AIRules::createOrder(game, team.teamNumber, x, y,
                            candidate.placementType, workers, futureWorkers);
                    }
        }
    }
    return {};
}

namespace
{
using Intent = BuildingIntent;
constexpr unsigned NoUnitRequired = 1u << NB_UNIT_TYPE;

std::size_t index(Intent intent)
{
	const auto value = static_cast<std::size_t>(intent);
	if (value >= static_cast<std::size_t>(Intent::Count))
		throw std::invalid_argument("Unknown building intent");
	return value;
}

bool experimentEnabled(const std::string& key, const GameHeader& rules)
{
	return key.empty() || rules.getExperiments().has(key);
}

bool serviceRecipient(const BuildingsTypes& catalog, const BuildingType& type, Intent intent, unsigned unit)
{
    const auto& s=type.semantics;
    const bool admitted=type.maxUnitInside>0 && s.admittedUnits.matches(unit,s.admittedUnitMask);
    const int ability=BuildingCapabilityIndex::trainingAbility(intent);
    if(ability>=0) {
        const auto& training=s.training[ability];
        return admitted && training.enabled && training.targetLevel>0 && training.units.matches(unit,training.unitMask)
            && (catalog.unitTrainingAbilities(unit)&(1u<<ability));
    }
    switch(intent) {
    case Intent::ProduceWorker:case Intent::ProduceExplorer:case Intent::ProduceWarrior:
        return unit==unsigned(intent) && unit<s.production.recipes.size() && s.production.recipes[unit].enabled;
    case Intent::Feed:return admitted && s.feeding.enabled && s.feeding.units.matches(unit,s.feeding.unitMask);
    case Intent::Heal:return admitted && s.healing.enabled && s.healing.units.matches(unit,s.healing.unitMask);
    case Intent::TrainConstruction:
        for(unsigned ability=0;ability<NB_ABILITY;++ability) {
            const auto& t=s.training[ability];
            if(admitted && t.enabled && t.constructionLevel>0 && t.units.matches(unit,t.unitMask)
                && (catalog.unitConstructionTrainingAbilities(unit)&(1u<<ability)))return true;
        }
        return false;
    case Intent::ProjectileDefense:
        return type.shootingRange>0 && type.shootRhythm>0 && (unit<s.resolvedProjectileDamage.size()?s.resolvedProjectileDamage[unit]>0:unit<3 && s.projectileDamage[unit]>0);
    case Intent::AttractWorkers:case Intent::ClearResources:case Intent::AttractExplorers:case Intent::AttractWarriors: {
        const int role=intent==Intent::AttractExplorers?1:intent==Intent::AttractWarriors?2:0;
        const auto& selection=s.attractionUnits[role];
        return selection.resolved.empty()?selection.matches(unit,type.zonable[role]?(1u<<role):0):unit<selection.resolved.size() && selection.resolved[unit];
    }
    case Intent::ExchangeResources:return false;
    default:return false;
    }
}

std::vector<std::uint8_t> serviceMask(const BuildingsTypes& catalog, const BuildingType& type, Intent intent,unsigned count)
{
    std::vector<std::uint8_t> result(count+1);
    for(unsigned id=0;id<count;++id)result[id]=serviceRecipient(catalog,type,intent,id);
    const auto& s=type.semantics;
    result[count]=(intent==Intent::ExchangeResources && (s.market.interTeamFruitExchange || s.market.suppliesStock || s.market.suppliesDirectStock))
        || (intent==Intent::ProjectileDefense && type.shootingRange>0 && type.shootRhythm>0 && s.projectileBuildingDamage>0);
    return result;
}

}

int BuildingCapabilityIndex::trainingAbility(BuildingIntent intent)
{
	switch (intent)
	{
		case Intent::TrainWalk: return WALK;
		case Intent::TrainSwim: return SWIM;
		case Intent::TrainFly: return FLY;
		case Intent::TrainBuild: return BUILD;
		case Intent::TrainHarvest: return HARVEST;
		case Intent::TrainAttackSpeed: return ATTACK_SPEED;
		case Intent::TrainAttackStrength: return ATTACK_STRENGTH;
		case Intent::TrainAirAttack: return MAGIC_ATTACK_AIR;
		case Intent::TrainBombing: return MAGIC_ATTACK_GROUND;
		case Intent::TrainCreateWood: return MAGIC_CREATE_WOOD;
		case Intent::TrainCreateWheat: return MAGIC_CREATE_WHEAT;
		case Intent::TrainCreateAlgae: return MAGIC_CREATE_ALGA;
		case Intent::TrainArmor: return ARMOR;
		case Intent::TrainHealth: return HP;
		default: return -1;
	}
}

BuildingCapabilityIndex::BuildingCapabilityIndex(const BuildingsTypes& catalog)
	: catalog_(catalog), data_(std::make_shared<BuildingCapabilityTables>(catalog.size()))
{
	for (std::size_t id = 0; id < catalog.size(); ++id)
	{
		const auto& type = *catalog.get(id);
		if (type.isBuildingSite) continue;
        for(std::size_t demand=0;demand<IntentCount;++demand) {
            auto& mask=data_->masks_[id][demand];
            mask=serviceMask(catalog,type,static_cast<Intent>(demand),catalog.getRuntime(id)->unitCount);
            if(std::any_of(mask.begin(),mask.end(),[](auto value){return value!=0;})) {
                data_->intentMasks_[id]|=std::uint64_t(1)<<demand;
                data_->providers_[demand].push_back(int(id));
            }
        }

	}
	for (std::size_t id = 0; id < catalog.size(); ++id)
	{
		const auto& type = *catalog.get(id);
		if (!type.semantics.placeable) continue;
		int position = 0;
		for (int current = static_cast<int>(id); current >= 0
			&& static_cast<std::size_t>(current) < catalog.size()
			&& data_->lineageRoots_[current] < 0; current = catalog.get(current)->nextLevel)
		{
			const auto& variant = *catalog.get(current);
			data_->lineageRoots_[current] = static_cast<int>(id);
			data_->lineagePositions_[current] = variant.isBuildingSite ? position + 1 : ++position;
		}
		const int complete = type.isBuildingSite ? type.nextLevel : static_cast<int>(id);
		if (complete < 0 || static_cast<std::size_t>(complete) >= catalog.size()
			|| catalog.get(complete)->isBuildingSite)
			throw std::invalid_argument("Placeable building has no completed variant");
		for (std::size_t demand = 0; demand < IntentCount; ++demand)
			if (data_->intentMasks_[complete] & (std::uint64_t(1)<<demand))
				data_->placements_[demand].push_back({static_cast<int>(id), complete});
	}
    // This fallback ranks the unweighted construction resource total, then ID.
    // Strategies with labor/throughput models can use their own valuation.
    data_->placementsByCost_ = data_->placements_;
    std::vector<int> costs(catalog.size());
    for (std::size_t id = 0; id < catalog.size(); ++id)
        for (int amount : catalog.get(id)->semantics.constructionCost) costs[id] += amount;
    for (auto& candidates : data_->placementsByCost_)
        std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
            return std::tie(costs[a.placementType], a.placementType)
                < std::tie(costs[b.placementType], b.placementType);
        });
}

const std::vector<int>& BuildingCapabilityTables::providers(BuildingIntent intent) const
{
	return providers_[index(intent)];
}

const std::vector<BuildingCandidate>& BuildingCapabilityTables::placements(BuildingIntent intent) const
{
	return placements_[index(intent)];
}

const std::vector<BuildingCandidate>& BuildingCapabilityTables::placementsByCost(BuildingIntent intent) const
{
    return placementsByCost_[index(intent)];
}

bool BuildingCapabilityTables::matches(int type, BuildingIntent intent, int unit) const
{
	if (type < 0 || static_cast<std::size_t>(type) >= masks_.size()) return false;
	const auto& mask=masks_[type][index(intent)];
    return unit==-1 ? bool(intentMasks_[type]&(std::uint64_t(1)<<index(intent)))
        : unit>=0 && std::size_t(unit)+1<mask.size() && mask[unit]!=0;
}

int BuildingCapabilityTables::lineageRoot(int type) const
{
	return type >= 0 && static_cast<std::size_t>(type) < lineageRoots_.size() ? lineageRoots_[type] : -1;
}

bool BuildingCapabilityIndex::allowed(BuildingIntent intent, const GameHeader& rules)
{
	index(intent);
	if (rules.isHungerDisabled() && intent == Intent::Feed) return false;
	if (rules.isUnitUpgradesDisabled() && (trainingAbility(intent) >= 0 || intent == Intent::TrainConstruction)) return false;
	if (rules.isPeacefulModeEnabled())
		switch (intent)
		{
			case Intent::ProduceWarrior: case Intent::TrainAttackSpeed:
			case Intent::TrainAttackStrength: case Intent::TrainAirAttack:
			case Intent::TrainBombing: case Intent::ProjectileDefense:
			case Intent::AttractWarriors: return false;
			default: break;
		}
	return true;
}

bool BuildingCapabilityIndex::available(int type, BuildingIntent intent,
	const GameHeader& rules, int unit) const
{
	if (!matches(type, intent, unit) || !allowed(intent, rules)) return false;
	const auto& definition = *catalog_.get(type);
	if (!experimentEnabled(definition.requiredExperiment, rules)) return false;
	if (intent == Intent::ExchangeResources)
	{
		const auto& market = definition.semantics.market;
		return market.interTeamFruitExchange || market.suppliesDirectStock
			|| (market.suppliesStock && experimentEnabled(market.suppliesStockExperiment, rules));
	}
	return true;
}

bool BuildingCapabilityIndex::available(const BuildingCandidate& candidate,
	BuildingIntent intent, const GameHeader& rules, int unit) const
{
	if (candidate.placementType < 0 || static_cast<std::size_t>(candidate.placementType) >= catalog_.size()) return false;
	const auto& placement = *catalog_.get(candidate.placementType);
	return placement.semantics.placeable
		&& (placement.isBuildingSite ? placement.nextLevel : candidate.placementType) == candidate.completedType
		&& experimentEnabled(placement.requiredExperiment, rules)
		&& available(candidate.completedType, intent, rules, unit);
}
}
