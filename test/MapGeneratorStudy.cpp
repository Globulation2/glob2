// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated analysis executable; invokes the production generators.
#define SDL_MAIN_HANDLED
#include "Game.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "MapGenerator.h"
#include "Race.h"
#include "Unit.h"
#include "StartQuality.h"
#include "Utilities.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <queue>
#include <string>
#include <vector>

GlobalContainer *globalContainer = nullptr;
int main(int argc, char **argv)
{
	if (argc == 2 && std::string(argv[1]) == "--catalog")
	{
		using D = GenerationRequest;
		std::puts("[");
		const auto methods = GeneratorRegistry::builtins().methods();
		for (int m : methods)
		{
			const int method = m;
			std::printf("{\"method\":%d,\"id\":\"%s\",\"revision\":%u,\"editorOnly\":%s,"
						"\"nameKey\":\"%s\",\"controls\":[",
						m, GeneratorRegistry::builtins().at(m).id,
						GeneratorRegistry::builtins().at(m).revision,
						GeneratorRegistry::builtins().at(m).editorOnly ? "true" : "false",
						D::methodName(method));
			auto controls = D::sharedControls();
			const auto &specific = D::controls(method);
			controls.insert(controls.end(), specific.begin(), specific.end());
			for (size_t i = 0; i < controls.size(); ++i)
			{
				const auto &c = controls[i];
				std::printf("%s{\"id\":\"%s\",\"label\":\"%s\",\"min\":%d,\"max\":%d,\"step\":%d,"
							"\"default\":%d,"
							"\"group\":%d,\"powerOfTwo\":%s,\"values\":[",
							i ? "," : "", c.id.c_str(), c.label, c.minimum, c.maximum, c.step,
							c.defaultValue, int(c.group), c.powerOfTwo ? "true" : "false");
				const auto domain = c.values();
				for (size_t j = 0; j < domain.size(); ++j)
					std::printf("%s%d", j ? "," : "", domain[j]);
				std::printf("]}");
			}
			std::printf("]}%s\n", m == methods.back() ? "" : ",");
		}
		std::puts("]");
		return 0;
	}
	if (argc < 4)
		return 2; // method, seed, disposable profile, [displayed]
	const int method = std::atoi(argv[1]);
	const unsigned seed = std::strtoul(argv[2], nullptr, 10);

	SDL_SetMainReady();
	GlobalContainer globals(argv[3]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	Game game(nullptr);
	GenerationRequest descriptor;
	if (!GeneratorRegistry::builtins().find(method))
		return 2;
	descriptor.setMethodDefaults(method);
	descriptor.seed = seed;
	bool tuning = false;
	bool headroom = false;
	bool quality = false;
	std::string dump;
	std::map<std::string, std::string> aliases = {{"smooth", "smoothing"},
												  {"craters", "lake-density"},
												  {"extra", "extra-islands"},
												  {"island", "island-size"},
												  {"beach", "beach-size"},
												  {"w", "width"},
												  {"h", "height"}};
	for (const auto &c : GenerationRequest::controls(method))
		if (c.id == "lake-size" || c.id == "channel-width" || c.id == "bridge-width" ||
			c.id == "river-width")
			aliases["river"] = c.id;
	for (int i = 4; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "tuning")
			tuning = true;
		else if (arg == "headroom")
			headroom = true;
		else if (arg == "quality")
			quality = true;
		else if (arg == "preset")
		{
		} // All new requests start at registered defaults.
		else
		{
			auto eq = arg.find('=');
			if (eq == std::string::npos)
				return 2;
			std::string id = arg.substr(0, eq);
			if (id == "dump")
			{
				dump = arg.substr(eq + 1);
				continue;
			}
			if (aliases.count(id))
				id = aliases[id];
			int value = std::stoi(arg.substr(eq + 1));
			if (id == "width")
				descriptor.wDec = value;
			else if (id == "height")
				descriptor.hDec = value;
			else if (id == "teams")
				descriptor.nbTeams = value;
			else if (id == "workers")
				descriptor.nbWorkers = value;
			else
				descriptor.options[id] = value; // Service validates; never silently clamp studies.
		}
	}
	const auto start = std::chrono::steady_clock::now();
	const auto result = GenerationService().generate(game, descriptor);
	const bool success = bool(result);
	if (!success)
		std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
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
			bestWheat = 100000, bestWood = 100000, viableTeams = 0;
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
				// minWheat/minWood keep the worst (largest) distance across teams, i.e. the
				// least-served team; bestWheat/bestWood keep the smallest, i.e. the
				// best-served team. worst-minus-best is the per-map fairness spread between
				// colonies that a single aggregate (like the worst case alone) can't show: a
				// map can have a great worst case and still hand one team everything while
				// another gets comparatively little.
				if (t == 0)
				{
					minWheat = bestWheat = wheat;
					minWood = bestWood = wood;
				}
				else
				{
					minWheat = std::max(minWheat, wheat);
					minWood = std::max(minWood, wood);
					bestWheat = std::min(bestWheat, wheat);
					bestWood = std::min(bestWood, wood);
				}
				if (local >= 16 && wheat <= 24 && wood <= 32)
					++viableTeams;
			}
		std::printf("TUNE,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", minLocal, minWheat, minWood, viableTeams,
					resources[CORN], resources[WOOD], resources[STONE], resources[ALGA], bestWheat,
					bestWood);
	}
	if (headroom)
	{
		// How much of the fairness gap is recoverable by *placement alone*, on this exact
		// finished map? Nothing here changes the map; it asks what the colony sites could have
		// been. One multi-source flood per resource gives every tile its walking distance to
		// the nearest deposit, so scoring a candidate site is a lookup rather than its own
		// search, and the whole map's candidates cost two floods total.
		const int mw = map.getW(), mh = map.getH();
		auto distanceField = [&](int resourceType)
		{
			std::vector<int> d(size_t(mw) * mh, -1);
			std::queue<int> q;
			// A worker stands beside a deposit rather than on it, so the walkable tiles
			// touching one are the sources at distance 0.
			for (int y = 0; y < mh; ++y)
				for (int x = 0; x < mw; ++x)
				{
					if (map.getResource(x, y).type != resourceType)
						continue;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
							int np = ny * mw + nx;
							if (d[np] < 0 && map.isHardSpaceForGroundUnit(nx, ny, false, 0))
							{
								d[np] = 0;
								q.push(np);
							}
						}
				}
			while (!q.empty())
			{
				int p = q.front();
				q.pop();
				int x = p % mw, y = p / mw;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (dx == 0 && dy == 0)
							continue;
						int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
						int np = ny * mw + nx;
						if (d[np] < 0 && map.isHardSpaceForGroundUnit(nx, ny, false, 0))
						{
							d[np] = d[p] + 1;
							q.push(np);
						}
					}
			}
			return d;
		};
		const std::vector<int> woodField = distanceField(WOOD), wheatField = distanceField(CORN);
		// A site's score is its binding constraint: the further of its two primary resources.
		auto siteScore = [&](int x, int y) -> int
		{
			int p = map.normalizeY(y) * mw + map.normalizeX(x);
			int a = woodField[p], b = wheatField[p];
			if (a < 0 || b < 0)
				return -1;
			return std::max(a, b);
		};

		// What the generator's own placement actually achieved, by this same measure.
		int actualLo = 1 << 28, actualHi = -1;
		bool actualValid = success && method != 0;
		for (int t = 0; actualValid && t < game.teamsCount(); ++t)
		{
			int s = -1;
			for (int y = 0; y < mh && s < 0; ++y)
				for (int x = 0; x < mw; ++x)
				{
					auto gid = map.getGroundUnit(x, y);
					if (gid != NOGUID && Unit::GIDtoTeam(gid) == t)
					{
						s = siteScore(x, y);
						break;
					}
				}
			if (s < 0)
				actualValid = false;
			else
			{
				actualLo = std::min(actualLo, s);
				actualHi = std::max(actualHi, s);
			}
		}

		// Every site a colony could legally have occupied, scored and sorted.
		std::vector<std::pair<int, int>> candidates; // (score, tile index)
		for (int y = 0; y < mh; ++y)
			for (int x = 0; x < mw; ++x)
			{
				if (!map.isFreeForBuilding(x, y, 4, 4))
					continue;
				int s = siteScore(x, y);
				if (s >= 0)
					candidates.push_back({s, y * mw + x});
			}
		std::sort(candidates.begin(), candidates.end());
		// Keep the search bounded on large maps; a stride keeps the sample spread over the
		// whole score range rather than clustering at one end.
		const size_t cap = 700;
		if (candidates.size() > cap)
		{
			std::vector<std::pair<int, int>> thinned;
			for (size_t i = 0; i < cap; ++i)
				thinned.push_back(candidates[i * candidates.size() / cap]);
			candidates.swap(thinned);
		}

		// Narrowest score window that still holds nbTeams mutually distant sites. Greedy
		// selection inside a window understates what a full search would find, so this is a
		// conservative floor on the achievable spread, not an optimistic one.
		const int nbTeams = descriptor.nbTeams;
		const int minDistSquare = int((double)mw * mh / (double)nbTeams / 5);
		int bestSpread = -1, bestLo = -1, bestHi = -1;
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			std::vector<int> picked;
			for (size_t j = i; j < candidates.size(); ++j)
			{
				int px = candidates[j].second % mw, py = candidates[j].second / mw;
				bool farEnough = true;
				for (int q : picked)
					if (map.warpDistSquare(px, py, q % mw, q / mw) < minDistSquare)
					{
						farEnough = false;
						break;
					}
				if (!farEnough)
					continue;
				picked.push_back(candidates[j].second);
				if ((int)picked.size() == nbTeams)
				{
					int spread = candidates[j].first - candidates[i].first;
					if (bestSpread < 0 || spread < bestSpread)
					{
						bestSpread = spread;
						bestLo = candidates[i].first;
						bestHi = candidates[j].first;
					}
					break;
				}
			}
			if (bestSpread == 0)
				break;
		}
		std::printf("HEADROOM,%d,%d,%d,%d,%d,%d,%d,%d\n", (int)candidates.size(),
					actualValid ? actualHi - actualLo : -1, actualValid ? actualLo : -1,
					actualValid ? actualHi : -1, bestSpread, bestLo, bestHi, nbTeams);
	}
	if (quality)
	{
		const auto &q = result.quality;
		// The service already scored this map. Scoring it again times what that costs, which
		// is what a sampling caller pays per candidate on top of generating it.
		const auto scoreStart = std::chrono::steady_clock::now();
		MapGeneration::scoreStarts(game, descriptor.nbTeams);
		const double scoreSeconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - scoreStart).count();
		std::printf("QUALITY,%d,%.6f,%.6f,%.6f,%.6f,%d,%.6f\n", q.measured, q.score, q.fairness,
					q.worst, q.best, (int)q.colonies.size(), scoreSeconds);
		for (size_t t = 0; t < q.colonies.size(); ++t)
		{
			const auto &c = q.colonies[t];
			std::printf("COLONY,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
						(int)t, c.wheatDistance, c.woodDistance, c.catchmentTiles, c.buildSites,
						c.resourceAmount, c.rivalDistance, c.rivalsWithinThreat, c.meanFertility,
						c.wheat, c.wood, c.fertility, c.depth, c.room, c.isolation, c.total);
		}
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
				if (map.getResource(x, y).type >= CHERRY && map.getResource(x, y).type <= CHERRY + 2)
					c = 8;
				if (map.getResource(x, y).type == ALGA)
					c = 9;
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
