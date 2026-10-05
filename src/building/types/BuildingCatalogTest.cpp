// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "BuildingType.h"
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
    bad = good; bad["variants"][1]["previous"] = bad["variants"][2]["key"]; rejected(bad);
    bad = good; bad["variants"][0]["properties"]["width"] = 0; rejected(bad);
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

}
