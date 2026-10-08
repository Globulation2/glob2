// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceRegistry.h"
#include <nlohmann/json.hpp>
#include <random>
#include <algorithm>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<ResourceRegistry>);
static_assert(!std::is_same_v<MaterialId, ResourceId>);
using Json = nlohmann::json;
namespace
{
Json definition(std::string key = "test:mixed")
{
    return {{"key", key},
            {"properties", {{"ecology", "land"}, {"growthRate", ResourceRateScale}, {"spreadRate", ResourceRateScale / 2},
                            {"blocksGround", false}, {"persistsWhenEmpty", true}}},
            {"yields", {{"food", {{"capacity", 9}, {"initial", 3}, {"consumption", "one"}}},
                        {"paper", {{"capacity", 6}, {"initial", 2}, {"consumption", "all"}, {"growthRate", ResourceRateScale / 3}}}}},
            {"presentation", {{"name", "Mixed crop"}, {"sprite", "data/gfx/ressource"}, {"minimap", {10, 20, 30}},
                              {"levels", Json::array({{{"stock", 0}, {"variants", Json::array({{{"frame", 1}, {"weight", 1}}})}},
                                                      {{"stock", 5}, {"variants", Json::array({{{"frame", 2}, {"weight", 1}}, {{"frame", 3}, {"weight", 3}}})}}})}}}};
}
std::string source(Json definitions)
{
    return Json{{"schemaVersion", 1}, {"resources", std::move(definitions)}}.dump();
}
std::shared_ptr<const ResourceRegistry> stock()
{
    return ResourceRegistry::loadFile((glob2test::sourceRoot() / "data/resources/registry.json").string());
}
}

