// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "BuildingType.h"
#include "BuildingLibrary.h"
#include "BuildingLibraryScreen.h"
#include "OnlineServices.h"
#include "OnlineFakes.h"
#include "EngineFixtures.h"
#include "OnlineStorage.h"
#include "SimVersion.h"
#include "GameHeader.h"
#include "Version.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <GraphicContext.h>
#include "ExperimentalFeatures.h"
#include <type_traits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <map>
#include <array>

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

namespace {
nlohmann::json packageFixture(const std::string& name)
{
    const std::string key="b-"+name+"-kitchen";
    return {{"schemaVersion",1},{"namespace",name},{"experiments",nlohmann::json::array()},
        {"sprites",nlohmann::json::array()}, {"variants",nlohmann::json::array({{
            {"key",key}, {"properties",{{"width",2},{"height",2},{"hpInit",200},{"hpMax",200},
                {"gameSprite","data/gfx/inn0b"},{"miniSprite","data/gfx/miniinn0b"}}},
            {"semantics",{{"placeable",true},{"instantPlacement",true}}}
        }})}};
}
}
TEST_SUITE("BuildingPackages")
{
TEST_CASE("composition sorts packages and preserves the stock IDs and starting building")
{
    const auto first=packageFixture("11111111-1111-4111-8111-111111111111").dump();
    const auto second=packageFixture("22222222-2222-4222-8222-222222222222").dump();
    BuildingsTypes a,b,stock; a.initLegacy(); b.initLegacy(); stock.initLegacy();
    a.composePackages({second,first}); b.composePackages({first,second});
    CHECK(a.snapshotJson()==b.snapshotJson());
    CHECK(a.fingerprint()==b.fingerprint());
    const auto composed=a.snapshotJson();
    CHECK_THROWS(a.composePackages({first}));
    CHECK(a.snapshotJson()==composed);
    CHECK(a.getStartingBuildingTypeNum()==stock.getStartingBuildingTypeNum());
    REQUIRE(a.size()==stock.size()+2);
    for(std::size_t i=0;i<stock.size();++i) CHECK(a.get(i)->key==stock.get(i)->key);
    auto unchanged=stock; unchanged.composePackages({});
    CHECK(unchanged.fingerprint()==stock.fingerprint());
}
TEST_CASE("rejected package composition preserves descriptors and canonical stock bytes")
{
    using Json=nlohmann::json;
    BuildingsTypes catalog; catalog.initLegacy();
    const auto before=catalog.snapshotJson();
    const auto good=packageFixture("11111111-1111-4111-8111-111111111111");
    auto rejected=[&](Json package) {
        CHECK_THROWS(catalog.composePackages({package.dump()}));
        CHECK(catalog.snapshotJson()==before);
    };
    auto bad=good; bad["variants"][0]["key"]="stock-replacement"; rejected(bad);
    bad=good; bad["variants"][0]["previous"]="inn.0.site"; rejected(bad);
    bad=good; bad["variants"][0]["requiredExperiment"]="markets-v2"; rejected(bad);
    bad=good; bad["variants"][0]["properties"]["width"]=0; rejected(bad);
    bad=good; bad["variants"][0]["properties"]["gameSprite"]="/tmp/sprite"; rejected(bad);
    bad=good; bad["variants"][0]["properties"].erase("gameSprite"); rejected(bad);
    bad=good; bad["variants"][0]["properties"].erase("miniSprite"); rejected(bad);
    bad["variants"][0]["properties"]["miniSpriteImage"]=-1;
    BuildingsTypes withoutMini; withoutMini.initLegacy();
    CHECK_NOTHROW(withoutMini.composePackages({bad.dump()}));
    bad=good; bad["variants"][0]["id"]=0; rejected(bad);
    bad=good; bad["variants"][0]["semantics"]["market"]["suppliesStockExperiment"]="b-22222222-2222-4222-8222-222222222222-enable"; rejected(bad);
    CHECK_THROWS(catalog.composePackages({good.dump(),good.dump()}));
    CHECK(catalog.snapshotJson()==before);
}
TEST_CASE("artwork hashes bind catalog identity and custom frame indices are checked")
{
    using Json=nlohmann::json;
    BuildingsTypes a,b; a.initLegacy(); b.initLegacy();
    auto package=packageFixture("11111111-1111-4111-8111-111111111111");
    package["sprites"].push_back({{"key","kitchen"},{"frames",Json::array({{
        {"imageHash",std::string(64,'a')},{"width",64},{"height",64}
    }})}});
    package["variants"][0]["properties"]["gameSprite"]="package:kitchen";
    a.composePackages({package.dump()});
    CHECK(a.get(55)->gameSprite.starts_with("community/buildings/"));
    package["sprites"][0]["frames"][0]["imageHash"]=std::string(64,'b');
    b.composePackages({package.dump()});
    CHECK(a.fingerprint()!=b.fingerprint());
    package["variants"][0]["properties"]["gameSpriteCount"]=2;
    CHECK_THROWS(b.composePackages({package.dump()}));
}
}

