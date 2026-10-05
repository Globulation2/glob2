// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "BuildingType.h"
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

TEST_CASE("invalid catalog is rejected atomically")
{
    BuildingsTypes registry; registry.initLegacy();
    const auto before = registry.snapshotJson();
    using Json = nlohmann::json;
    const auto good = Json::parse(before);
    const auto rejected = [&](Json bad) {
        CHECK_THROWS_AS(registry.loadSnapshotJson(bad.dump()), std::exception);
        CHECK(registry.snapshotJson() == before);
    };
    auto bad = good; bad["variants"][1]["key"] = bad["variants"][0]["key"]; rejected(bad);
    bad = good; bad["variants"][1]["id"] = 0; rejected(bad);
    bad = good; bad["variants"][0]["next"] = "missing"; rejected(bad);
    bad = good; bad["variants"][1]["next"] = bad["variants"][0]["key"]; rejected(bad);
    bad = good; bad["variants"][1]["next"] = bad["variants"][3]["key"]; rejected(bad);
    bad = good; bad["variants"][1]["previous"] = "missing.repair.site"; rejected(bad);
    bad = good; bad["variants"][0]["properties"]["width"] = 0; rejected(bad);
    bad = good; bad["variants"][1]["properties"]["hpInit"] = 701; rejected(bad);
    bad = good; bad["variants"][1]["properties"]["hpMax"] = 0; rejected(bad);
    bad = good; bad["variants"][0]["properties"]["maxResource"][8] = 1; rejected(bad);
    bad = good; bad["variants"][0]["semantics"]["replenishResources"] = {"gold"}; rejected(bad);
    bad = good; bad["variants"][0]["semantics"]["market"]["suppliesStockResources"] = {"wood","wood"}; rejected(bad);
    bad = good; bad["variants"][0]["properties"]["width"] = 1.5; rejected(bad);
    bad = good; bad["variants"][0]["requiredExperiment"] = "unknown-feature"; rejected(bad);
    bad = good; bad["variants"][1]["semantics"]["production"]["recipes"]["warrior"]["duration"] = 17; rejected(bad);
    bad = good; bad["variants"][3]["semantics"]["feeding"]["cost"]["gold"] = 2; rejected(bad);
    bad = good; bad["variants"][3]["semantics"]["feeding"]["cost"]["wheat"] = -1; rejected(bad);
    bad = good; bad["variants"][3]["properties"]["canFeedUnit"]=1; rejected(bad);
    bad = good; bad["variants"][3]["semantics"]["training"]["stopWalk"]={{"enabled",true}}; rejected(bad);
    CHECK_THROWS_AS(registry.loadSnapshotJson("{\"schemaVersion\":1,\"schemaVersion\":1}"), std::exception);
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
    for(int i=0;i<59;++i) catalog["experiments"].push_back({{"key","feature-"+std::to_string(i)}, {"label","Label"},{"help","Help"}});
    CHECK_NOTHROW(registry.loadSnapshotJson(catalog.dump()));
    catalog["experiments"].push_back({{"key","feature-59"},{"label","Label"},{"help","Help"}});
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

}
