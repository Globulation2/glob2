// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapReport.h"
#include "MapImage.h"
#include <string>
#include <cstdio>
#include <exception>
#include "GenerationRequest.h"
#include "GenerationResult.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

GlobalContainer *globalContainer = nullptr;

std::string serialize(Game &game)
{
	auto *memory = new GAGCore::MemoryStreamBackend();
	GAGCore::BinaryOutputStream stream(memory);
	game.save(&stream, false, "A \"quoted\" map\n\xc3\xa9");
	stream.flush();
	memory->seekFromEnd(0);
	return std::string(memory->getBuffer(), memory->getPosition());
}
void require(bool ok, const char *message)
{
	if (!ok)
		throw std::runtime_error(message);
}
void emit(Game &game, const std::filesystem::path &path)
{
	game.mapHeader.setMapName("A \"quoted\" map\n\xc3\xa9");
	// Deliberately non-derived cache values must survive analysis unchanged.
	for (size_t p = 0; p < game.map.getTiles().size(); ++p)
		game.map.setFertility(p % game.map.getW(), p / game.map.getW(), p % 50000);
	game.map.fertilityMaximum = 54321;
	// First save establishes the map offset used by the serializer's content hash.
	serialize(game);
	const auto before = serialize(game);
	const auto rng = syncRandEngine();
	const auto json = describeMap(game);
	require(syncRandEngine() == rng, "Report consumed simulation RNG");
	require(game.map.fertilityMaximum == 54321, "Report changed fertility maximum");
	for (size_t p = 0; p < game.map.getTiles().size(); ++p)
		require(game.map.getTile(p).fertility == p % 50000, "Report changed stored fertility");
	require(serialize(game) == before, "Report changed serialized game state");
	std::ofstream out(path);
	out << json;
	require(bool(out), "Cannot write report fixture");
}
void teams(Game &game, int x0, int x1)
{
	for (int i = 0; i < 2; ++i)
	{
		game.addTeam();
		game.teams[i]->startPosSet = 1;
		game.teams[i]->startPosX = i ? x1 : x0;
		game.teams[i]->startPosY = 1;
	}
	require(game.addUnit(x0, 1, 0, WORKER, 0, 0, 0, 0) != nullptr, "First worker");
	require(game.addUnit(x1, 1, 1, WORKER, 0, 0, 0, 0) != nullptr, "Second worker");
}
int main(int argc, char **argv)
{
	try
	{
		require(argc == 2, "Pass output directory");
		std::filesystem::create_directories(argv[1]);
		GlobalContainer globals;
		globalContainer = &globals;
		globals.runNoX = true;
		globals.settings.rememberUnit = false;
		globals.buildingsTypes.init();
		IntBuildingType::init();
		Race::loadDefault();
		{
			Game game(nullptr);
			game.map.setSize(6, 6, GRASS);
			game.map.setGame(&game);
			teams(game, 0, 63);
			game.map.replaceResource(5, 5, {WOOD, 0, 3, 0});
			game.map.replaceResource(7, 5, {WHEAT, 0, 7, 0});
			emit(game, std::filesystem::path(argv[1]) / "grass.json");
			game.map.setCellTerrain(20, 20, ICE);
			game.map.setCellTerrain(21, 20, TRAIL);
			game.map.setCellTerrain(22, 20, GRASS_SAND_SHORE);
			game.map.setCellTerrain(23, 20, SAND_WATER_SHORE);
			emit(game, std::filesystem::path(argv[1]) / "materials.json");
		}
		{
			Game game(nullptr);
			game.map.setSize(6, 6, GRASS);
			game.map.setGame(&game);
			teams(game, 0, 63);
			auto catalog = nlohmann::json::parse(game.map.resourceRegistry().serialize());
			auto mixed = catalog["resources"][0];
			mixed["key"] = "wood"; // Deliberately collides with the legacy trees alias.
			mixed["properties"]["primaryMaterial"] = "food";
			mixed["yields"]["food"] = mixed["yields"]["wood"];
			mixed["yields"]["food"]["initial"] = 2;
			mixed["yields"]["wood"]["initial"] = 3;
			catalog["resources"] = nlohmann::json::array({mixed});
			game.map.installResourceDefinitions(catalog.dump());
			const auto id = *game.map.resourceRegistry().find("wood");
			game.map.replaceResource(5, 5, {static_cast<Uint16>(resourceIndex(id)), 0, 5, 0});
			game.map.setMaterialAmount(5 + 5 * game.map.getW(), MaterialId::Food, 2);
			game.map.setMaterialAmount(5 + 5 * game.map.getW(), MaterialId::Wood, 3);
			emit(game, std::filesystem::path(argv[1]) / "compound.json");
		}
		{
			Game game(nullptr);
			game.map.setSize(6, 6, WATER);
			game.map.setGame(&game);
			for (int x : {1, 3})
			{
				game.map.setTerrain(x, 1, 0);
				game.map.setUMTerrain(x, 1, GRASS);
			}
			teams(game, 1, 3);
			emit(game, std::filesystem::path(argv[1]) / "islands.json");
			for (int y = 0; y < 3; ++y)
				for (int x = 0; x < 3; ++x)
					if (x != 1 || y != 1)
						game.map.replaceResource(x, y, {ALGA, 0, 1, 0});
			emit(game, std::filesystem::path(argv[1]) / "algae-ring.json");
		}
		{
			Game game(nullptr);
			game.map.setSize(6, 6, WATER);
			game.map.setGame(&game);
			emit(game, std::filesystem::path(argv[1]) / "empty.json");
		}
		{
			GenerationRequest request;
			request.setMethodDefaults(GeneratorRegistry::builtins().idOf("maze"));
			GenerationResult result;
			result.error = GenerationError::PlacementFailed;
			result.stage = "fixture";
			result.detail = "No plot\nwith \"room\"";
			result.telemetry.measure("fixture.count", 7, 0);
			result.telemetry.measure("fixture.share", 0.5);
			result.telemetry.measure("fixture.open", false);
			result.telemetry.choice("fixture.variant", "A \"quoted\" choice\n");
			result.telemetry.fallback("fixture.plot", "omitted", 0);
			std::ofstream out(std::filesystem::path(argv[1]) / "failure.json");
			out << describeGenerationFailure(request, result);
			require(bool(out), "Cannot write failure fixture");
		}
		{
			Game source(nullptr);
			source.map.setSize(6, 6, GRASS);
			source.map.setGame(&source);
			source.addTeam();
			source.teams[0]->startPosSet = 1;
			source.teams[0]->startPosX = source.teams[0]->startPosY = 32;
			for (int y = 0; y < 64; ++y)
				for (int x = 0; x < 64; ++x)
					if ((x < 12 && y >= 8 && y < 16) || (x >= 52 && y >= 11 && y < 19))
						source.map.replaceResource(x, y, {WOOD, 0, 3, 0});
			const auto path = std::filesystem::path(argv[1]) / "configured-seams.png";
			exportMapImage(source, path.string());
			for (bool spreading : {true, false})
			{
				Game imported(nullptr);
				auto catalog = nlohmann::json::parse(imported.map.resourceRegistry().serialize());
				if (!spreading)
				{
					catalog["resources"][0]["properties"]["spreadRate"] = 0;
					imported.map.installResourceDefinitions(catalog.dump());
				}
				GenerationRequest request;
				request.wDec = request.hDec = 6;
				MapImageImportReport report;
				importMapImage(imported, path.string(), request, 1, report, 8);
				require(!report.resourceSeamFallback, "Configured seam repair unexpectedly rolled back");
				require(spreading ? report.seamResourceChanges > 0 : report.seamResourceChanges == 0,
					"Image seam eligibility ignored configured spreading");
			}
		}
		for (int variant = 0; variant < 4; ++variant)
		{
			GenerationRequest request;
			request.setMethodDefaults(GeneratorRegistry::builtins().idOf("maze"));
			if (variant == 0)
				request.method = 100000;
			if (variant == 1)
				request.options.clear();
			if (variant == 2)
				request.wDec = -100;
			if (variant == 3)
				request.options["unknown-option"] = 7;
			Game game(nullptr);
			const auto result = GenerationService().generate(game, request, true);
			require(result.error == GenerationError::InvalidRequest, "Malformed request accepted");
			std::ofstream out(std::filesystem::path(argv[1]) /
							  ("invalid-" + std::to_string(variant) + ".json"));
			out << describeGenerationFailure(request, result);
			require(bool(out), "Cannot report malformed request");
		}
		std::puts("PASS report fixtures: known routes, resource barriers, unchanged serialization "
				  "and RNG");
		return 0;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
