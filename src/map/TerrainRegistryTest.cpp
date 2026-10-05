// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "TerrainRegistry.h"
#include "field/RuntimeTerrainGradient.h"
#include <nlohmann/json.hpp>
#include <queue>
#include <random>
using Json = nlohmann::json;
namespace
{
Json definition(std::string key = "test:mud", std::string base = "grass",
				Json properties = Json::object())
{
	return {{"key", key},
			{"name", "Custom terrain"},
			{"base", base},
			{"properties", properties},
			{"appearance", "sand"}};
}
std::string source(Json definitions)
{
	return Json{{"schemaVersion", 1}, {"terrains", definitions}}.dump();
}
std::vector<std::uint16_t> oracle(std::vector<std::uint16_t> values, int w, int h, int swim,
								  int cap, const std::vector<TerrainType> &ids,
								  const TerrainRegistry &registry)
{
	using Entry = std::pair<unsigned, std::size_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
	for (std::size_t i = 0; i < values.size(); ++i)
		if (values[i] > 1)
			queue.emplace(GRADIENT_AT_GOAL - values[i], i);
	while (!queue.empty())
	{
		auto [cost, i] = queue.top();
		queue.pop();
		if (cost > unsigned(cap) || values[i] != GRADIENT_AT_GOAL - cost)
			continue;
		auto steps = registry.movement(swim).entries[ids[i]];
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				auto n = std::size_t((int(i / w) + dy + h) % h) * w + (int(i % w) + dx + w) % w;
				unsigned candidate = cost + (dx && dy ? steps.diagonal : steps.cardinal);
				if (values[n] && candidate <= unsigned(cap) &&
					GRADIENT_AT_GOAL - candidate > values[n])
				{
					values[n] = GRADIENT_AT_GOAL - candidate;
					queue.emplace(candidate, n);
				}
			}
	}
	return values;
}
} // namespace
TEST_SUITE("TerrainRegistry")
{
	TEST_CASE("imports preserve builtins and IDs and round trip resolved definitions")
	{
		auto builtin = TerrainRegistry::builtins();
		auto registry = builtin->importJson(source(Json::array(
			{definition("test:z"), definition("test:a", "grass", {{"groundSpeedQ8", 192}})})));
		REQUIRE(registry->size() == 9);
		CHECK(registry->find("test:a") == TerrainType(7));
		CHECK(registry->find("test:z") == TerrainType(8));
		CHECK(registry->properties(TerrainType(7)).groundSpeedQ8 == 192);
		CHECK_FALSE(registry->presentation(TerrainType(7)).legacyCorners);
		CHECK(registry->appearance(TerrainType(7)) == SAND);
		auto updated = registry->importJson(source(Json::array(
			{definition("test:a", "grass", {{"groundSpeedQ8", 64}}), definition("test:0")})));
		CHECK(updated->find("test:a") == TerrainType(7));
		CHECK(updated->find("test:0") == TerrainType(9));
		CHECK(registry->properties(TerrainType(7)).groundSpeedQ8 == 192);
		CHECK(builtin->size() == 7);
		CHECK(builtin->checksum() == 0);
		auto loaded = TerrainRegistry::deserialize(updated->serialize());
		CHECK(loaded->serialize() == updated->serialize());
		CHECK(loaded->digest() == updated->digest());
		CHECK(std::string(loaded->presentation(TerrainType(7)).name) == "test:a");
		CHECK(std::string(loaded->presentation(TerrainType(7)).label) == "Custom terrain");
		CHECK(loaded->movement(0).minimum == 5); // Built-in road remains an admissible lower bound.
	}
	TEST_CASE("invalid imports and saved registries are rejected without changing their owner")
	{
		const auto registry = TerrainRegistry::builtins();
		for (const auto &key : {"grass", "glob2:grass", "Bad:name", "test:", ":mud", "test:a:b"})
			CHECK_THROWS(registry->importJson(source(Json::array({definition(key)}))));
		CHECK_THROWS(registry->importJson(source(Json::array({definition(), definition()}))));
		for (auto properties :
			 {Json{{"walkable", true}, {"swimmable", true}}, Json{{"groundSpeedQ8", 0}},
			  Json{{"airSpeedQ8", 1025}}, Json{{"growthQ8", 1.5}}, Json{{"farmCrop", 8}},
			  Json{{"allowedResources", 256}}, Json{{"flyable", 1}}, Json{{"unknown", false}},
			  Json{{"groundHealthQ8", 65535}}})
			CHECK_THROWS(registry->importJson(
				source(Json::array({definition("test:x", "grass", properties)}))));
		CHECK_THROWS(
			registry->importJson(R"({"schemaVersion":1,"schemaVersion":1,"terrains":[]})"));
		auto imported = registry->importJson(source(Json::array({definition()})));
		auto saved = Json::parse(imported->serialize());
		saved["terrains"][0]["id"] = 10;
		CHECK_THROWS(TerrainRegistry::deserialize(saved.dump()));
		saved = Json::parse(imported->serialize());
		saved["terrains"][0]["presentation"]["firstFrame"] = 999999;
		CHECK_THROWS(TerrainRegistry::deserialize(saved.dump()));
		CHECK(registry->size() == TERRAIN_COUNT);
	}
	TEST_CASE("large registries compile equivalent definitions into bounded movement profiles")
	{
		Json types = Json::array();
		for (unsigned i = 0; i < TerrainRegistry::Capacity - TERRAIN_COUNT; ++i)
			types.push_back(definition("many:t" + std::to_string(i)));
		auto registry = TerrainRegistry::builtins()->importJson(source(types));
		CHECK(registry->size() == TerrainRegistry::Capacity);
		CHECK(registry->propertyProfiles().size() ==
			  TerrainRegistry::builtins()->propertyProfiles().size());
		CHECK(registry->propertyIndex(TerrainType(1000)) ==
			  registry->propertyIndex(TerrainType(7)));
		CHECK(registry->movement(4).profiles.size() == 4);
		CHECK_THROWS(registry->importJson(source(Json::array({definition("overflow:x")}))));
		CHECK(TerrainRegistry::deserialize(registry->serialize())->digest() == registry->digest());
	}
	TEST_CASE(
		"runtime SIMD propagation agrees with heap oracle across queue boundaries and thin grids")
	{
		auto registry = TerrainRegistry::builtins()->importJson(
			source(Json::array({definition("test:quarter", "water", {{"groundSpeedQ8", 64}}),
								definition("test:half", "water", {{"groundSpeedQ8", 128}}),
								definition("test:fast", "grass", {{"groundSpeedQ8", 1024}}),
								definition("test:mud", "grass", {{"groundSpeedQ8", 192}})})));
		std::mt19937 rng(42);
		for (auto [w, h] :
			 {std::pair{1, 32}, std::pair{32, 1}, std::pair{32, 32}, std::pair{17, 13}})
			for (int swim = 0; swim < 7; ++swim)
				for (int cap : {60, 500, gradient_kernel::COST_LIMIT})
				{
					std::vector<TerrainType> ids(w * h);
					std::vector<std::uint16_t> seeds(w * h, 1);
					unsigned largest = 0;
					for (unsigned i = 0; i < ids.size(); ++i)
					{
						ids[i] = TerrainType(rng() % registry->size());
						if (rng() % 13 == 0)
							seeds[i] = 0;
						largest =
							std::max(largest, registry->movement(swim).entries[ids[i]].diagonal);
					}
					unsigned buckets = 64;
					while (buckets <= largest)
						buckets *= 2;
					seeds[rng() % seeds.size()] = GRADIENT_AT_GOAL;
					seeds[rng() % seeds.size()] = GRADIENT_AT_GOAL - 90;
					auto expected = oracle(seeds, w, h, swim, cap, ids, *registry);
					GradientWorkspace workspace;
					auto actual = seeds;
					gradient_kernel::propagateTerrainField(
						actual.data(), swim, cap, {w, h}, workspace,
						[&](size_t i) { return ids[i]; }, true, *registry, buckets);
					CHECK(actual == expected);
					std::vector<std::uint8_t> profiles(ids.size());
					for (std::size_t i = 0; i < ids.size(); ++i)
						profiles[i] = registry->movement(swim).profileIds[ids[i]];
					actual = seeds;
					gradient_kernel::propagateTerrainProfiles(actual.data(), swim, cap, {w, h},
															  workspace, profiles.data(), *registry,
															  buckets);
					CHECK(actual == expected);
				}
	}
	TEST_CASE("unused large edge definitions preserve the 64 bucket path")
	{
		auto registry = TerrainRegistry::builtins()->importJson(
			source(Json::array({definition("test:slow", "water", {{"groundSpeedQ8", 64}})})));
		std::vector<TerrainType> ids(1024, TRAIL);
		std::vector<std::uint16_t> seeds(1024, 1);
		seeds[0] = GRADIENT_AT_GOAL;
		auto expected = oracle(seeds, 32, 32, 6, 500, ids, *registry);
		GradientWorkspace workspace;
		gradient_kernel::propagateTerrainField(
			seeds.data(), 6, 500, {32, 32}, workspace, [&](size_t i) { return ids[i]; }, true,
			*registry, 64);
		CHECK(seeds == expected);
	}
}

