// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapReport.h"
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
	for (size_t p = 0; p < game.map.tiles.size(); ++p)
		game.map.tiles[p].fertility = p % 50000;
	game.map.fertilityMaximum = 54321;
	// First save establishes the map offset used by the serializer's content hash.
	serialize(game);
	const auto before = serialize(game);
	const auto rng = syncRandEngine();
	const auto json = describeMap(game);
	require(syncRandEngine() == rng, "Report consumed simulation RNG");
	require(game.map.fertilityMaximum == 54321, "Report changed fertility maximum");
	for (size_t p = 0; p < game.map.tiles.size(); ++p)
		require(game.map.tiles[p].fertility == p % 50000, "Report changed stored fertility");
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
			game.map.getResource(5, 5) = {WOOD, 0, 3, 0};
			game.map.getResource(7, 5) = {WHEAT, 0, 7, 0};
			emit(game, std::filesystem::path(argv[1]) / "grass.json");
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
						game.map.getResource(x, y) = {ALGA, 0, 1, 0};
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