#include "BuildingArtwork.h"
#include "Sha256.h"
namespace {
std::string artworkFixture(const nlohmann::json& sprites,const std::map<std::string,std::string>& images)
{
    std::string result="G2BA0001";
    const auto write=[&](std::uint32_t value) { for(unsigned i=0;i<4;++i) result+=char((value>>(i*8))&255); };
    const auto text=sprites.dump();write(text.size());result+=text;write(images.size());
    for(const auto& [hash,bytes]:images) { result+=hash;write(bytes.size());result+=bytes; }
    return result;
}
}
TEST_SUITE("BuildingArtwork") {
TEST_CASE("portable bundle verifies image bytes and catalog frame references") {
    const unsigned char webp[]={82,73,70,70,58,0,0,0,87,69,66,80,86,80,56,76,45,0,0,0,47,1,64,0,16,31,32,32,33,238,240,127,159,220,16,18,144,41,81,245,144,144,128,88,66,247,127,138,67,2,1,66,58,229,98,156,66,169,23,23,104,136,232,127,4,0};
    const std::string image(reinterpret_cast<const char*>(webp),sizeof(webp));
    const auto hash=Online::Sha256::hex(image);
    auto package=packageFixture("11111111-1111-4111-8111-111111111111");
    package["sprites"].push_back({{"key","kitchen"},{"frames",nlohmann::json::array({{
        {"imageHash",hash},{"width",2},{"height",2}
    }})}});
    package["variants"][0]["properties"]["gameSprite"]="package:kitchen";
    BuildingsTypes catalog; catalog.initLegacy();catalog.composePackages({package.dump()});
	CHECK_THROWS(BuildingArtwork::decode({}, catalog));
	const auto bytes = artworkFixture(package["sprites"], {{hash, image}});
	const auto artwork = BuildingArtwork::decode(bytes, catalog);
	REQUIRE(artwork);
	CHECK(artwork->bytes() == bytes);
	CHECK(artwork->files().size() == 1);
	GameHeader header;
	header.setBuildingCatalogSnapshot(catalog.snapshotJson());
	header.setBuildingArtwork(bytes);
	for (bool text : {false, true})
		for (bool withoutPlayerInfo : {false, true})
		{
			GAGCore::MemoryStreamBackend backend;
			{
				auto *owned = new GAGCore::MemoryStreamBackend;
				std::unique_ptr<GAGCore::OutputStream> stream;
				if (text)
					stream = std::make_unique<GAGCore::TextOutputStream>(owned);
				else
					stream = std::make_unique<GAGCore::BinaryOutputStream>(owned);
				if (withoutPlayerInfo)
					header.saveWithoutPlayerInfo(stream.get());
				else
					header.save(stream.get());
				stream->flush();
				backend = *owned;
			}
			backend.seekFromStart(0);
			GameHeader restored;
			std::unique_ptr<GAGCore::InputStream> stream;
			if (text)
				stream = std::make_unique<GAGCore::TextInputStream>(
					new GAGCore::MemoryStreamBackend(backend));
			else
				stream = std::make_unique<GAGCore::BinaryInputStream>(
					new GAGCore::MemoryStreamBackend(backend));
			REQUIRE((withoutPlayerInfo ? restored.loadWithoutPlayerInfo(stream.get(), VERSION_MINOR)
									   : restored.load(stream.get(), VERSION_MINOR)));
			REQUIRE(restored.getBuildingArtwork());
			CHECK(restored.getBuildingArtwork()->bytes() == bytes);
			CHECK(restored.getBuildingCatalogSnapshot() == catalog.snapshotJson());
		}
	GameHeader retained = header;
	CHECK(retained.getBuildingArtwork() == header.getBuildingArtwork());
	retained.reset();
	CHECK_FALSE(retained.getBuildingArtwork());
	CHECK_THROWS(header.setBuildingArtwork({}));
	{
		glob2test::HeadlessGlobals globals;
		Online::MemoryStorage storage;
		BuildingLibrary library(storage);
		const auto releaseHash = Online::Sha256::hex("artwork-release");
		nlohmann::json manifest = {{"schemaVersion", 1},
								   {"name", "Painted kitchen"},
								   {"namespace", package.at("namespace")},
								   {"archiveHash", releaseHash},
								   {"packageJson", package.dump()},
								   {"packageHash", Online::Sha256::hex(package.dump())},
								   {"catalogHash", catalog.fingerprint()},
								   {"baseHash", globalContainer->buildingsTypes.fingerprint()},
								   {"simVersion", Online::SimVersion::local().key()},
								   {"artworkHash", Online::Sha256::hex(bytes)}};
		library.install(manifest, bytes);
		library.select(package.at("namespace").get<std::string>(), true);
		auto selected = library.compose(globalContainer->buildingsTypes);
		REQUIRE(selected.artwork);
		CHECK(selected.artwork->files().size() == 1);
		CHECK(selected.artwork->bytes() == bytes);
		glob2test::HeadlessGame::Options options;
		options.header = true;
		glob2test::HeadlessGame world(options);
		world.game.buildingsTypes = selected.catalog;
		world.game.gameHeader.setBuildingCatalogSnapshot(selected.catalog.snapshotJson());
		world.game.gameHeader.setBuildingArtwork(selected.artwork->bytes());
		world.game.configureBuildingCatalog();
		const auto id = world.game.buildingsTypes.findByKey(
			package.at("variants")[0].at("key").get<std::string>());
		auto *building = world.game.addBuilding(5, 5, id, 0);
		REQUIRE(building);
		world.game.map.setBuilding(5, 5, building->type->width, building->type->height,
								   building->gid);
		for (bool text : {false, true})
		{
			auto *saved = new GAGCore::MemoryStreamBackend;
			std::unique_ptr<GAGCore::OutputStream> output;
			if (text)
				output = std::make_unique<GAGCore::TextOutputStream>(saved);
			else
				output = std::make_unique<GAGCore::BinaryOutputStream>(saved);
			world.game.save(output.get(), false, "Portable kitchen");
			output->flush();
			saved->seekFromStart(0);
			std::unique_ptr<GAGCore::InputStream> input;
			if (text)
				input = std::make_unique<GAGCore::TextInputStream>(
					new GAGCore::MemoryStreamBackend(*saved));
			else
				input = std::make_unique<GAGCore::BinaryInputStream>(
					new GAGCore::MemoryStreamBackend(*saved));
			Game loaded(nullptr);
			REQUIRE(loaded.load(input.get()));
			REQUIRE(loaded.gameHeader.getBuildingArtwork());
			CHECK(loaded.gameHeader.getBuildingArtwork()->bytes() == bytes);
			CHECK(loaded.buildingsTypes.fingerprint() == catalog.fingerprint());
			CHECK(loaded.checkSum() == world.game.checkSum());
		}
		storage.files["online/buildings/" + releaseHash + ".g2ba"] = "corrupt";
		CHECK_THROWS(library.compose(globalContainer->buildingsTypes));
	}
	auto bad = bytes;
	bad.back() ^= 1;
	CHECK_THROWS(BuildingArtwork::decode(bad, catalog));
	CHECK_THROWS(BuildingArtwork::decode(bytes + "extra", catalog));
	auto brokenImage = image;
	brokenImage[25] ^= char(255);
	const auto brokenHash = Online::Sha256::hex(brokenImage);
	auto brokenPackage = package;
	brokenPackage["sprites"][0]["frames"][0]["imageHash"] = brokenHash;
	BuildingsTypes brokenCatalog;
	brokenCatalog.initLegacy();
	brokenCatalog.composePackages({brokenPackage.dump()});
	CHECK_THROWS_WITH(
		BuildingArtwork::decode(
			artworkFixture(brokenPackage["sprites"], {{brokenHash, brokenImage}}), brokenCatalog),
		"Building artwork: damaged WebP pixels");

	auto wrong = package["sprites"];
	wrong[0]["frames"][0]["width"] = 3;
	CHECK_THROWS(BuildingArtwork::decode(artworkFixture(wrong, {{hash, image}}), catalog));
	CHECK_THROWS(BuildingArtwork::decode(artworkFixture(nlohmann::json::array(), {}), catalog));
	BuildingsTypes stock;
	stock.initLegacy();
	CHECK_FALSE(BuildingArtwork::decode({}, stock));
	// Shared hashes do not share renderer allocations: every sprite frame and
	// team layer must count against the decoded budget before assets are read.
	for (bool teamLayer : {false, true})
	{
		nlohmann::json frames = nlohmann::json::array();
		for (int frame = 0; frame < (teamLayer ? 33 : 65); ++frame)
		{
			nlohmann::json descriptor = {{"imageHash", hash}, {"width", 512}, {"height", 512}};
			if (teamLayer)
				descriptor["teamColorHash"] = hash;
			frames.push_back(descriptor);
		}
		CHECK_THROWS_WITH(
			BuildingArtwork::decode(
				artworkFixture(nlohmann::json::array({{{"key", "shared"}, {"frames", frames}}}),
							   {{hash, image}}),
				stock),
			"Building artwork: decoded artwork exceeds 64 MiB");
	}
	const std::string nested = std::string(1000, '[') + "0" + std::string(1000, ']');
	std::string deep = "G2BA0001";
	for (unsigned i = 0; i < 4; ++i)
		deep += char((nested.size() >> (i * 8)) & 255);
	deep+=nested;deep+=std::string(4,0);
    CHECK_THROWS_WITH(BuildingArtwork::decode(deep,stock),"Building artwork: manifest nesting exceeds 64 levels");
}
}

