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
        REQUIRE(registry->size() == MaterialCount);
        CHECK(registry->key(static_cast<ResourceId>(0)) == "trees");
        CHECK(registry->key(static_cast<ResourceId>(1)) == "wheat");
        CHECK(registry->key(static_cast<ResourceId>(6)) == "orange-tree");
        for (unsigned i = 0; i < MaterialCount; ++i)
        {
            const auto id = static_cast<ResourceId>(i);
            const auto material = static_cast<MaterialId>(i);
            CHECK(registry->properties(id).materialMask == materialBit(material));
            CHECK(registry->properties(id).primaryMaterial == material);
            CHECK(registry->yields(id)[i].capacity > 0);
            CHECK(registry->yields(id)[i].initial == 1);
            CHECK(registry->requiredExperiment(id).empty() == (i < 8));
        }
        CHECK(registry->yields(*registry->find("trees"))[0].destroysDeposit);
        CHECK(registry->yields(*registry->find("rocks"))[3].consumption == ResourceConsumption::Infinite);
        CHECK(registry->properties(*registry->find("wheat")).growthRate == ResourceRateScale / 3);
        CHECK(registry->properties(*registry->find("algae")).ecology == ResourceEcology::Shore);
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
        CHECK(original->size() == 12);
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
