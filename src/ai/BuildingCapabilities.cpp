// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingCapabilities.h"

#include "BuildingType.h"
#include "GameHeader.h"
#include <algorithm>
#include <stdexcept>

namespace AIPlanning
{
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

unsigned recipients(const BuildingType& type, unsigned serviceMask)
{
	return type.maxUnitInside > 0
		? type.semantics.admittedUnitMask & serviceMask & BUILDING_ALL_UNIT_TYPES : 0;
}

unsigned serviceMask(const BuildingType& type, Intent intent)
{
	const auto& semantics = type.semantics;
	const int ability = BuildingCapabilityIndex::trainingAbility(intent);
	if (ability >= 0)
	{
		const auto& training = semantics.training[ability];
		return training.enabled && training.targetLevel > 0
			? recipients(type, training.unitMask) : 0;
	}
	int produced = -1;
	switch (intent)
	{
		case Intent::ProduceWorker: produced = WORKER; break;
		case Intent::ProduceExplorer: produced = EXPLORER; break;
		case Intent::ProduceWarrior: produced = WARRIOR; break;
		case Intent::Feed:
			return semantics.feeding.enabled ? recipients(type, semantics.feeding.unitMask) : 0;
		case Intent::Heal:
			return semantics.healing.enabled ? recipients(type, semantics.healing.unitMask) : 0;
		case Intent::TrainConstruction:
  {
   unsigned mask = 0;
   for (const auto& training : semantics.training)
    if (training.enabled && training.constructionLevel > 0) mask |= recipients(type,training.unitMask);
   return mask;
  }
  case Intent::ProjectileDefense:
		{
			if (type.shootingRange <= 0 || type.shootRhythm <= 0) return 0;
			unsigned mask = semantics.projectileBuildingDamage > 0 ? NoUnitRequired : 0;
			for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
				if (semantics.projectileDamage[unit] > 0) mask |= 1u << unit;
			return mask;
		}
		case Intent::AttractWorkers:
		case Intent::ClearResources: return type.zonable[WORKER] ? 1u << WORKER : 0;
		case Intent::AttractExplorers: return type.zonable[EXPLORER] ? 1u << EXPLORER : 0;
		case Intent::AttractWarriors: return type.zonable[WARRIOR] ? 1u << WARRIOR : 0;
		case Intent::ExchangeResources:
			return semantics.market.interTeamFruitExchange || semantics.market.suppliesStock ? NoUnitRequired : 0;
		default: return 0;
	}
	return semantics.production.recipes[produced].enabled ? 1u << produced : 0;
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
	: catalog_(catalog), masks_(catalog.size()), intentMasks_(catalog.size()), lineageRoots_(catalog.size(), -1),
	  lineagePositions_(catalog.size(), 0)
{
	for (std::size_t id = 0; id < catalog.size(); ++id)
	{
		const auto& type = *catalog.get(id);
		if (type.isBuildingSite) continue;
		for (std::size_t demand = 0; demand < IntentCount; ++demand)
			if ((masks_[id][demand] = serviceMask(type, static_cast<Intent>(demand))))
			{
				intentMasks_[id] |= std::uint64_t(1) << demand;
				providers_[demand].push_back(static_cast<int>(id));
			}
	}
	for (std::size_t id = 0; id < catalog.size(); ++id)
	{
		const auto& type = *catalog.get(id);
		if (!type.semantics.placeable) continue;
		int position = 0;
		for (int current = static_cast<int>(id); current >= 0
			&& static_cast<std::size_t>(current) < catalog.size()
			&& lineageRoots_[current] < 0; current = catalog.get(current)->nextLevel)
		{
			const auto& variant = *catalog.get(current);
			lineageRoots_[current] = static_cast<int>(id);
			lineagePositions_[current] = variant.isBuildingSite ? position + 1 : ++position;
		}
		const int complete = type.isBuildingSite ? type.nextLevel : static_cast<int>(id);
		if (complete < 0 || static_cast<std::size_t>(complete) >= catalog.size()
			|| catalog.get(complete)->isBuildingSite)
			throw std::invalid_argument("Placeable building has no completed variant");
		for (std::size_t demand = 0; demand < IntentCount; ++demand)
			if (masks_[complete][demand])
				placements_[demand].push_back({static_cast<int>(id), complete});
	}
}

const std::vector<int>& BuildingCapabilityIndex::providers(BuildingIntent intent) const
{
	return providers_[index(intent)];
}

const std::vector<BuildingCandidate>& BuildingCapabilityIndex::placements(BuildingIntent intent) const
{
	return placements_[index(intent)];
}

bool BuildingCapabilityIndex::matches(int type, BuildingIntent intent, int unit) const
{
	if (type < 0 || static_cast<std::size_t>(type) >= masks_.size()) return false;
	const unsigned mask = masks_[type][index(intent)];
	return unit == -1 ? mask != 0 : unit >= 0 && unit < NB_UNIT_TYPE && (mask & (1u << unit)) != 0;
}

std::uint64_t BuildingCapabilityIndex::intentMask(int type) const
{
	return type >= 0 && static_cast<std::size_t>(type) < intentMasks_.size() ? intentMasks_[type] : 0;
}

int BuildingCapabilityIndex::lineageRoot(int type) const
{
	return type >= 0 && static_cast<std::size_t>(type) < lineageRoots_.size() ? lineageRoots_[type] : -1;
}

int BuildingCapabilityIndex::lineagePosition(int type) const
{
	return type >= 0 && static_cast<std::size_t>(type) < lineagePositions_.size() ? lineagePositions_[type] : 0;
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
		return market.interTeamFruitExchange
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