TEST_CASE("all legal speed factors preserve distinct cost collisions" *
		  doctest::test_suite("TerrainRegistry"))
{
	Json types = Json::array();
	for (unsigned speed = 64; speed <= 1024; ++speed)
		types.push_back(
			definition("cost:s" + std::to_string(speed), "water", {{"groundSpeedQ8", speed}}));
	auto registry = TerrainRegistry::builtins()->importJson(source(types));
	std::vector<TerrainType> ids(1024);
	std::vector<std::uint16_t> seeds(1024, 1);
	std::mt19937 rng(109);
	for (auto &id : ids)
		id = TerrainType(rng() % registry->size());
	seeds[0] = GRADIENT_AT_GOAL;
	for (unsigned swim = 0; swim < 7; ++swim)
	{
		const auto expected =
			oracle(seeds, 32, 32, swim, gradient_kernel::COST_LIMIT, ids, *registry);
		std::vector<std::uint8_t> profiles(1024);
		for (unsigned i = 0; i < 1024; ++i)
			profiles[i] = registry->movement(swim).profileIds[ids[i]];
		auto actual = seeds;
		GradientWorkspace workspace;
		gradient_kernel::propagateTerrainProfiles(actual.data(), swim, gradient_kernel::COST_LIMIT,
												  {32, 32}, workspace, profiles.data(), *registry,
												  256);
		CHECK(actual == expected);
	}
}
