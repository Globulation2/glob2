// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "BuildingType.h"
#include "ExperimentalFeatures.h"
#include <type_traits>
#include <nlohmann/json.hpp>
#include <stdexcept>

TEST_SUITE("BuildingCatalog")
{
TEST_CASE("shipped JSON preserves all 55 frozen variants and exact canonical stock snapshot")
{
    BuildingsTypes legacy, installed;
    legacy.initLegacy(); installed.loadManifest((glob2test::sourceRoot() / "data/buildings/manifest.json").string());
    REQUIRE(legacy.size() == 55);
    CHECK(installed.snapshotJson() == legacy.snapshotJson());
    CHECK(installed.fingerprint() == legacy.fingerprint());
    for (std::size_t id=0; id<installed.size(); ++id)
    {
        const auto* terminal=installed.getLastLevel(id);
        CHECK(terminal->nextLevel==-1);
        CHECK(terminal==installed.get(installed.get(id)->terminalTypeNum));
    }
}

TEST_CASE("snapshot roundtrip owns independent descriptors and ignores runtime gates")
{
    BuildingsTypes original; original.initLegacy();
    const auto snapshot = original.snapshotJson();
    BuildingsTypes restored; restored.loadSnapshotJson(snapshot);
    CHECK(restored.snapshotJson() == snapshot);
    CHECK(restored.get(0) != original.get(0));
    auto retained = restored.retainTypes();
    const BuildingType* retainedFirst = restored.get(0);
    restored.loadSnapshotJson(snapshot);
    CHECK(retainedFirst == &retained->front());
    CHECK(retainedFirst->key == restored.get(0)->key);
    CHECK(retainedFirst != restored.get(0));
    auto copied = restored;
    copied.get(0)->hpMax++;
    CHECK(copied.get(0)->hpMax != restored.get(0)->hpMax);
    original.configureExperiments({});
    CHECK_FALSE(original.get(51)->runtimeAvailable);
    CHECK_FALSE(original.usesMarketRouting());
    original.configureExperiments({"markets-v2"});
    CHECK(original.get(51)->runtimeAvailable);
    CHECK(original.get(50)->runtimeSuppliesStock);
    CHECK_FALSE(original.get(50)->runtimeFetchesStock);
    CHECK(original.get(3)->runtimeFetchesStock);
    CHECK(original.usesMarketRouting());
    CHECK(original.snapshotJson() == snapshot);
}

TEST_CASE("invalid catalog reports the rejected field and preserves the active catalog")
{
    using Json = nlohmann::json;
    BuildingsTypes registry;
    registry.initLegacy();
    registry.configureExperiments({"markets-v2"});
    const auto before = registry.snapshotJson();
    const auto fingerprint = registry.fingerprint();
    const auto count = registry.size();
    const auto* descriptor = registry.get(3);
    const auto* runtime = registry.getRuntime(3);
    const auto fetchesStockMask = runtime->fetchesStockMask;
    const auto good = Json::parse(before);

    struct InvalidCase
    {
        const char* name;
        void (*mutate)(Json&);
        std::vector<std::string> diagnostics;
    };
    const InvalidCase cases[] = {
        {"duplicate stable key",
            [](Json& j) { j["variants"][1]["key"] = j["variants"][0]["key"]; },
            {"duplicate stable key"}},
        {"duplicate runtime ID",
            [](Json& j) { j["variants"][1]["id"] = 0; },
            {"IDs must be unique and dense"}},
        {"missing completion",
            [](Json& j) { j["variants"][0]["next"] = "missing"; },
            {"unresolved transition"}},
        {"forward cycle",
            [](Json& j) { j["variants"][1]["next"] = j["variants"][0]["key"]; },
            {"cyclic forward transitions"}},
        {"upgrade skips construction",
            [](Json& j) { j["variants"][1]["next"] = j["variants"][3]["key"]; },
            {"upgrade must target a construction variant"}},
        {"missing repair site",
            [](Json& j) { j["variants"][1]["previous"] = "missing.repair.site"; },
            {"unresolved transition"}},
        {"zero width",
            [](Json& j) { j["variants"][0]["properties"]["width"] = 0; },
            {"width", "outside 1..64"}},
        {"excess initial health",
            [](Json& j) { j["variants"][1]["properties"]["hpInit"] = 701; },
            {"initial health exceeds maximum health"}},
        {"zero maximum health",
            [](Json& j) { j["variants"][1]["properties"]["hpMax"] = 0; },
            {"initial health exceeds maximum health"}},
        {"reserved resource capacity",
            [](Json& j) { j["variants"][0]["properties"]["maxMaterial"][MaterialCount] = 1; },
            {"maxMaterial", "outside 0..0"}},
        {"unknown replenishment resource",
            [](Json& j) { j["variants"][0]["semantics"]["replenishMaterials"] = {"unobtainium"}; },
            {"replenishMaterials", "unknown material 'unobtainium'"}},
        {"duplicate supply resource",
            [](Json& j) { j["variants"][0]["semantics"]["market"]["suppliesStockMaterials"] = {"wood", "wood"}; },
            {"suppliesStockMaterials", "duplicate material 'wood'"}},
        {"fractional width",
            [](Json& j) { j["variants"][0]["properties"]["width"] = 1.5; },
            {"width", "expected an integer"}},
        {"undeclared experiment",
            [](Json& j) { j["variants"][0]["requiredExperiment"] = "unknown-feature"; },
            {"unknown-feature", "experiment metadata is missing"}},
        {"unequal late-choice recipes",
            [](Json& j) { j["variants"][1]["semantics"]["production"]["recipes"]["warrior"]["duration"] = 17; },
            {"late-choice production requires equal recipes"}},
        {"unknown service resource",
            [](Json& j) { j["variants"][3]["semantics"]["feeding"]["cost"]["unobtainium"] = 2; },
            {"variants[3]", "inn.0.finished", "unknown material 'unobtainium'"}},
        {"negative service cost",
            [](Json& j) { j["variants"][3]["semantics"]["feeding"]["cost"]["food"] = -1; },
            {"food", "outside 0..1000000"}},
        {"obsolete capability field",
            [](Json& j) { j["variants"][3]["properties"]["canFeedUnit"] = 1; },
            {"unknown field 'canFeedUnit'"}},
        {"idle movement training",
            [](Json& j) { j["variants"][3]["semantics"]["training"]["stopWalk"] = {{"enabled", true}}; },
            {"idle movement primitives cannot be trained"}},
        {"missing variant properties",
            [](Json& j) { j["variants"][3].erase("properties"); },
            {"variants[3]", "inn.0.finished", "properties"}},
    };
    const auto reject = [&](const std::string& input, const std::vector<std::string>& diagnostics) {
        bool threw = false;
        try { registry.loadSnapshotJson(input); }
        catch (const std::exception& error)
        {
            threw = true;
            const std::string message = error.what();
            CAPTURE(message);
            for (const auto& expected : diagnostics)
            {
                CAPTURE(expected);
                CHECK(message.find(expected) != std::string::npos);
            }
        }
        REQUIRE(threw);
        CHECK(registry.snapshotJson() == before);
        CHECK(registry.fingerprint() == fingerprint);
        CHECK(registry.size() == count);
        REQUIRE(registry.get(3) == descriptor);
        REQUIRE(registry.getRuntime(3) == runtime);
        CHECK(runtime->fetchesStockMask == fetchesStockMask);
        CHECK(registry.usesMarketRouting());
    };
    for (const auto& invalid : cases)
    {
        INFO(invalid.name);
        auto bad = good;
        invalid.mutate(bad);
        reject(bad.dump(), invalid.diagnostics);
    }
    reject(R"({"schemaVersion":1,"schemaVersion":1})", {"duplicate key"});
}

TEST_CASE("manifest parsing identifies the broken definition file without publishing a catalog")
{
    BuildingsTypes catalog;
    catalog.initLegacy();
    const auto before = catalog.snapshotJson();
    const auto fingerprint = catalog.fingerprint();
    const auto count = catalog.size();
    const auto* runtime = catalog.getRuntime(0);
    glob2test::TempDir files("building-catalog-errors");
    const auto manifest = files.path / "manifest.json";
    const auto definition = files.path / "broken-building.json";
    glob2test::writeFile(manifest,
        R"({"schemaVersion":1,"catalogKey":"broken-example","files":["broken-building.json"]})");
    auto invalidService = nlohmann::json::parse(glob2test::readFile(
        glob2test::fixture("building-catalog/authoring/field-kitchen.json")));
    invalidService["variants"][0]["semantics"]["feeding"]["cost"]["unobtainium"] = 1;
    for (const bool malformedJson : {true, false})
    {
        CAPTURE(malformedJson);
        glob2test::writeFile(definition,
            malformedJson ? R"({"variants":[})" : invalidService.dump());
        bool threw = false;
        try { catalog.loadManifest(manifest.string()); }
        catch (const std::exception& error)
        {
            threw = true;
            const std::string message = error.what();
            CAPTURE(message);
            CHECK(message.find(definition.string()) != std::string::npos);
            if (malformedJson) CHECK(message.find("parse") != std::string::npos);
            else
            {
                CHECK(message.find("field-kitchen.finished") != std::string::npos);
                CHECK(message.find("unknown material 'unobtainium'") != std::string::npos);
            }
        }
        REQUIRE(threw);
        CHECK(catalog.snapshotJson() == before);
        CHECK(catalog.fingerprint() == fingerprint);
        CHECK(catalog.size() == count);
        CHECK(catalog.getRuntime(0) == runtime);
    }
}

TEST_CASE("documented experimental field kitchen loads from its authored files")
{
    BuildingsTypes catalog;
    catalog.loadManifest(glob2test::fixture("building-catalog/authoring/manifest.json").string());
    REQUIRE(catalog.size() == 1);
    const auto* kitchen = catalog.get(0);
    CHECK(kitchen->key == "field-kitchen.finished");
    CHECK(kitchen->type.empty());
    CHECK(kitchen->shortTypeNum == -1);
    CHECK(kitchen->semantics.placeable);
    CHECK(kitchen->semantics.instantPlacement);
    CHECK(kitchen->maxUnitInside == 3);
    CHECK(kitchen->semantics.admittedUnitMask == BUILDING_ALL_UNIT_TYPES);
    CHECK(kitchen->semantics.feeding.enabled);
    CHECK(kitchen->semantics.healing.enabled);
    CHECK(kitchen->semantics.feeding.cost[WHEAT] == 1);
    CHECK(kitchen->semantics.healing.cost == BuildingMaterialCost{});
    CHECK(kitchen->maxMaterial[WHEAT] == 12);
    CHECK(kitchen->semantics.replenishMaterialMask == (1u << WHEAT));
    CHECK(kitchen->semantics.assignmentLimit == 2);
    catalog.configureExperiments({});
    CHECK_FALSE(catalog.getRuntime(0)->has(BuildingRuntimeTraits::Available));
    catalog.configureExperiments({"field-kitchens"});
    CHECK(catalog.getRuntime(0)->has(BuildingRuntimeTraits::Available));
    CHECK(catalog.getRuntime(0)->has(BuildingRuntimeTraits::Feeds));
    CHECK(catalog.getRuntime(0)->has(BuildingRuntimeTraits::Heals));
    const auto snapshot = catalog.snapshotJson();
    CHECK(snapshot.find("field-kitchen.json") == std::string::npos);
    CHECK(snapshot.find(glob2test::fixture("building-catalog/authoring").string()) == std::string::npos);
    BuildingsTypes restored;
    restored.loadSnapshotJson(snapshot);
    CHECK(restored.snapshotJson() == snapshot);
    CHECK(restored.fingerprint() == catalog.fingerprint());
}

TEST_CASE("renamed composite variant and added feature survive embedded snapshot")
{
    BuildingsTypes registry; registry.initLegacy();
    auto snapshot = nlohmann::json::parse(registry.snapshotJson());
    auto& variants = snapshot["variants"];
    auto composite = variants[3];
    composite["id"] = variants.size(); composite["key"] = "custom.refuge";
    composite["previous"] = ""; composite["next"] = "";
    composite["properties"]["type"] = "refuge";
    composite["semantics"]["repairable"] = false;
    composite["semantics"]["placeable"] = true;
    composite["semantics"]["instantPlacement"] = true;
    composite["semantics"]["healing"] = variants[9]["semantics"]["healing"];
    composite["requiredExperiment"] = "custom-refuge";
    snapshot["experiments"].push_back({{"key", "custom-refuge"}, {"label", "Refuge"}, {"help", "Test fixture"}});
    variants.push_back(composite);
    registry.loadSnapshotJson(snapshot.dump());
    const auto id = registry.findByKey("custom.refuge");
    REQUIRE(id >= 0);
    CHECK(registry.get(id)->semantics.feeding.enabled);
    CHECK(registry.get(id)->semantics.healing.enabled);
    CHECK_FALSE(registry.isAvailable(id, {}));
    CHECK(registry.isAvailable(id, {"custom-refuge"}));
    BuildingsTypes restored; restored.loadSnapshotJson(registry.snapshotJson());
    CHECK(restored.fingerprint() == registry.fingerprint());
}
TEST_CASE("experiment metadata obeys the same contract before startup and transport")
{
    BuildingsTypes registry; registry.initLegacy();
    const auto original=registry.snapshotJson();
    const auto rejected=[&](nlohmann::json definition) {
        auto catalog=nlohmann::json::parse(original);
        catalog["experiments"].push_back(definition);
        CHECK_THROWS(registry.loadSnapshotJson(catalog.dump()));
        CHECK(registry.snapshotJson()==original);
    };
    for(const char* key : {"new_building","new.building","-building","building-","new--building"})
        rejected({{"key",key},{"label","Label"},{"help","Help"}});
    rejected({{"key","new-building"},{"label",""},{"help","Help"}});
    rejected({{"key","new-building"},{"label","Label"},{"help",""}});
    auto catalog=nlohmann::json::parse(original);
    // Catalog experiments share the 64-entry budget with the built-in ones.
    const int room=int(ExperimentSet::MAX_STORED-ExperimentSet::COUNT);
    for(int i=0;i<room;++i) catalog["experiments"].push_back({{"key","feature-"+std::to_string(i)}, {"label","Label"},{"help","Help"}});
    CHECK_NOTHROW(registry.loadSnapshotJson(catalog.dump()));
    catalog["experiments"].push_back({{"key","feature-"+std::to_string(room)},{"label","Label"},{"help","Help"}});
    CHECK_THROWS(registry.loadSnapshotJson(catalog.dump()));
}
TEST_CASE("expanded canonical snapshots must fit transport before catalog publication")
{
    BuildingsTypes catalog; catalog.initLegacy();
    const auto original=catalog.snapshotJson();
    nlohmann::json sparse={{"schemaVersion",1},{"catalogKey","sparse"},{"variants",nlohmann::json::array()}};
    for(int id=0;id<4096;++id)
        sparse["variants"].push_back({{"id",id},{"key","variant-"+std::to_string(id)},
            {"properties",{{"width",1},{"height",1}}},{"semantics",nlohmann::json::object()}});
    CHECK(sparse.dump().size()<1024*1024);
    CHECK_THROWS_WITH_AS(catalog.loadSnapshotJson(sparse.dump()),
        "Building catalog JSON: resolved catalog exceeds the 8 MiB snapshot limit",std::runtime_error);
    CHECK(catalog.snapshotJson()==original);
}
TEST_CASE("assignment limits compile staffing capability independently for each stage")
{
    BuildingsTypes registry; registry.initLegacy();
    const int site=registry.getTypeNum("hospital",0,true);
    const int completed=registry.getTypeNum("hospital",0,false);
    REQUIRE(registry.get(site)->semantics.assignmentLimit==20);
    REQUIRE(registry.get(completed)->semantics.assignmentLimit==0);
    CHECK(registry.get(completed)->maxUnitWorking==0);
    auto snapshot=nlohmann::json::parse(registry.snapshotJson());
    snapshot["variants"][completed]["semantics"]["assignmentLimit"]=40;
    registry.loadSnapshotJson(snapshot.dump());
    CHECK(registry.get(site)->semantics.assignmentLimit==20);
    CHECK(registry.get(completed)->semantics.assignmentLimit==40);
    CHECK(registry.get(completed)->maxUnitWorking==1);
}

TEST_CASE("stable keys need no compatibility family alias and defaults are bounded preferences")
{
    BuildingsTypes registry; registry.initLegacy();
    auto snapshot=nlohmann::json::parse(registry.snapshotJson());
    for (const char* key : {"custom.alpha","custom.beta"})
    {
        auto variant=snapshot["variants"][44];
        variant["id"]=snapshot["variants"].size(); variant["key"]=key;
        variant["properties"].erase("type"); variant["properties"].erase("shortTypeNum");
        variant["presentation"].erase("displayName"); variant["presentation"].erase("defaultAssigned");
        snapshot["variants"].push_back(variant);
    }
    registry.loadSnapshotJson(snapshot.dump());
    for (const char* key : {"custom.alpha","custom.beta"})
    {
        const auto* b=registry.get(registry.findByKey(key));
        CHECK(b->type.empty()); CHECK(b->shortTypeNum==-1);
        CHECK(b->presentation.displayName==key); CHECK(b->presentation.defaultAssigned==2);
    }
    CHECK(registry.getTypeNum("",0,false)==-1);
    snapshot=nlohmann::json::parse(registry.snapshotJson());
    snapshot["variants"][0]["presentation"]["defaultAssigned"]=21;
    CHECK_THROWS_AS(registry.loadSnapshotJson(snapshot.dump()),std::exception);
}

TEST_CASE("compiled runtime rows are dense aligned value copies and refresh gates in place")
{
    BuildingsTypes catalog; catalog.initLegacy();
    CHECK(sizeof(BuildingRuntimeTraits)==64);
    CHECK(std::is_trivially_copyable_v<BuildingRuntimeTraits>);
    CHECK(reinterpret_cast<std::uintptr_t>(catalog.getRuntime(0))%64==0);
    CHECK(catalog.getRuntime(1)==catalog.getRuntime(0)+1);
    const auto* retained=catalog.getRuntime(3);
    catalog.configureExperiments({"markets-v2"});
    CHECK(catalog.getRuntime(3)==retained);
    CHECK(retained->fetchesStockMask==catalog.get(3)->runtimeFetchesStockMask);
    auto copy=catalog;
    CHECK(copy.stockSupplyMask()==catalog.stockSupplyMask());
    CHECK(copy.directSupplyMask()==catalog.directSupplyMask());
    CHECK(copy.getRuntime(3)!=retained);
    copy.configureExperiments({});
    CHECK(copy.getRuntime(3)->fetchesStockMask==0);
    CHECK(copy.stockSupplyMask()==0);
    CHECK((copy.directSupplyMask()&(1u<<WHEAT))==0);
    CHECK((copy.directSupplyMask()&(1u<<CHERRY))!=0);
    CHECK(retained->fetchesStockMask!=0);
    for (std::size_t id=0; id<catalog.size(); ++id) {
        const auto& authored=*catalog.get(id); const auto& hot=*catalog.getRuntime(id);
        CHECK(hot.hpMax==authored.hpMax); CHECK(hot.armor==authored.armor);
        CHECK(hot.width==authored.width); CHECK(hot.height==authored.height);
        CHECK(hot.assignmentLimit==authored.semantics.assignmentLimit);
        CHECK(hot.productionEnabledMask==authored.semantics.production.enabledUnitMask);
        CHECK(hot.projectileDamage==authored.semantics.projectileDamage);
    }
}

TEST_CASE("compact runtime traits preserve validated numeric boundaries")
{
    BuildingsTypes catalog; catalog.initLegacy();
    auto json=nlohmann::json::parse(catalog.snapshotJson());
    auto& variant=json["variants"][3];
    auto& properties=variant["properties"];
    properties["width"]=64; properties["height"]=64; properties["decLeft"]=-64; properties["decTop"]=64;
    properties["shootingRange"]=1024; properties["shootRhythm"]=65535; properties["shootSpeed"]=65535;
    properties["maxBullets"]=1; properties["multiplierStoneToBullets"]=1;
    variant["semantics"]["assignmentLimit"]=1024;
    variant["semantics"]["requiredWorkerLevel"]=3;
    variant["semantics"]["projectileDamage"]={1000000,1000000,1000000};
    catalog.loadSnapshotJson(json.dump());
    const auto& hot=*catalog.getRuntime(3);
    CHECK(hot.shootingRange==1024); CHECK(hot.shootRhythm==65535); CHECK(hot.shootSpeed==65535);
    CHECK(hot.width==64); CHECK(hot.height==64); CHECK(hot.decLeft==-64); CHECK(hot.decTop==64);
    CHECK(hot.assignmentLimit==1024); CHECK(hot.requiredWorkerLevel==3);
    CHECK(hot.projectileDamage[WARRIOR]==1000000);
    properties["shootingRange"]=0; properties["shootRhythm"]=1000000;
    catalog.loadSnapshotJson(json.dump()); CHECK(catalog.getRuntime(3)->shootRhythm==0);
}

TEST_CASE("new materials retain high bits through catalogs and compact runtime rows")
{
    BuildingsTypes catalog; catalog.initLegacy();
    auto json=nlohmann::json::parse(catalog.snapshotJson());
    auto& variant=json["variants"][3];
    auto& sem=variant["semantics"];
    variant["properties"]["maxMaterial"][materialIndex(MaterialId::Fabric)]=40;
    sem["replenishMaterials"]={"fabric", "gold", "metal", "glass"};
    sem["feeding"]["cost"]={{"fabric",2}};
    sem["market"]["fetchesStock"]=true;
    sem["market"]["fetchesStockExperiment"]="";
    sem["market"]["fetchesStockMaterials"]={"fabric", "gold"};
    catalog.loadSnapshotJson(json.dump());
    catalog.configureExperiments({});
    CHECK(catalog.getRuntime(3)->replenishMaterialMask==0xF00);
    CHECK(catalog.getRuntime(3)->fetchesStockMask==0x900);
    CHECK(catalog.get(3)->semantics.feeding.cost[materialIndex(MaterialId::Fabric)]==2);
    CHECK(catalog.get(3)->semantics.feeding.costMask==materialBit(MaterialId::Fabric));
    BuildingsTypes restored; restored.loadSnapshotJson(catalog.snapshotJson());
    CHECK(restored.snapshotJson()==catalog.snapshotJson());
}

TEST_CASE("legacy inventory vocabulary imports and ambiguous material aliases fail atomically")
{
    BuildingsTypes catalog; catalog.initLegacy();
    auto json=nlohmann::json::parse(catalog.snapshotJson());
    auto& variant=json["variants"][3];
    auto& p=variant["properties"];
    p["maxResource"]=p["maxMaterial"]; p.erase("maxMaterial");
    auto& sem=variant["semantics"];
    sem["replenishResources"]=sem["replenishMaterials"]; sem.erase("replenishMaterials");
    sem["feeding"]["cost"]={{"wheat",1}};
    catalog.loadSnapshotJson(json.dump());
    CHECK(catalog.get(3)->semantics.feeding.cost[materialIndex(MaterialId::Food)]==1);
    const auto before=catalog.snapshotJson();
    sem["feeding"]["cost"]["food"]=2;
    CHECK_THROWS_AS(catalog.loadSnapshotJson(json.dump()),std::exception);
    CHECK(catalog.snapshotJson()==before);
}

}
