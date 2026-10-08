// Local one-off: dump every bundled map's per-cell terrain rules for an
// old/new comparison. Not committed.
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "FileManager.h"
#include "MapHeader.h"
#include <cstdio>
#include <cstdlib>
#include <string>

TEST_CASE("dump bundled map cell rules [artifacts]" * doctest::skip(std::getenv("GLOB2_RULE_DUMP") == nullptr))
{
	glob2test::HeadlessGlobals globals;
	const std::string out = std::getenv("GLOB2_RULE_DUMP");
	const char *maps[] = {"A_big_pond", "Archipelago", "Centerfolds_2", "Dejans", "Easy_Three", "FourSquares1", "G2",
						  "Garden_3", "Holiday_Island_2", "Island_of_the_Renfur", "Isles", "Mazury", "Migration",
						  "Muka", "Oazis", "Playground", "Sand_River", "SmallForTwo", "Triangle", "Wild_River",
						  "balanced", "balanced_for_2", "strange2"};
	std::vector<std::string> paths;
	for (auto *m : maps) paths.push_back(std::string("maps/") + m + ".map");
	for (int i = 1; i <= 4; ++i) paths.push_back("campaigns/tutorial-part" + std::to_string(i) + ".map");
	for (const auto &path : paths)
	{
		GameGUI loaded;
		GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(*globalContainer->fileManager, path));
		if (!loaded.game.load(&input)) { MESSAGE("cannot load " << path); continue; }
		const auto &map = loaded.game.map;
		std::string name = path;
		for (auto &c : name) if (c == '/') c = '_';
		FILE *f = std::fopen((out + "/" + name + ".rules").c_str(), "wb");
		REQUIRE(f);
		std::fprintf(f, "%d %d\n", map.getW(), map.getH());
		for (int y = 0; y < map.getH(); ++y)
			for (int x = 0; x < map.getW(); ++x)
			{
				const auto &p = map.terrainPropertiesAt(x, y);
				std::fprintf(f, "%d%d%d%d%d%d%d%d%d %d %d %d %d %d %d %d %d %d %d\n", p.walkable, p.swimmable, p.flyable,
							 p.resourcesGrow, p.fertilitySource, p.nonGrowingResources, p.buildable, p.projectileBlocks,
							 p.shoreline, p.groundSpeedQ8, p.airSpeedQ8, p.groundHealthQ8, p.airHealthQ8, p.growthQ8,
							 p.fertilityQ8, p.inhibitionQ8, p.shoreSupportQ8, p.allowedResources, p.farmMaterial);
			}
		std::fclose(f);
	}
}
