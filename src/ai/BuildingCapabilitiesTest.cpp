// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "BuildingCapabilities.h"
#include "BuildingType.h"
#include "GameHeader.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

namespace
{
using AIPlanning::BuildingCapabilityIndex;
using AIPlanning::BuildingIntent;

std::set<std::string> providerKeys(const BuildingsTypes& catalog,
	const BuildingCapabilityIndex& index, BuildingIntent intent)
{
	std::set<std::string> keys;
	for (int id : index.providers(intent)) keys.insert(catalog.get(id)->key);
	return keys;
}
}

TEST_SUITE("BuildingCapabilities")
{
	TEST_CASE("stock demand lookup distinguishes finished providers and placements")
	{
		BuildingsTypes catalog;
		catalog.initLegacy();
		BuildingCapabilityIndex index(catalog);
		for (const auto& [intent, family] : {
			std::pair{BuildingIntent::Feed, "inn"},
			{BuildingIntent::Heal, "hospital"},
			{BuildingIntent::TrainWalk, "racetrack"},
			{BuildingIntent::TrainSwim, "swimmingpool"},
			{BuildingIntent::TrainBuild, "school"},
			{BuildingIntent::TrainAttackStrength, "barracks"},
			{BuildingIntent::ProjectileDefense, "defencetower"}})
		{
			CAPTURE(family);
			CHECK(index.providers(intent).size() == 3);
			REQUIRE(index.placements(intent).size() == 1);
			const auto& candidate = index.placements(intent).front();
			CHECK(candidate.placementType == catalog.getTypeNum(family, 0, true));
			CHECK(candidate.completedType == catalog.getTypeNum(family, 0, false));
			CHECK_FALSE(index.matches(candidate.placementType, intent));
		}
		REQUIRE(index.placements(BuildingIntent::AttractWarriors).size() == 1);
		const auto flag = index.placements(BuildingIntent::AttractWarriors).front();
		CHECK(flag.placementType == flag.completedType);
		CHECK(index.matches(flag.completedType, BuildingIntent::AttractWarriors, WARRIOR));
		CHECK_FALSE(index.matches(flag.completedType, BuildingIntent::AttractWarriors, WORKER));
		CHECK(index.providers(BuildingIntent::TrainBombing).size() == 1);
		CHECK(index.placements(BuildingIntent::TrainBombing).empty());
		CHECK(index.providers(BuildingIntent::TrainCreateWood).empty());
	}

	TEST_CASE("fetching stock is not an exchange or supply service")
	{
		BuildingsTypes catalog; catalog.initLegacy();
		const int inn=catalog.getTypeNum("inn",0,false);
		REQUIRE(catalog.get(inn)->semantics.market.fetchesStock);
		BuildingCapabilityIndex index(catalog);
		CHECK_FALSE(index.matches(inn,BuildingIntent::ExchangeResources));
		GameHeader rules; rules.getExperiments().set(ExperimentId::MarketsV2);
		CHECK_FALSE(index.available(inn,BuildingIntent::ExchangeResources,rules));
		const int market=catalog.getTypeNum("market",0,false);
		CHECK(index.available(market,BuildingIntent::ExchangeResources,rules));
	}

	TEST_CASE("lineage follows forward transitions without treating repair links as ancestry")
	{
		BuildingsTypes catalog; catalog.initLegacy();
		const int initial=catalog.getTypeNum("inn",0,true);
		const int second=catalog.getTypeNum("inn",1,false);
		catalog.get(second)->prevLevel=catalog.getTypeNum("hospital",0,false);
		BuildingCapabilityIndex index(catalog);
		CHECK(index.lineageRoot(second)==initial);
		CHECK(index.lineagePosition(initial)==1);
		CHECK(index.lineagePosition(second)==2);
		CHECK(index.lineageRoot(-1)==-1);
		CHECK(index.lineagePosition(-1)==0);
	}

 TEST_CASE("construction qualification grants are independent from building speed")
 {
  BuildingsTypes catalog; catalog.initLegacy();
  const int id=catalog.getTypeNum("inn",0,false);
  auto& type=*catalog.get(id);
  type.semantics.training[WALK].enabled=true;
  type.semantics.training[WALK].targetLevel=1;
  type.semantics.training[WALK].constructionLevel=2;
  type.semantics.training[WALK].unitMask=1u<<WORKER;
  BuildingCapabilityIndex index(catalog);
  GameHeader rules;
  CHECK(index.matches(id,BuildingIntent::TrainConstruction,WORKER));
  CHECK_FALSE(index.matches(id,BuildingIntent::TrainConstruction,WARRIOR));
  CHECK_FALSE(index.matches(id,BuildingIntent::TrainBuild));
  rules.setUnitUpgradesDisabled(true);
  CHECK_FALSE(index.available(id,BuildingIntent::TrainConstruction,rules));
  CHECK(index.available(id,BuildingIntent::Feed,rules));
 }

	TEST_CASE("combined providers retain independent admission and operation rule gates")
	{
		BuildingsTypes catalog;
		catalog.initLegacy();
		const int complete = catalog.getTypeNum("inn", 0, false);
		auto& type = *catalog.get(complete);
		type.type = "combined-provider";
		type.shortTypeNum = 1000; // No strategy-family identity is consulted.
		type.semantics.admittedUnitMask = (1u << WORKER) | (1u << WARRIOR);
		type.semantics.feeding.unitMask = 1u << WORKER;
		type.semantics.healing.enabled = true;
		type.semantics.healing.unitMask = 1u << WARRIOR;
		type.semantics.production.recipes[EXPLORER].enabled = true;
		type.semantics.training[ATTACK_STRENGTH].enabled = true;
		type.semantics.training[ATTACK_STRENGTH].targetLevel = 1;
		type.semantics.training[ATTACK_STRENGTH].unitMask = 1u << WARRIOR;
		BuildingCapabilityIndex index(catalog);
		CHECK(index.matches(complete, BuildingIntent::Feed, WORKER));
		CHECK_FALSE(index.matches(complete, BuildingIntent::Feed, WARRIOR));
		CHECK_FALSE(index.matches(complete, BuildingIntent::Feed, EXPLORER));
		CHECK(index.matches(complete, BuildingIntent::Heal, WARRIOR));
		CHECK_FALSE(index.matches(complete, BuildingIntent::Heal, WORKER));
		CHECK(index.matches(complete, BuildingIntent::ProduceExplorer));
		CHECK_FALSE(index.matches(complete, BuildingIntent::ProduceWorker));
		CHECK(index.matches(complete, BuildingIntent::TrainAttackStrength, WARRIOR));
		for (const auto intent : {BuildingIntent::Feed, BuildingIntent::Heal,
			BuildingIntent::ProduceExplorer, BuildingIntent::TrainAttackStrength})
			CHECK(std::count(index.providers(intent).begin(), index.providers(intent).end(), complete) == 1);
		GameHeader rules;
		rules.setUnitUpgradesDisabled(true);
		CHECK_FALSE(index.available(complete, BuildingIntent::TrainAttackStrength, rules));
		CHECK(index.available(complete, BuildingIntent::Heal, rules));
		CHECK(index.available(complete, BuildingIntent::Feed, rules));
		rules.setHungerDisabled(true);
		CHECK_FALSE(index.available(complete, BuildingIntent::Feed, rules));
		CHECK(index.available(complete, BuildingIntent::Heal, rules));
		rules.setPeacefulModeEnabled(true);
		CHECK(index.available(complete, BuildingIntent::ProduceExplorer, rules));
		CHECK_FALSE(BuildingCapabilityIndex::allowed(BuildingIntent::ProduceWarrior, rules));
	}

	TEST_CASE("experiment gates apply to both initial placement and completion without rebuilding")
	{
		BuildingsTypes catalog;
		catalog.initLegacy();
		const int site = catalog.getTypeNum("inn", 0, true);
		const int complete = catalog.getTypeNum("inn", 0, false);
		catalog.get(site)->requiredExperiment = "markets-v2";
		catalog.get(complete)->requiredExperiment = "guard-area-balancing";
		BuildingCapabilityIndex index(catalog);
		const AIPlanning::BuildingCandidate candidate{site, complete};
		GameHeader rules;
		CHECK_FALSE(index.available(candidate, BuildingIntent::Feed, rules));
		rules.getExperiments().set(ExperimentId::MarketsV2);
		CHECK_FALSE(index.available(candidate, BuildingIntent::Feed, rules));
		rules.getExperiments().set(ExperimentId::GuardAreaBalancing);
		CHECK(index.available(candidate, BuildingIntent::Feed, rules));
		CHECK_FALSE(index.available({site, site}, BuildingIntent::Feed, rules));
		CHECK_FALSE(index.matches(-1, BuildingIntent::Feed));
		CHECK_FALSE(index.matches(100000, BuildingIntent::Feed));
	}

	TEST_CASE("renamed families and reordered concrete IDs preserve capability membership")
	{
		BuildingsTypes catalog;
		catalog.initLegacy();
		for (std::size_t id = 0; id < catalog.size(); ++id)
			catalog.get(id)->type = "custom-family-" + std::to_string(catalog.get(id)->shortTypeNum);
		BuildingCapabilityIndex original(catalog);
		auto snapshot = nlohmann::json::parse(catalog.snapshotJson());
		auto& variants = snapshot["variants"];
		for (auto& variant : variants)
			variant["id"] = variants.size() - 1 - variant["id"].get<std::size_t>();
		BuildingsTypes reordered;
		reordered.loadSnapshotJson(snapshot.dump());
		BuildingCapabilityIndex changed(reordered);
		BuildingCapabilityIndex repeated(reordered);
		for (unsigned value = 0; value < static_cast<unsigned>(BuildingIntent::Count); ++value)
		{
			const auto intent = static_cast<BuildingIntent>(value);
			CHECK(providerKeys(catalog, original, intent) == providerKeys(reordered, changed, intent));
			CHECK(changed.providers(intent) == repeated.providers(intent));
			CHECK(changed.placements(intent) == repeated.placements(intent));
			CHECK(std::is_sorted(changed.providers(intent).begin(), changed.providers(intent).end()));
		}
	}
}
