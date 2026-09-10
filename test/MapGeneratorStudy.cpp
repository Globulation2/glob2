// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated analysis executable; invokes the production generators.
#define SDL_MAIN_HANDLED
#include "GlobalContainer.h"
#include "Game.h"
#include "MapGenerator.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "PerlinNoise.h"
#include "Utilities.h"
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <string>
#include <map>
#include <vector>
#include <queue>
#include <algorithm>
#include "Unit.h"

GlobalContainer *globalContainer = nullptr;
static time_t studyTime = 1700000000;
// Production generation reseeds from time() internally. Override time only in
// this dedicated executable so each sample has a reproducible synthetic clock.
extern "C" time_t time(time_t *out)
{
	if (out)
		*out = studyTime;
	return studyTime;
}

int main(int argc, char **argv)
{
	if (argc == 2 && std::string(argv[1]) == "--catalog")
	{
		using D = MapGenerationDescriptor;
		std::puts("[");
		for (int m = 0; m <= D::eOLDISLANDS; ++m)
		{
			auto method = static_cast<D::Method>(m);
			std::printf("{\"method\":%d,\"nameKey\":\"%s\",\"controls\":[", m,
						D::methodName(method));
			auto controls = D::sharedControls();
			const auto &specific = D::controls(method);
			controls.insert(controls.end(), specific.begin(), specific.end());
			for (size_t i = 0; i < controls.size(); ++i)
			{
				const auto &c = controls[i];
				std::printf("%s{\"label\":\"%s\",\"min\":%d,\"max\":%d,\"step\":%d,\"default\":%d,"
							"\"group\":%d,\"powerOfTwo\":%s}",
							i ? "," : "", c.label, c.minimum, c.maximum, c.step, c.defaultValue,
							int(c.group), c.powerOfTwo ? "true" : "false");
			}
			std::printf("]}%s\n", m == D::eOLDISLANDS ? "" : ",");
		}
		std::puts("]");
		return 0;
	}
	if (argc < 4)
		return 2; // method, seed, disposable profile, [displayed]
	const int method = std::atoi(argv[1]);
	const unsigned seed = std::strtoul(argv[2], nullptr, 10);
	studyTime += seed;
	SDL_SetMainReady();
	GlobalContainer globals(argv[3]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	Game game(nullptr);
	MapGenerationDescriptor descriptor;
	descriptor.method = static_cast<MapGenerationDescriptor::Method>(method);
	bool terrainOnly = false;
	bool tuning = false;
	std::string dump;
	std::map<std::string, Sint32 *> controls = {{"water", &descriptor.waterRatio},
												{"sand", &descriptor.sandRatio},
												{"grass", &descriptor.grassRatio},
												{"desert", &descriptor.desertRatio},
												{"smooth", &descriptor.smooth},
												{"river", &descriptor.riverDiameter},
												{"craters", &descriptor.craterDensity},
												{"extra", &descriptor.extraIslands},
												{"island", &descriptor.oldIslandSize},
												{"beach", &descriptor.oldBeach},
												{"fruit", &descriptor.fruitRatio},
												{"w", &descriptor.wDec},
												{"h", &descriptor.hDec},
												{"teams", &descriptor.nbTeams}};
	for (int i = 4; i < argc; ++i)
	{
		if (std::string(argv[i]) == "preset")
			descriptor.setMethodDefaults(static_cast<MapGenerationDescriptor::Method>(method));
		if (std::string(argv[i]) == "displayed")
		{
			descriptor.sandRatio = 0;
			descriptor.desertRatio = 0;
		}
		if (std::string(argv[i]) == "terrain-only")
			terrainOnly = true;
		if (std::string(argv[i]) == "tuning")
			tuning = true;
		std::string arg = argv[i];
		auto eq = arg.find('=');
		if (eq != std::string::npos)
		{
			auto key = arg.substr(0, eq);
			if (key == "dump")
				dump = arg.substr(eq + 1);
			else if (controls.count(key))
				*controls[key] = std::stoi(arg.substr(eq + 1));
			else
				return 2;
		}
	}
	PerlinNoise::reseed(seed);
	std::srand(seed);
	setSyncRandSeed(seed);
	const auto start = std::chrono::steady_clock::now();
	MapGenerator generator;
	bool success;
	if (terrainOnly && method == MapGenerationDescriptor::eOLDISLANDS)
	{
		game.map.setSize(descriptor.wDec, descriptor.hDec);
		game.map.setGame(&game);
		setSyncRandSeed(static_cast<Uint32>(studyTime));
		success = game.map.oldMakeIslandsMap(descriptor);
	}
	else
	{
		success = generator.generateMap(game, descriptor, static_cast<Uint32>(studyTime));
	}
	const double seconds =
		std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	auto &map = game.map;
	int grass = 0, sand = 0, water = 0, shore = 0, free = 0, fit4 = 0;
	int umGrass = 0, umSand = 0, umWater = 0;
	std::uint64_t hash = 14695981039346656037ULL;
	std::vector<int> footprint(map.getW() * map.getH(), 0);
	int resources[8] = {};
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
		{
			if (map.isGrass(x, y))
				++grass;
			else if (map.isSand(x, y))
				++sand;
			else if (map.isWater(x, y))
				++water;
			else
				++shore;
			if (map.isFreeForBuilding(x, y))
				++free;
			if (map.isFreeForBuilding(x, y, 4, 4))
			{
				++fit4;
				footprint[y * map.getW() + x] = 1;
			}
			if (map.getResource(x, y).type < 8)
				++resources[map.getResource(x, y).type];
			switch (map.getUMTerrain(x, y))
			{
			case GRASS:
				++umGrass;
				break;
			case SAND:
				++umSand;
				break;
			case WATER:
				++umWater;
				break;
			}
			hash ^= map.getTerrain(x, y);
			hash *= 1099511628211ULL;
			hash ^= map.getResource(x, y).getUint32();
			hash *= 1099511628211ULL;
		}
	std::printf("STUDY,%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%llu\n", method, seed, success,
				map.getW() * map.getH(), grass, sand, water, shore, free, fit4, umGrass, umSand,
				umWater, seconds, (unsigned long long)hash);
	if (tuning)
	{
		int minLocal = map.getW() * map.getH(), minWheat = 100000, minWood = 100000,
			viableTeams = 0;
		if (success && method != 0)
			for (int t = 0; t < game.teamsCount(); ++t)
			{
				std::vector<int> dist(map.getW() * map.getH(), -1);
				std::queue<int> q;
				// Seed the flood from actual starting workers, not a guessed base edge.
				for (int y = 0; y < map.getH(); ++y)
					for (int x = 0; x < map.getW(); ++x)
					{
						auto gid = map.getGroundUnit(x, y);
						if (gid != NOGUID && Unit::GIDtoTeam(gid) == t)
						{
							int p = y * map.getW() + x;
							dist[p] = 0;
							q.push(p);
						}
					}
				int local = 0, wheat = 100000, wood = 100000;
				while (!q.empty())
				{
					int p = q.front();
					q.pop();
					int x = p % map.getW(), y = p / map.getW();
					// Expansion space within 24 walking steps of a starting worker.
					if (dist[p] <= 24 && footprint[p])
						++local;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							if (dx == 0 && dy == 0)
								continue;
							int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy),
								np = ny * map.getW() + nx;
							auto type = map.getResource(nx, ny).type;
							if (type == CORN)
								wheat = std::min(wheat, dist[p] + 1);
							if (type == WOOD)
								wood = std::min(wood, dist[p] + 1);
							if (dist[np] < 0 && map.isHardSpaceForGroundUnit(nx, ny, false, 0))
							{
								dist[np] = dist[p] + 1;
								q.push(np);
							}
						}
				}
				minLocal = std::min(minLocal, local);
				// Store the worst (largest) resource distance across teams below.
				if (t == 0)
				{
					minWheat = wheat;
					minWood = wood;
				}
				else
				{
					minWheat = std::max(minWheat, wheat);
					minWood = std::max(minWood, wood);
				}
				if (local >= 16 && wheat <= 24 && wood <= 32)
					++viableTeams;
			}
		std::printf("TUNE,%d,%d,%d,%d,%d,%d,%d,%d\n", minLocal, minWheat, minWood, viableTeams,
					resources[CORN], resources[WOOD], resources[STONE], resources[ALGA]);
	}
	if (!dump.empty())
	{
		FILE *f = std::fopen(dump.c_str(), "w");
		if (!f)
			return 3;
		std::fprintf(f, "%d %d\n", map.getW(), map.getH());
		for (int y = 0; y < map.getH(); ++y)
		{
			for (int x = 0; x < map.getW(); ++x)
			{
				int c = map.isGrass(x, y) ? 0 : map.isSand(x, y) ? 1 : map.isWater(x, y) ? 2 : 3;
				if (map.getResource(x, y).type == CORN)
					c = 4;
				if (map.getResource(x, y).type == WOOD)
					c = 5;
				if (map.getResource(x, y).type == STONE)
					c = 6;
				if (map.getBuilding(x, y) != NOGBID)
					c = 7;
				std::fprintf(f, "%d ", c);
			}
			std::fprintf(f, "\n");
		}
		std::fclose(f);
	}
	return 0;
}