TEST_CASE("portable custom artwork loads and draws its exact native frame [display] [artifacts]" * doctest::test_suite("BuildingArtwork"))
{
    glob2test::HeadlessGlobals globals({.display=true,.width=128,.height=128,.screenFlags=0});
	// The pinned ImageAssets lossless fixture has opaque, transparent and half-alpha pixels.
	const unsigned char webp[] = {
		82, 73, 70,  70, 58,  0,   0,   0,   87,  69,  66,  80,  86,  80,  56,  76,  45,
		0,  0,  0,   47, 1,   64,  0,   16,  31,  32,  32,  33,  238, 240, 127, 159, 220,
		16, 18, 144, 41, 81,  245, 144, 144, 128, 88,  66,  247, 127, 138, 67,  2,   1,
		66, 58, 229, 98, 156, 66,  169, 23,  23,  104, 136, 232, 127, 4,   0};
	const std::string image(reinterpret_cast<const char *>(webp), sizeof(webp));
	const auto hash = Online::Sha256::hex(image);
	auto package = packageFixture("11111111-1111-4111-8111-111111111111");
	package["sprites"].push_back(
		{{"key", "native-render"},
		 {"frames", nlohmann::json::array({{{"imageHash", hash}, {"width", 2}, {"height", 2}}})}});
	package["variants"][0]["properties"]["gameSprite"] = "package:native-render";
	package["variants"][0]["properties"]["miniSprite"] = "package:native-render";
	auto catalog = globals->buildingsTypes;
	catalog.composePackages({package.dump()});
	const auto artwork = artworkFixture(package["sprites"], {{hash, image}});
	glob2test::HeadlessGame world({.header = true});
	world.game.buildingsTypes = catalog;
	world.game.gameHeader.setBuildingCatalogSnapshot(catalog.snapshotJson());
	world.game.gameHeader.setBuildingArtwork(artwork);
	world.game.configureBuildingCatalog();
	const auto id = catalog.findByKey(package.at("variants")[0].at("key").get<std::string>());
	auto *building = world.game.addBuilding(5, 5, id, 0);
	REQUIRE(building);
	world.game.map.setBuilding(5, 5, building->type->width, building->type->height, building->gid);
	auto *saved = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream output(saved);
	world.game.save(&output, false, "Painted kitchen");
	output.flush();
	saved->seekFromStart(0);
	GameGUI restored(false);
	GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(*saved));
	REQUIRE(restored.game.load(&input));
	CHECK(restored.game.checkSum() == world.game.checkSum());
	REQUIRE(restored.game.gameHeader.getBuildingArtwork());
	CHECK(restored.game.gameHeader.getBuildingArtwork()->bytes() == artwork);
	const auto *type = restored.game.buildingsTypes.get(id);
	REQUIRE(type->gameSpritePtr);
	REQUIRE(type->miniSpritePtr);
	CHECK(type->gameSpritePtr->getFrameCount() == 1);
	CHECK(type->gameSpritePtr->getW(0) == 2);
	CHECK(type->gameSpritePtr->getH(0) == 2);
	auto *gfx = globals->gfx;
	gfx->beginFrame(GAGCore::GraphicContext::FrameMode::FullRedraw);
	gfx->setClipRect();
	gfx->drawFilledRect(0, 0, 128, 128, GAGCore::Color(10, 20, 30));
	gfx->drawSprite(10, 10, type->gameSpritePtr, type->gameSpriteImage);
	// Enlarged copy makes the same mounted frame easy to inspect in the evidence.
	gfx->drawSprite(32, 32, 64, 64, type->miniSpritePtr, type->miniSpriteImage);
	gfx->nextFrame();
	auto *frame = gfx->completedFrame();
	REQUIRE(frame);
	const auto pixel = [&](int x, int y)
	{
		Uint8 r, g, b, a;
		// Native desktop frames use backing pixels; draw positions use logical units.
		const int px = x * frame->w / gfx->getW(), py = y * frame->h / gfx->getH();
		REQUIRE(SDL_ReadSurfacePixel(frame, px, py, &r, &g, &b, &a));
		return std::array<Uint8, 3>{r, g, b};
	};
	CHECK(pixel(10, 10) == std::array<Uint8, 3>{17, 39, 71});
	CHECK(pixel(11, 10) == std::array<Uint8, 3>{10, 20, 30});
	CHECK(pixel(11, 11) == std::array<Uint8, 3>{0, 255, 30});
	REQUIRE(SDL_SaveBMP(
		frame,
		(glob2test::artifactDir() / "building-artwork-native-software.bmp").string().c_str()));
}

