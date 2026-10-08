// SPDX-License-Identifier: GPL-3.0-or-later
// Deliberately uses pre-pipeline APIs: compile this harness against the baseline
// to emit starting saves that both baseline and candidate executables can load.
#include "EngineFixtures.h"
#include "Engine.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Toolkit.h>
#include <nlohmann/json.hpp>
#include <cstdlib>

TEST_CASE("controlled full-engine growth fixtures [benchmark][resources]" *
		  doctest::test_suite("ResourceGrowthFixtures"))
{
	const char *input = std::getenv("GLOB2_GROWTH_FIXTURE_INPUT");
	const char *output = std::getenv("GLOB2_GROWTH_FIXTURE_OUTPUT");
	if (!input || !output)
		return;
	glob2test::GlobalsOptions options;
	options.loadStrings = true;
	glob2test::HeadlessGlobals globals(options);
	for (const auto &[scenario, size] : {std::pair{"sparse", 128},
										 {"dense", 256},
										 {"saturated", 512},
										 {"harvested", 256},
										 {"blocked", 128},
										 {"multi", 512},
										 {"disabled", 256}})
	{
		CAPTURE(scenario);
		Engine engine;
		const auto source = std::filesystem::path(input) /
							(std::string(scenario == std::string("harvested") ? "ai" : "idle") +
							 std::to_string(size)) /
							"initial.game.gz";
		REQUIRE(engine.initCustom(source.string()) == Engine::EE_NO_ERROR);
		auto &game = engine.gui.game;
		auto &map = game.map;
		REQUIRE(game.stepCounter == 0);
		const auto random = game.bindRandom();
		game.syncRandom.seed(713);
		auto catalog = nlohmann::json::parse(map.resourceRegistry().serialize());
		auto crop = catalog["resources"][1];
		crop["key"] = "benchmark-crop";
		crop["properties"]["ecology"] = "uniform";
		if (scenario == std::string("multi"))
			crop["yields"]["paper"] = {{"capacity", 5},
									   {"initial", 2},
									   {"growthRate", ResourceRateScale},
									   {"consumption", "one"}};
		map.installResourceDefinitions(
			nlohmann::json{{"schemaVersion", 1}, {"resources", {crop}}}.dump());
		const auto id = *map.resourceRegistry().find("benchmark-crop");
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
			{
				map.replaceResource(x, y, Resource{});
				map.setVertexTerrain(x, y, GRASS);
				map.setResourcesGrow(x, y, scenario != std::string("blocked") || !((x | y) & 1));
			}
		const int stride = scenario == std::string("sparse") ? 8 : 2;
		for (int y = 0; y < map.getH(); y += stride)
			for (int x = 0; x < map.getW(); x += stride)
			{
				map.setResource(x, y, id, 0);
				const auto at = map.coordToIndex(x, y);
				if (map.getResource(at).type == NO_RES_TYPE)
					continue;
				if (scenario == std::string("saturated"))
					map.setMaterialAmount(at, MaterialId::Food, 5);
				if (scenario == std::string("harvested"))
					map.setMaterialAmount(at, MaterialId::Food, 1);
			}
		game.gameHeader.setResourceGrowthDisabled(scenario == std::string("disabled"));
		const auto destination = std::filesystem::path(output) / scenario;
		std::filesystem::create_directories(destination);
		GAGCore::BinaryOutputStream stream(
			GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(
				(destination / "initial.game").string()));
		REQUIRE(stream.isValid());
		engine.gui.save(&stream, std::string("Growth ") + scenario);
		stream.flush();
	}
}
