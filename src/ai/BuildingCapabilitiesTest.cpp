// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "BuildingCapabilities.h"
#include "BuildingType.h"
#include "GameHeader.h"
#include "UnitCatalog.h"
#include "ai/model/BuildingProjection.h"
#include <algorithm>
#include <array>
#include <barrier>
#include <thread>
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

    TEST_CASE("direct stock supply is independently available without market experiments")
    {
        BuildingsTypes catalog;catalog.initLegacy();
        const int flag=catalog.getTypeNum("warflag",0,false);
        auto& type=*catalog.get(flag);
        type.semantics.market.suppliesDirectStock=true;
        type.semantics.market.suppliesStock=false;
        type.semantics.market.interTeamFruitExchange=false;
        BuildingCapabilityIndex index(catalog);
        GameHeader rules;
        CHECK(index.matches(flag,BuildingIntent::ExchangeResources));
        CHECK(index.available(flag,BuildingIntent::ExchangeResources,rules));
        CHECK(std::any_of(index.placements(BuildingIntent::ExchangeResources).begin(),
            index.placements(BuildingIntent::ExchangeResources).end(),[&](auto candidate){return candidate.placementType==flag;}));
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

    TEST_CASE("combat providers require learnable active abilities for custom hybrid recipients")
    {
        glob2test::HeadlessGlobals globals;
        for (int mode=0;mode<3;++mode) {
            CAPTURE(mode);
            auto behaviors=nlohmann::json{{"transport",true},{"construct",true},{"clear",true},{"melee",mode!=1}};
            if (mode==2) behaviors["learnableMask"]=0;
            const auto units=UnitCatalog::fromJson(nlohmann::json{
                {"schemaVersion",1},{"units",nlohmann::json::array({{
                    {"key","fixture:hybrid"},{"extends","warrior"},{"mesh","worker"},{"behaviors",behaviors}}})}}.dump());
            const auto hybrid=units->find("fixture:hybrid"); REQUIRE(hybrid);
            BuildingsTypes catalog; catalog.initLegacy();
            const int id=catalog.getTypeNum("inn",0,false);
            auto& type=*catalog.get(id);
            type.semantics.feeding.enabled=false;
            type.semantics.healing.enabled=false;
            type.semantics.training={};
            type.semantics.admittedUnits.specified=true;
            type.semantics.admittedUnits.keys={"fixture:hybrid"};
            for (int ability:{ATTACK_SPEED,ATTACK_STRENGTH}) {
                auto& course=type.semantics.training[ability];
                course.enabled=true; course.targetLevel=1;
                course.units.specified=true; course.units.keys={"fixture:hybrid"};
            }
            type.requiredExperiment="markets-v2";
            catalog.configureUnits(*units);
            catalog.configureExperiments({"markets-v2"});
            BuildingCapabilityIndex index(catalog);
            const bool meaningful=mode==0;
            CHECK(index.matches(id,BuildingIntent::TrainAttackSpeed,*hybrid)==meaningful);
            CHECK(index.matches(id,BuildingIntent::TrainAttackStrength,*hybrid)==meaningful);
            CHECK_FALSE(index.matches(id,BuildingIntent::TrainAttackStrength,WORKER));
            CHECK(ModelBuildingProjection::trainsWarriorCombat(type)==meaningful);
            CHECK(ModelBuildingProjection::channelCompleted(type)==(meaningful ? ModelBuildingProjection::CombatTraining : ModelBuildingProjection::PassiveGround));
            // Authoring and simulation admission rows remain independent of the
            // lossy planning masks, including inactive and nonlearnable courses.
            CHECK(catalog.getRuntime(id)->interaction(*hybrid).trainingMask==((1u<<ATTACK_SPEED)|(1u<<ATTACK_STRENGTH)));
            BuildingsTypes copied(catalog); BuildingsTypes assigned; assigned=catalog;
            for (const auto* retained:{&copied,&assigned}) {
                CHECK(retained->getRuntime(id)->unitCount==units->size());
                CHECK(retained->unitTrainingAbilities(*hybrid)==catalog.unitTrainingAbilities(*hybrid));
                CHECK(retained->get(id)->runtimeTrainingAbilities==type.runtimeTrainingAbilities);
                CHECK(BuildingCapabilityIndex(*retained).matches(id,BuildingIntent::TrainAttackSpeed,*hybrid)==meaningful);
            }
            GameHeader rules;
            CHECK_FALSE(index.available(id,BuildingIntent::TrainAttackSpeed,rules,*hybrid));
            rules.getExperiments().set(ExperimentId::MarketsV2);
            CHECK(index.available(id,BuildingIntent::TrainAttackSpeed,rules,*hybrid)==meaningful);
            rules.setUnitUpgradesDisabled(true);
            CHECK_FALSE(index.available(id,BuildingIntent::TrainAttackSpeed,rules,*hybrid));
        }
    }

    TEST_CASE("qualification-only courses retain raw learnability without construction or active ability")
    {
        glob2test::HeadlessGlobals globals;
        for (bool learnable:{false,true}) for (bool active:{false,true}) {
            CAPTURE(learnable); CAPTURE(active);
            const auto units=UnitCatalog::fromJson(nlohmann::json{
                {"schemaVersion",1},{"units",nlohmann::json::array({{
                    {"key","fixture:qualification"},{"extends","worker"},
                    {"behaviors",{{"construct",false},{"transport",false},{"clear",false},{"walk",active},
                        {"learnConstruction",true},{"learnableMask",learnable ? (1u<<WALK) : 0u}}}}})}}.dump());
            const auto recipient=units->find("fixture:qualification"); REQUIRE(recipient);
            BuildingsTypes catalog; catalog.initLegacy();
            const int id=catalog.getTypeNum("inn",0,false);
            auto& type=*catalog.get(id);
            type.semantics.feeding.enabled=false; type.semantics.healing.enabled=false;
            type.semantics.training={};
            type.semantics.admittedUnits.specified=true;
            type.semantics.admittedUnits.keys={"fixture:qualification"};
            auto& course=type.semantics.training[WALK];
            course.enabled=true; course.targetLevel=0; course.constructionLevel=2;
            course.units.specified=true; course.units.keys={"fixture:qualification"};
            catalog.configureUnits(*units);
            BuildingCapabilityIndex index(catalog);
            CHECK(catalog.unitTrainingAbilities(*recipient)==(learnable && active ? (1u<<WALK) : 0u));
            CHECK(catalog.unitConstructionTrainingAbilities(*recipient)==(learnable ? (1u<<WALK) : 0u));
            CHECK(type.runtimeTrainingAbilities==0);
            CHECK(type.runtimeConstructionTraining==learnable);
            CHECK(index.matches(id,BuildingIntent::TrainConstruction,*recipient)==learnable);
            CHECK_FALSE(index.matches(id,BuildingIntent::TrainWalk,*recipient));
            CHECK_FALSE(index.matches(id,BuildingIntent::TrainConstruction,WORKER));
            CHECK(catalog.getRuntime(id)->interaction(*recipient).trainingMask==(1u<<WALK));
            BuildingsTypes copy(catalog);
            CHECK(copy.unitConstructionTrainingAbilities(*recipient)==catalog.unitConstructionTrainingAbilities(*recipient));
            CHECK(copy.get(id)->runtimeConstructionTraining==learnable);
        }
    }

    TEST_CASE("construction and clearing jobs independently grant meaningful work training")
    {
        glob2test::HeadlessGlobals globals;
        for (int mode=0;mode<4;++mode) {
            CAPTURE(mode);
            const auto units=UnitCatalog::fromJson(nlohmann::json{
                {"schemaVersion",1},{"units",nlohmann::json::array({{
                    {"key","fixture:work"},{"extends","worker"},
                    {"behaviors",{{"transport",mode==0},{"construct",mode==1},{"clear",mode==2},
                        {"learnableMask",(1u<<BUILD)|(1u<<HARVEST)}}}}})}}.dump());
            const auto recipient=units->find("fixture:work"); REQUIRE(recipient);
            BuildingsTypes catalog; catalog.initLegacy();
            const int id=catalog.getTypeNum("school",0,false);
            auto& type=*catalog.get(id);
            type.semantics.admittedUnits.specified=true;
            type.semantics.admittedUnits.keys={"fixture:work"};
            for (int ability:{BUILD,HARVEST}) {
                auto& course=type.semantics.training[ability];
                course.enabled=true; course.targetLevel=1;
                course.units.specified=true; course.units.keys={"fixture:work"};
            }
            catalog.configureUnits(*units);
            BuildingCapabilityIndex index(catalog);
            CHECK(index.matches(id,BuildingIntent::TrainBuild,*recipient)==(mode==0 || mode==1));
            CHECK(index.matches(id,BuildingIntent::TrainHarvest,*recipient)==(mode==0 || mode==2));
            CHECK(bool(type.runtimeTrainingAbilities&(1u<<BUILD))==(mode==0 || mode==1));
            CHECK(bool(type.runtimeTrainingAbilities&(1u<<HARVEST))==(mode==0 || mode==2));
        }
    }

    TEST_CASE("legacy cached ability policies retain training outside historical job capabilities")
    {
        glob2test::HeadlessGlobals globals;
        const auto legacy=UnitCatalog::legacy();
        std::vector<std::array<UnitType,NB_UNIT_LEVELS>> tables;
        for (unsigned unit=0;unit<legacy->size();++unit) tables.push_back(legacy->levels(unit));
        // Historical explorer Race tables can learn build despite having no
        // construction/transport job. A restored cached unit keeps that policy.
        tables[EXPLORER].back().performance[BUILD]=12;
        const auto recovered=legacy->withLegacyLevels(tables,425);
        REQUIRE(recovered->runtime(EXPLORER).has(UnitRuntimeTraits::LegacyPerformancePolicies));
        REQUIRE_FALSE(recovered->runtime(EXPLORER).has(UnitRuntimeTraits::Construct));
        REQUIRE_FALSE(recovered->runtime(EXPLORER).has(UnitRuntimeTraits::Transport));
        BuildingsTypes catalog; catalog.initLegacy();
        const int id=catalog.getTypeNum("school",0,false);
        catalog.get(id)->semantics.admittedUnitMask=BUILDING_ALL_UNIT_TYPES;
        catalog.get(id)->semantics.training[BUILD].unitMask=BUILDING_ALL_UNIT_TYPES;
        catalog.configureUnits(*recovered);
        CHECK((catalog.unitTrainingAbilities(EXPLORER)&(1u<<BUILD))!=0);
        CHECK(BuildingCapabilityIndex(catalog).matches(id,BuildingIntent::TrainBuild,EXPLORER));
        CHECK_FALSE(BuildingCapabilityIndex(catalog).matches(id,BuildingIntent::TrainConstruction,EXPLORER));
        // The same table authored without the legacy cache policy is gated by
        // its active capabilities, while the service interaction stays intact.
        auto authoredSnapshot=nlohmann::json::parse(recovered->serialize());
        authoredSnapshot["legacyPerformancePolicies"]=false;
        const auto authored=UnitCatalog::deserialize(authoredSnapshot.dump());
        catalog.configureUnits(*authored);
        CHECK((catalog.unitTrainingAbilities(EXPLORER)&(1u<<BUILD))==0);
        CHECK_FALSE(BuildingCapabilityIndex(catalog).matches(id,BuildingIntent::TrainBuild,EXPLORER));
        CHECK((catalog.getRuntime(id)->interaction(EXPLORER).trainingMask&(1u<<BUILD))!=0);
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

TEST_SUITE("BuildingCapabilities")
{
TEST_CASE("concurrent first reads share one immutable catalog index")
{
    glob2test::HeadlessGlobals globals;
    for(int repetition=0;repetition<16;++repetition)
    {
        glob2test::HeadlessGame world;
        std::barrier start(8);
        std::array<const BuildingCapabilityIndex*,8> indexes{};
        std::array<std::size_t,8> providerCounts{};
        std::array<std::thread,8> workers;
        for(std::size_t thread=0;thread<workers.size();++thread)
            workers[thread]=std::thread([&,thread] {
                start.arrive_and_wait();
                const auto& index=world.game.buildingCapabilities();
                indexes[thread]=&index;
                providerCounts[thread]=index.providers(BuildingIntent::Feed).size();
            });
        for(auto& worker:workers) worker.join();
        REQUIRE(providerCounts[0]>0);
        for(std::size_t thread=1;thread<workers.size();++thread)
        {
            CHECK(indexes[thread]==indexes[0]);
            CHECK(providerCounts[thread]==providerCounts[0]);
        }
    }
}
}