struct BuildingLibraryScreenHarness
{
	static void checkDetail()
	{
		const std::string origin = "https://play.example.org",
						  id = "11111111-1111-4111-8111-111111111111";
		OnlineFakes::World world;
		Online::ServicesOwner owner;
		auto &client = owner.get().client;
		client.replaceEnvironment(world.environment());
		client.start(origin);
		auto guest = world.http.pending("/api/v1/auth/guest");
		REQUIRE(guest);
		guest->reply(200, nlohmann::json{{"account", OnlineFakes::account()},
										 {"tokens", OnlineFakes::tokens("r1", 1790000000, 600)},
										 {"deviceCredential", std::string(43, 'c')}});
		client.update();
		BuildingLibraryScreen screen;
		screen.familyInput = origin + "/buildings/" + id;
		screen.openFamily();
		auto detail = world.http.pending("/api/v1/buildings/" + id);
		REQUIRE(detail);
		CHECK(detail->request.url == origin + "/api/v1/buildings/" + id);
		detail->reply(200, nlohmann::json{
							   {"id", id}, {"name", "Shared kitchen"}, {"visibility", "unlisted"}});
		client.update();
		CHECK_FALSE(screen.busy);
		CHECK(screen.openedFamily.at("id") == id);
		const auto before = world.http.exchanges.size();
		screen.familyInput = "https://another.example/buildings/" + id;
		screen.openFamily();
		CHECK(world.http.exchanges.size() == before);
		CHECK(screen.status.find("another instance") != std::string::npos);
		screen.familyInput = id;
		screen.openFamily();
		detail = world.http.pending("/api/v1/buildings/" + id);
		REQUIRE(detail);
		detail->reply(200, nlohmann::json{{"id", "22222222-2222-4222-8222-222222222222"}});
		client.update();
		CHECK(screen.openedFamily.is_null());
		CHECK(screen.status == "The server returned a different family.");
		screen.openFamily();
		detail = world.http.pending("/api/v1/buildings/" + id);
		REQUIRE(detail);
		detail->reply(404,
					  nlohmann::json{{"code", "not_found"}, {"message", "Not found"}});
		client.update();
		CHECK(screen.openedFamily.is_null());
		CHECK(screen.status.find("signing in through Online") != std::string::npos);
	}
};
TEST_SUITE("BuildingFamilyLinks")
{
	TEST_CASE("family links stay on the selected instance and normalize bounded IDs")
	{
		const std::string id = "abcdefab-cdef-4abc-8def-abcdefabcdef",
						  origin = "https://play.example.org";
		CHECK(buildingFamilyIdFromLink("  " + id + "\n", origin) == id);
		CHECK(buildingFamilyIdFromLink("ABCDEFAB-CDEF-4ABC-8DEF-ABCDEFABCDEF", origin) == id);
		CHECK(buildingFamilyIdFromLink(origin + "/buildings/" + id + "/?from=share#details",
									   origin) == id);
		for (const auto &link :
			 {"https://other.example/buildings/" + id, origin + ".evil/buildings/" + id,
			  "https://play.example.org@evil.example/buildings/" + id,
			  origin + "/api/v1/buildings/" + id, origin + "/buildings/../" + id, id + "/extra",
			  std::string(36, '-'), std::string(1025, 'x')})
			CHECK_THROWS(buildingFamilyIdFromLink(link, origin));
	}
	TEST_CASE("unlisted family detail uses the instance client and rejects changed identity")
	{
		glob2test::HeadlessGlobals globals;
		BuildingLibraryScreenHarness::checkDetail();
	}
}

