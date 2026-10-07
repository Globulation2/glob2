// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "TerrainRegistry.h"
#include "field/RuntimeTerrainGradient.h"
#include <nlohmann/json.hpp>
#include <queue>
#include <random>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<TerrainRegistry>);
static_assert(!std::is_copy_assignable_v<TerrainRegistry>);
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
		constexpr auto first = TerrainType(TERRAIN_COUNT), second = TerrainType(TERRAIN_COUNT + 1),
					   third = TerrainType(TERRAIN_COUNT + 2);
		REQUIRE(registry->size() == TERRAIN_COUNT + 2);
		CHECK(registry->find("test:a") == first);
		CHECK(registry->find("test:z") == second);
		CHECK(registry->properties(first).groundSpeedQ8 == 192);
		CHECK_FALSE(registry->compatibility(first).legacyCorners);
		CHECK(registry->appearance(first) == SAND);
		auto updated = registry->importJson(source(Json::array(
			{definition("test:a", "grass", {{"groundSpeedQ8", 64}}), definition("test:0")})));
		CHECK(updated->find("test:a") == first);
		CHECK(updated->find("test:0") == third);
		CHECK(registry->properties(first).groundSpeedQ8 == 192);
		CHECK(builtin->size() == TERRAIN_COUNT);
		CHECK(builtin->checksum() == 0);
		const auto serialized = updated->serialize();
		CHECK(serialized == Json::parse(serialized).dump());
		auto loaded = TerrainRegistry::deserialize(serialized);
		CHECK(loaded->serialize() == updated->serialize());
		CHECK(loaded->digest() == updated->digest());
		CHECK(std::string(loaded->presentation(first).name) == "test:a");
		CHECK(std::string(loaded->presentation(first).label) == "Custom terrain");
		CHECK(loaded->movement(0).minimum == 5); // Built-in road remains an admissible lower bound.
	}
	TEST_CASE("appearance presets preserve snapshot names and resolved saved metadata")
	{
		auto first = definition("test:first");
		auto second = definition("test:second", "grass", {{"groundSpeedQ8", 192}});
		first["name"] = "First terrain";
		second["name"] = "Second terrain";
		const auto snapshot =
			TerrainRegistry::builtins()->importJson(source(Json::array({first, second})));
		const auto firstId = *snapshot->find("test:first");
		const auto secondId = *snapshot->find("test:second");
		CHECK(snapshot->appearance(firstId) == snapshot->appearance(secondId));
		CHECK(snapshot->appearance(firstId) == SAND);
		CHECK(snapshot->propertyIndex(firstId) != snapshot->propertyIndex(secondId));
		CHECK_FALSE(snapshot->compatibility(firstId).legacyCorners);
		CHECK(snapshot->compatibility(firstId).firstFrame == terrainCompatibility(SAND).firstFrame);

		first["name"] = "Replacement name";
		const auto replacement = snapshot->importJson(source(Json::array({first})));
		CHECK(std::string(snapshot->presentation(firstId).name) == "test:first");
		CHECK(std::string(snapshot->presentation(firstId).label) == "First terrain");
		CHECK(std::string(replacement->presentation(firstId).label) == "Replacement name");
		CHECK(std::string(replacement->presentation(secondId).label) == "Second terrain");

		// Format 136 retains every resolved field, including legacy timing that
		// no longer drives drawing. Appearance material catalogs cannot rewrite
		// authoritative saved bytes or their simulation digest.
		for (const auto *field : {"minimap", "animationTicks"})
		{
			CAPTURE(field);
			auto saved = Json::parse(snapshot->serialize());
			auto &value = saved["terrains"][1]["presentation"][field];
			if (value.is_array())
				value[0] = (value[0].get<unsigned>() + 1) % 256;
			else
				value = value.get<unsigned>() + 1;
			const auto distinct = TerrainRegistry::deserialize(saved.dump());
			CHECK(distinct->serialize() == saved.dump());
			CHECK(distinct->digest() != snapshot->digest());
			if (std::string_view(field) == "minimap")
				CHECK(distinct->presentation(secondId).minimap.r == value[0].get<unsigned>());
		}
	}
	TEST_CASE("farm material names support every material and import legacy crop slots")
	{
		const auto base = TerrainRegistry::builtins();
		const auto registry = base->importJson(source(Json::array({
			definition("test:fabric", "grass", {{"farmMaterial", "fabric"}, {"allowedResources", 0}}),
			definition("test:legacy", "grass", {{"farmCrop", 1}}),
			definition("test:none", "grass", {{"farmMaterial", nullptr}})})));
		CHECK(registry->properties(*registry->find("test:fabric")).farmMaterial == materialIndex(MaterialId::Fabric));
		CHECK(registry->properties(*registry->find("test:legacy")).farmMaterial == materialIndex(MaterialId::Food));
		CHECK(registry->properties(*registry->find("test:none")).farmMaterial == 255);
		const auto saved = Json::parse(registry->serialize());
		CHECK(saved["terrains"][0]["properties"]["farmMaterial"] == "fabric");
		CHECK_FALSE(saved["terrains"][0]["properties"].contains("farmCrop"));
		CHECK(TerrainRegistry::deserialize(saved.dump())->serialize() == registry->serialize());
		auto legacySaved = saved;
		for (auto& terrain : legacySaved["terrains"])
		{
			terrain["properties"].erase("farmMaterial");
			terrain["properties"]["farmCrop"] = 1;
		}
		CHECK(TerrainRegistry::deserialize(legacySaved.dump())->properties(*registry->find("test:fabric")).farmMaterial == materialIndex(MaterialId::Food));
		for (const auto& invalid : {Json{{"farmMaterial", "wheat"}}, Json{{"farmMaterial", 11}},
			Json{{"farmMaterial", "food"}, {"farmCrop", 1}}})
			CHECK_THROWS(base->importJson(source(Json::array({definition("test:invalid", "grass", invalid)}))));
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
		CHECK_THROWS_AS(registry->importJson(std::string(32, '[') + "0" + std::string(32, ']')),
						std::invalid_argument);
		auto imported = registry->importJson(source(Json::array({definition()})));
		auto saved = Json::parse(imported->serialize());
		saved["terrains"][0]["id"] = TERRAIN_COUNT + 3;
		CHECK_THROWS(TerrainRegistry::deserialize(saved.dump()));
		saved = Json::parse(imported->serialize());
		saved["terrains"][0]["presentation"]["firstFrame"] = 999999;
		CHECK_THROWS(TerrainRegistry::deserialize(saved.dump()));
		CHECK(registry->size() == TERRAIN_COUNT);
	}
	TEST_CASE("catalogue groups share one profile, every paintable built-in is a preset and "
			  "format-136 saved IDs remap behind the current built-ins")
	{
		const auto builtin = TerrainRegistry::builtins();
		// A group is one mechanic: the registry deduplicates its members into one
		// property profile, and no two groups collapse together.
		CHECK(builtin->propertyProfiles().size() == TERRAIN_GROUP_COUNT);
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
			for (unsigned j = 0; j < TERRAIN_COUNT; ++j)
			{
				CAPTURE(i);
				CAPTURE(j);
				CHECK((builtin->propertyIndex(TerrainType(i)) == builtin->propertyIndex(TerrainType(j))) ==
					  (terrainGroup(TerrainType(i)) == terrainGroup(TerrainType(j))));
			}
		// Built-in movement stays on the small prepared tables and the 64-bucket ring.
		static_assert(gradient_kernel::BUCKETS == 64);
		for (unsigned swim = 0; swim < 7; ++swim)
			CHECK(std::holds_alternative<gradient_kernel::PreparedTerrainCosts<8>>(
				*builtin->movement(swim).prepared));
		unsigned presets = 0;
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
		{
			const auto type = TerrainType(i);
			const auto *name = TerrainPresentations[i].name;
			Json item = {{"key", "test:preset"},
						 {"name", "Preset"},
						 {"base", name},
						 {"properties", Json::object()},
						 {"appearance", name}};
			CAPTURE(name);
			if (!terrainPaintable(type))
			{
				CHECK_THROWS(builtin->importJson(source(Json::array({item}))));
				continue;
			}
			++presets;
			const auto registry = builtin->importJson(source(Json::array({item})));
			const auto id = *registry->find("test:preset");
			CHECK(registry->appearance(id) == type);
			CHECK(registry->propertyIndex(id) == registry->propertyIndex(type));
			CHECK(registry->compatibility(id).firstFrame == terrainCompatibility(type).firstFrame);
			CHECK(registry->compatibility(id).variants == terrainCompatibility(type).variants);
			CHECK_FALSE(registry->compatibility(id).legacyCorners);
			CHECK(registry->presentation(id).minimap.r == TerrainPresentations[i].minimap.r);
			// Saved metadata round-trips through the frozen-frame check.
			CHECK(TerrainRegistry::deserialize(registry->serialize())->digest() == registry->digest());
		}
		CHECK(presets == TERRAIN_COUNT - 2);

		// A file written with seven built-ins numbers its definitions from 7; the
		// loader renumbers them behind the current built-ins without changing content.
		const auto registry = builtin->importJson(
			source(Json::array({definition("test:z"), definition("test:a", "grass", {{"groundSpeedQ8", 192}})})));
		auto saved = Json::parse(registry->serialize());
		for (auto &item : saved["terrains"])
			item["id"] = item["id"].get<unsigned>() - TERRAIN_COUNT + TERRAIN_COUNT_BEFORE_CATALOGUE;
		CHECK(saved["terrains"][0]["id"] == TERRAIN_COUNT_BEFORE_CATALOGUE);
		const auto legacy = TerrainRegistry::deserialize(saved.dump(), TERRAIN_COUNT_BEFORE_CATALOGUE);
		CHECK(legacy->serialize() == registry->serialize());
		CHECK(legacy->digest() == registry->digest());
		CHECK(legacy->find("test:a") == TerrainType(TERRAIN_COUNT));
		CHECK(legacy->find("test:z") == TerrainType(TERRAIN_COUNT + 1));
		CHECK_THROWS(TerrainRegistry::deserialize(saved.dump()));
		CHECK_THROWS(TerrainRegistry::deserialize(registry->serialize(), TERRAIN_COUNT_BEFORE_CATALOGUE));
		CHECK_THROWS(TerrainRegistry::deserialize(saved.dump(), TERRAIN_COUNT_BEFORE_CATALOGUE - 1));
		CHECK_THROWS(TerrainRegistry::deserialize(saved.dump(), TERRAIN_COUNT + 1));
		CHECK(TerrainRegistry::deserialize(R"({"schemaVersion":1,"terrains":[]})", TERRAIN_COUNT_BEFORE_CATALOGUE) == builtin);
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
			  registry->propertyIndex(TerrainType(TERRAIN_COUNT)));
		// Equivalent definitions add no movement profile beyond the built-ins'.
		CHECK(registry->movement(4).profiles.size() ==
			  TerrainRegistry::builtins()->movement(4).profiles.size());
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
	TEST_CASE("runtime searches reject undersized queues before changing the field")
	{
		auto registry = TerrainRegistry::builtins()->importJson(
			source(Json::array({definition("test:slow", "water", {{"groundSpeedQ8", 64}})})));
		const auto slow = *registry->find("test:slow");
		std::vector<TerrainType> ids(1024, slow);
		std::vector<std::uint16_t> seeds(1024, GRADIENT_UNREACHABLE);
		seeds[0] = GRADIENT_AT_GOAL;
		GradientWorkspace workspace;
		for (unsigned buckets : {32, 64, 128, 512})
		{
			CAPTURE(buckets);
			auto actual = seeds;
			CHECK_THROWS_AS(gradient_kernel::propagateTerrainField(
								actual.data(), 6, 500, {32, 32}, workspace,
								[&](std::size_t i) { return ids[i]; }, true, *registry, buckets),
							std::invalid_argument);
			CHECK(actual == seeds);
			std::vector<std::uint8_t> profiles(1024, registry->movement(6).profileIds[slow]);
			CHECK_THROWS_AS(gradient_kernel::propagateTerrainProfiles(
								actual.data(), 6, 500, {32, 32}, workspace, profiles.data(),
								*registry, buckets),
							std::invalid_argument);
			CHECK(actual == seeds);
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
		seeds.assign(1024, GRADIENT_UNREACHABLE);
		seeds[0] = GRADIENT_AT_GOAL;
		std::vector<std::uint8_t> profiles(1024, registry->movement(6).profileIds[TRAIL]);
		gradient_kernel::propagateTerrainProfiles(seeds.data(), 6, 500, {32, 32}, workspace,
												  profiles.data(), *registry, 64);
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