TEST_SUITE("ResourceRegistry")
{
    TEST_CASE("stock catalog separates map identities from the twelve fixed materials")
    {
        const auto registry = stock();
        REQUIRE(registry->size() == 24);
        CHECK(registry->key(static_cast<ResourceId>(0)) == "trees");
        CHECK(registry->key(static_cast<ResourceId>(1)) == "wheat");
        CHECK(registry->key(static_cast<ResourceId>(6)) == "orange-tree");
        // The eight legacy deposits keep their slots, one material each, and stay ungated.
        for (unsigned i = 0; i < 8; ++i)
        {
            const auto id = static_cast<ResourceId>(i);
            const auto material = static_cast<MaterialId>(i);
            CHECK(registry->properties(id).materialMask == materialBit(material));
            CHECK(registry->properties(id).primaryMaterial == material);
            CHECK(registry->yields(id)[i].capacity > 0);
            CHECK(registry->yields(id)[i].initial == 1);
            CHECK(registry->requiredExperiment(id).empty());
        }
        // Every later built-in deposit is gated by a declared experiment.
        for (unsigned i = 8; i < registry->size(); ++i)
            CHECK_FALSE(registry->requiredExperiment(static_cast<ResourceId>(i)).empty());
        const std::pair<const char*, MaterialId> foundation[] = {
            {"gold-ore", MaterialId::Gold}, {"iron-ore", MaterialId::Metal}, {"silica", MaterialId::Glass}, {"cotton", MaterialId::Fabric}};
        for (const auto& [key, material] : foundation)
        {
            REQUIRE(registry->find(key));
            const auto id = *registry->find(key);
            CHECK(registry->properties(id).materialMask == materialBit(material));
            CHECK(registry->requiredExperiment(id) == "foundation-resources");
        }
        CHECK(registry->yields(*registry->find("trees"))[0].destroysDeposit);
        CHECK(registry->yields(*registry->find("rocks"))[3].consumption == ResourceConsumption::Infinite);
        CHECK(registry->properties(*registry->find("wheat")).growthRate == ResourceRateScale / 3);
        CHECK(registry->properties(*registry->find("algae")).ecology == ResourceEcology::Shore);
    }

    TEST_CASE("landscape resources reuse existing rules behind their editor experiment")
    {
        const auto registry = stock();
        const char* keys[] = {"jungle-trees", "pine-trees", "dead-trees", "ruins", "camp-site", "ancient-debris",
            "scrub", "tall-grass", "maize", "potatoes", "rice", "fish"};
        for (const auto* key : keys)
        {
            REQUIRE(registry->find(key));
            CHECK(registry->requiredExperiment(*registry->find(key)) == "landscape-resources");
        }
        const auto props = [&](const char* key) { return registry->properties(*registry->find(key)); };
        const auto yield = [&](const char* key, MaterialId m) { return registry->yields(*registry->find(key))[materialIndex(m)]; };
        // Finite deposits never grow and start full.
        for (const auto* key : {"dead-trees", "ruins", "camp-site", "ancient-debris"})
        {
            CHECK(props(key).growthRate == 0);
            CHECK(props(key).spreadRate == 0);
            CHECK_FALSE(props(key).persistsWhenEmpty);
        }
        CHECK(yield("dead-trees", MaterialId::Wood).initial == 3);
        CHECK(props("ruins").materialMask == (materialBit(MaterialId::Wood) | materialBit(MaterialId::Metal)));
        CHECK(props("camp-site").materialMask == (materialBit(MaterialId::Wood) | materialBit(MaterialId::Food) | materialBit(MaterialId::Fabric)));
        CHECK(props("ancient-debris").materialMask == (materialBit(MaterialId::Gold) | materialBit(MaterialId::Metal)));
        CHECK(yield("ruins", MaterialId::Wood).initial == 4);
        // Stone stays a static source so stone gradients keep their cache.
        CHECK((registry->mutableMaterialSources() & materialBit(MaterialId::Stone)) == 0);
        // Undergrowth is walkable but must be cleared before building.
        for (const auto* key : {"scrub", "tall-grass"})
        {
            CHECK_FALSE(props(key).blocksGround);
            CHECK(props(key).blocksBuilding);
            CHECK(props(key).clearable);
        }
        // New crops are farmable food on land; fish are aquatic food.
        for (const auto* key : {"maize", "potatoes", "rice"})
        {
            CHECK(props(key).farmable);
            CHECK(props(key).primaryMaterial == MaterialId::Food);
            CHECK(props(key).habitatMask == ResourceLand);
        }
        CHECK(props("potatoes").ecology == ResourceEcology::Uniform);
        CHECK(props("rice").ecology == ResourceEcology::Shore);
        CHECK(props("fish").habitatMask == ResourceAquatic);
        CHECK(props("fish").primaryMaterial == MaterialId::Food);
        CHECK_FALSE(props("fish").farmable);
    }

    TEST_CASE("landscape artwork maps every stock level to painted frames")
    {
        const auto registry = stock();
        const auto& fish = registry->presentation(*registry->find("fish"));
        CHECK(fish.animationFrames == 12);
        // Each fish level is twelve consecutive frames advanced every three ticks.
        CHECK(fish.frame(1, 0, 0, 0) == 0);
        CHECK(fish.frame(1, 0, 0, 3) == 1);
        CHECK(fish.frame(1, 0, 0, 3 * 11) == 11);
        CHECK(fish.frame(1, 0, 0, 3 * 12) == 0);
        CHECK(fish.frame(5, 7, 9, 3 * 5) == 4 * 12 + 5);
        // Trees pick one of two looks per position; both are the stage's frame.
        const auto& jungle = registry->presentation(*registry->find("jungle-trees"));
        for (int x = 0; x < 16; ++x)
        {
            const auto frame = jungle.frame(5, x, 3, 0);
            CHECK((frame == 4 || frame == 9));
        }
        // Scavenge sites shrink with their total stock.
        const auto& ruins = registry->presentation(*registry->find("ruins"));
        CHECK(ruins.frame(6, 0, 0, 0) == 5);
        CHECK(ruins.frame(1, 0, 0, 0) == 0);
    }

    TEST_CASE("source mutability follows every definition and destructive secondary yield")
    {
        CHECK((stock()->mutableMaterialSources() & materialBit(MaterialId::Stone)) == 0);
        CHECK((stock()->mutableMaterialSources() & materialBit(MaterialId::Paper)) != 0);
        CHECK((stock()->mutableMaterialSources() & materialBit(MaterialId::Food)) != 0);
        auto fixed = definition("test:fixed");
        fixed["properties"] = {{"primaryMaterial", "gold"}, {"clearable", false},
            {"growthRate", 0}, {"spreadRate", 0}, {"ecology", "none"}, {"persistsWhenEmpty", true}};
        fixed["yields"] = {{"gold", {{"capacity", 9}, {"initial", 1}, {"consumption", "infinite"}}}};
        const auto maskFor = [](const Json& item) { return ResourceRegistry::fromJson(source(Json::array({item})))->mutableMaterialSources(); };
        CHECK(maskFor(fixed) == 0);
        for (const auto* consumption : {"one", "all"})
        {
            auto changed = fixed;
            changed["yields"]["gold"]["consumption"] = consumption;
            CHECK(maskFor(changed) == materialBit(MaterialId::Gold));
        }
        auto changed = fixed;
        changed["properties"]["clearable"] = true;
        CHECK(maskFor(changed) == materialBit(MaterialId::Gold));
        changed = fixed;
        changed["properties"]["growthRate"] = ResourceRateScale;
        // A rate without ecology has no natural source mutation.
        CHECK(maskFor(changed) == 0);
        changed["properties"]["ecology"] = "uniform";
        CHECK(maskFor(changed) == materialBit(MaterialId::Gold));
        changed["yields"]["gold"]["growthRate"] = 0;
        CHECK(maskFor(changed) == 0);
        changed["properties"]["spreadRate"] = ResourceRateScale;
        CHECK(maskFor(changed) == materialBit(MaterialId::Gold));
        for (const auto* consumption : {"one", "all"})
        {
            changed = fixed;
            changed["yields"]["food"] = {{"capacity", 3}, {"initial", 1}, {"consumption", consumption}, {"destroysDeposit", true}};
            CHECK(maskFor(changed) == (materialBit(MaterialId::Gold) | materialBit(MaterialId::Food)));
        }
        changed = fixed;
        changed["yields"]["food"] = {{"capacity", 3}, {"initial", 1}, {"consumption", "one"}};
        CHECK(maskFor(changed) == materialBit(MaterialId::Food));

        Json many = Json::array();
        for (unsigned i = 0; i < 260; ++i)
        {
            auto copy = fixed;
            copy["key"] = "test:static-" + std::to_string(i);
            many.push_back(copy);
        }
        auto registry = ResourceRegistry::fromJson(source(many));
        CHECK(registry->mutableMaterialSources() == 0);
        changed = fixed;
        changed["key"] = "test:finite";
        changed["yields"]["gold"]["consumption"] = "one";
        registry = registry->importJson(source(Json::array({changed})));
        REQUIRE(resourceIndex(*registry->find("test:finite")) > 255);
        CHECK(registry->mutableMaterialSources() == materialBit(MaterialId::Gold));
        CHECK(ResourceRegistry::deserialize(registry->serialize())->mutableMaterialSources() == registry->mutableMaterialSources());
    }

    TEST_CASE("legacy definitions are frozen while default catalogs pin historical identities")
    {
        const auto frozen=ResourceRegistry::legacy();
        REQUIRE(frozen->size()==8);
        const auto original=frozen->serialize();
        auto installed=Json::parse(stock()->serialize());
        std::reverse(installed["resources"].begin(),installed["resources"].end());
        for(auto& resource:installed["resources"])
            if(resource["key"]=="trees") resource["properties"]["growthRate"]=0;
        glob2test::TempDir temporary("resource-default-order");
        const auto path=temporary.path/"registry.json";
        glob2test::writeFile(path,installed.dump());
        const auto defaults=ResourceRegistry::loadDefaultsFile(path.string());
        for(unsigned i=0;i<8;++i)
            CHECK(defaults->key(static_cast<ResourceId>(i))==frozen->key(static_cast<ResourceId>(i)));
        CHECK(defaults->properties(*defaults->find("trees")).growthRate==0);
        CHECK(frozen->properties(*frozen->find("trees")).growthRate==ResourceRateScale);
        for(unsigned i=9;i<defaults->size();++i)
            CHECK(defaults->key(static_cast<ResourceId>(i-1))<defaults->key(static_cast<ResourceId>(i)));
        CHECK(ResourceRegistry::legacy()->serialize()==original);
        installed["resources"].erase(std::remove_if(installed["resources"].begin(),installed["resources"].end(),
            [](const auto& resource){return resource.at("key")=="trees";}),installed["resources"].end());
        glob2test::writeFile(path,installed.dump());
        CHECK_THROWS(ResourceRegistry::loadDefaultsFile(path.string()));
        std::filesystem::remove(path);
        CHECK_THROWS(ResourceRegistry::loadDefaultsFile(path.string()));
        CHECK(ResourceRegistry::legacy()->serialize()==original);
    }

    TEST_CASE("resolved snapshot restores all material and presentation fields without installed files")
    {
        const auto registry = ResourceRegistry::fromJson(source(Json::array({definition()})));
        const auto wire = registry->serialize();
        const auto restored = ResourceRegistry::deserialize(wire);
        CHECK(restored->serialize() == wire);
        CHECK(restored->digest() == registry->digest());
        CHECK(restored->checksum() == registry->checksum());
        REQUIRE(restored->find("test:mixed"));
        const auto id = *restored->find("test:mixed");
        CHECK(restored->properties(id).materialMask == (materialBit(MaterialId::Food) | materialBit(MaterialId::Paper)));
        CHECK(restored->yields(id)[materialIndex(MaterialId::Paper)].consumption == ResourceConsumption::All);
        CHECK(restored->yields(id)[materialIndex(MaterialId::Food)].initial == 3);
    }

    TEST_CASE("imports preserve existing IDs and append new identities deterministically beyond byte IDs")
    {
        const auto original = stock();
        Json additions = Json::array();
        for (unsigned i = 0; i < 300; ++i) additions.push_back(definition("test:r" + std::to_string(i)));
        const auto forward = original->importJson(source(additions));
        std::reverse(additions.begin(), additions.end());
        const auto backward = original->importJson(source(additions));
        CHECK(forward->serialize() == backward->serialize());
        CHECK(forward->size() == original->size() + 300);
        CHECK(original->size() == 24);
        for (unsigned i = 0; i < original->size(); ++i)
            CHECK(forward->find(original->key(static_cast<ResourceId>(i))) == static_cast<ResourceId>(i));
        CHECK(forward->valid(300));
        CHECK_FALSE(forward->valid(NoResource));
        CHECK(ResourceRegistry::deserialize(forward->serialize())->serialize() == forward->serialize());
    }

    TEST_CASE("malformed definitions fail without publishing partial imports")
    {
        const auto original = stock();
        const auto digest = original->digest();
        auto invalid = definition();
        invalid["yields"]["food"]["capacity"] = 0;
        CHECK_THROWS(original->importJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["properties"]["growhtRate"] = 10;
        CHECK_THROWS(original->importJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["yields"]["unknown"] = {{"capacity", 3}};
        CHECK_THROWS(original->importJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["yields"]["food"]["initial"] = 10;
        CHECK_THROWS(original->importJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["yields"]["food"]["growthRate"] = -1;
        CHECK_THROWS(original->importJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["yields"]["food"]["growthRate"] = 0.5;
        CHECK_THROWS(original->importJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["requiredExperiment"] = "not-declared";
        CHECK_THROWS(original->importJson(source(Json::array({invalid}))));
        CHECK_THROWS(original->importJson(source(Json::array({definition(), definition()}))));
        CHECK_THROWS(ResourceRegistry::fromJson(R"({"schemaVersion":1,"resources":[],"resources":[]})"));
        CHECK(original->digest() == digest);
    }

    TEST_CASE("artwork chooses by total stock and stable identity without simulation randomness")
    {
        const auto a = ResourceRegistry::fromJson(source(Json::array({definition()})));
        const auto b = ResourceRegistry::fromJson(source(Json::array({definition("test:other"), definition()})));
        const auto& first = a->presentation(*a->find("test:mixed"));
        const auto& second = b->presentation(*b->find("test:mixed"));
        for (int y = -8; y < 8; ++y)
            for (int x = -8; x < 8; ++x)
            {
                CHECK(first.frame(4, x, y) == 1);
                CHECK(first.frame(5, x, y) >= 2);
                CHECK(first.frame(5, x, y) <= 3);
                CHECK(first.frame(5, x, y) == second.frame(5, x, y));
            }
        auto invalid = definition(); invalid["presentation"]["sprite"] = "data/../private";
        CHECK_THROWS(ResourceRegistry::fromJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["presentation"]["levels"][0]["variants"][0]["weight"] = 0;
        CHECK_THROWS(ResourceRegistry::fromJson(source(Json::array({invalid}))));
        invalid = definition(); invalid["presentation"]["levels"][0]["variants"][0]["frame"] = 65535;
        invalid["presentation"]["animationFrames"] = 2;
        CHECK_THROWS(ResourceRegistry::fromJson(source(Json::array({invalid}))));
    }

    TEST_CASE("catalog experiments retain their metadata in embedded snapshots")
    {
        auto value = definition(); value["requiredExperiment"] = "test-crop";
        const Json catalog = {{"schemaVersion", 1}, {"resources", Json::array({value})},
                              {"experiments", Json::array({{{"key", "test-crop"}, {"label", "Test crop"}, {"help", "Enables the test crop."}}})}};
        const auto registry = ResourceRegistry::fromJson(catalog.dump());
        REQUIRE(registry->experiments().size() == 1);
        CHECK(registry->experimentKeys() == std::vector<std::string>{"test-crop"});
        CHECK(ResourceRegistry::deserialize(registry->serialize())->experiments() == registry->experiments());
        auto conflict = catalog; conflict["experiments"][0]["label"] = "Conflicting label";
        CHECK_THROWS(registry->importJson(conflict.dump()));
    }

    TEST_CASE("fixed generated property combinations have canonical round trips")
    {
        std::mt19937 random(0x739f24a1);
        Json definitions = Json::array();
        for (unsigned i = 0; i < 100; ++i)
        {
            auto value = definition("fixture:resource-" + std::to_string(i));
            value["properties"]["blocksGround"] = bool(random() & 1);
            value["properties"]["blocksAir"] = bool(random() & 1);
            value["properties"]["clearable"] = bool(random() & 1);
            value["properties"]["growthRate"] = random() % (4 * ResourceRateScale + 1);
            value["properties"]["spreadRate"] = random() % (ResourceRateScale + 1);
            value["yields"]["food"]["capacity"] = 3 + random() % 65533;
            value["yields"]["paper"]["capacity"] = 2 + random() % 65534;
            value["yields"]["paper"]["consumption"] = (random() & 1) ? "one" : "infinite";
            definitions.push_back(std::move(value));
        }
        const auto registry = ResourceRegistry::fromJson(source(definitions));
        const auto restored = ResourceRegistry::deserialize(registry->serialize());
        CHECK(restored->serialize() == registry->serialize());
        for (unsigned i = 0; i < registry->size(); ++i)
        {
            CHECK(restored->properties(static_cast<ResourceId>(i)) == registry->properties(static_cast<ResourceId>(i)));
            CHECK(restored->yields(static_cast<ResourceId>(i)) == registry->yields(static_cast<ResourceId>(i)));
        }
    }
}