TEST_SUITE("BuildingLibrary")
{
	TEST_CASE(
		"installed families are explicitly selected and corrupt updates never replace a release")
	{
		glob2test::HeadlessGlobals fixture;
		Online::MemoryStorage storage;
		BuildingLibrary library(storage);
		const auto stock = globalContainer->buildingsTypes;
		auto package = packageFixture("11111111-1111-4111-8111-111111111111");
		auto combined = stock;
		combined.composePackages({package.dump()});
		const auto archiveHash = Online::Sha256::hex("release-one");
		nlohmann::json manifest = {{"schemaVersion", 1},
								   {"name", "Kitchen"},
								   {"namespace", package.at("namespace")},
								   {"archiveHash", archiveHash},
								   {"packageJson", package.dump()},
								   {"packageHash", Online::Sha256::hex(package.dump())},
								   {"catalogHash", combined.fingerprint()},
								   {"baseHash", stock.fingerprint()},
								   {"simVersion", Online::SimVersion::local().key()}};
		library.install(manifest, {});
		CHECK(library.entries().size() == 1);
		CHECK(library.entries().at(0).at("simVersion") == manifest.at("simVersion"));
		CHECK(library.entries().at(0).at("baseHash") == manifest.at("baseHash"));
		// Exercise the production profile adapter as well as MemoryStorage: atomic
		// file writes require their parent directory to exist on a fresh install.
		const auto directory =
			std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0)) /
			"online/buildings";
		std::filesystem::remove_all(directory);
		auto profileStorage = Online::makeUserDirectoryStorage();
		BuildingLibrary profileLibrary(*profileStorage);
		REQUIRE(std::filesystem::is_directory(directory));
		CHECK_NOTHROW(profileLibrary.install(manifest, {}));
		CHECK(profileLibrary.entries().size() == 1);
		profileLibrary.remove(package.at("namespace").get<std::string>());
		CHECK(library.compose(stock).catalog.fingerprint() == stock.fingerprint());
		library.select(package.at("namespace").get<std::string>(), true);
		CHECK(library.compose(stock).catalog.fingerprint() == combined.fingerprint());
		auto corrupt = manifest;
		corrupt["packageHash"] = std::string(64, '0');
		const auto before = storage.files;
		CHECK_THROWS(library.install(corrupt, {}));
		CHECK(storage.files == before);
		auto other = packageFixture("22222222-2222-4222-8222-222222222222");
		auto second = stock;
		second.composePackages({other.dump()});
		auto next = manifest;
		next["namespace"] = other.at("namespace");
		next["packageJson"] = other.dump();
		next["packageHash"] = Online::Sha256::hex(other.dump());
		next["catalogHash"] = second.fingerprint();
		next["archiveHash"] = Online::Sha256::hex("release-two");
		library.install(next, {});
		library.select(other.at("namespace").get<std::string>(), true);
		auto expected = stock;
		expected.composePackages({other.dump(), package.dump()});
		CHECK(library.compose(stock).catalog.fingerprint() == expected.fingerprint());
		storage.files["online/buildings/" + archiveHash + ".json"] = "{}";
		CHECK_THROWS(library.compose(stock));
		library.remove(package.at("namespace").get<std::string>());
		CHECK(library.compose(stock).catalog.fingerprint() == second.fingerprint());
		CHECK(storage.persisted >= 6);
	}
}
