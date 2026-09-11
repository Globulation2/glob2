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
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <TextStream.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <queue>
#include <string>
#include <vector>

GlobalContainer *globalContainer = nullptr;

namespace
{
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
std::uint64_t fnv(std::uint64_t hash, std::uint64_t value)
{
	hash ^= value;
	return hash * 1099511628211ULL;
}
std::uint64_t fnvBytes(const std::string &bytes)
{
	std::uint64_t hash = kFnvOffset;
	for (unsigned char c : bytes)
		hash = fnv(hash, c);
	return hash;
}

// A playable map file's bytes, saved the way CustomGameScreen::generateMap() saves the lobby's
// generated map.
std::string saveMapBytes(Game &game, const std::string &name)
{
	auto *backend = new GAGCore::MemoryStreamBackend();
	GAGCore::BinaryOutputStream stream(backend); // owns the backend
	game.save(&stream, true, name);
	stream.flush();
	backend->seekFromEnd(0);
	return std::string(backend->getBuffer(), backend->getPosition());
}

// The same save as named text fields, for diffing two saves of one map. A text stream cannot
// seek, so the header Game::save rewrites once the map offset is known is appended a second time.
std::string saveMapText(Game &game, const std::string &name)
{
	auto *backend = new GAGCore::MemoryStreamBackend();
	GAGCore::TextOutputStream stream(backend); // owns the backend
	game.save(&stream, true, name);
	stream.flush();
	backend->seekFromEnd(0);
	return std::string(backend->getBuffer(), backend->getPosition());
}

bool loadMapBytes(Game &game, const std::string &bytes)
{
	// The backend's constructor copies the bytes in by writing them, which leaves it at the end.
	auto *backend = new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size());
	backend->seekFromStart(0);
	GAGCore::BinaryInputStream stream(backend); // owns the backend
	return game.load(&stream);
}

// Everything on the map that belongs to no colony.
std::uint64_t worldHash(const Game &game)
{
	std::uint64_t hash = kFnvOffset;
	for (const auto &tile : game.map.tiles)
	{
		hash = fnv(hash, tile.terrain);
		hash = fnv(hash, tile.resource.getUint32());
		hash = fnv(hash, tile.fertility);
		hash = fnv(hash, tile.canResourcesGrow);
	}
	return hash;
}

// One colony described without its team number: its start and every object it owns, with that
// object's state. Equal signatures mean the same colony, whichever team it plays as.
std::uint64_t colonySignature(const Game &game, int team)
{
	const Team *colony = game.teams[team];
	std::uint64_t hash = kFnvOffset;
	for (std::int64_t v : {std::int64_t(colony->startPosX), std::int64_t(colony->startPosY),
						   std::int64_t(colony->startPosSet)})
		hash = fnv(hash, std::uint64_t(v));
	for (int i = 0; i < Unit::MAX_COUNT; ++i)
		if (const Unit *u = colony->myUnits[i])
			for (std::int64_t v : {std::int64_t(i), std::int64_t(Unit::GIDtoID(u->gid)),
								   std::int64_t(u->typeNum), std::int64_t(u->posX),
								   std::int64_t(u->posY), std::int64_t(u->hp),
								   std::int64_t(u->hungry), std::int64_t(u->direction)})
				hash = fnv(hash, std::uint64_t(v));
	for (int i = 0; i < Building::MAX_COUNT; ++i)
		if (const Building *b = colony->myBuildings[i])
			for (std::int64_t v : {std::int64_t(i), std::int64_t(Building::GIDtoID(b->gid)),
								   std::int64_t(b->typeNum), std::int64_t(b->posX),
								   std::int64_t(b->posY), std::int64_t(b->hp)})
				hash = fnv(hash, std::uint64_t(v));
	for (unsigned r = 0; r < MAX_NB_RESOURCES; ++r)
		hash = fnv(hash, colony->teamResources[r]);
	return hash;
}

// Every unit or building a tile names must be the one that team keeps under that id, and every
// team must answer to its own index.
bool mapReferencesConsistent(const Game &game)
{
	const auto &map = game.map;
	const int teams = game.mapHeader.getNumberOfTeams();
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
		{
			for (Uint16 gid : {map.getGroundUnit(x, y), map.getAirUnit(x, y)})
			{
				if (gid == NOGUID)
					continue;
				const int t = Unit::GIDtoTeam(gid);
				const Unit *u = t < teams ? game.teams[t]->myUnits[Unit::GIDtoID(gid)] : nullptr;
				if (!u || u->gid != gid || u->posX != x || u->posY != y)
					return false;
			}
			const Uint16 gbid = map.getBuilding(x, y);
			if (gbid == NOGBID)
				continue;
			const int t = Building::GIDtoTeam(gbid);
			const Building *b =
				t < teams ? game.teams[t]->myBuildings[Building::GIDtoID(gbid)] : nullptr;
			if (!b || b->gid != gbid)
				return false;
		}
	for (int t = 0; t < teams; ++t)
		if (game.teams[t]->teamNumber != t || game.teams[t]->me != Team::teamNumberToMask(t))
			return false;
	return true;
}

// Relabels colonies so the colony that was team t becomes team (t + shift) mod N, without
// touching terrain or any colony object's state: only the team number each one answers to
// changes. That lets a fairness tournament rotate colony-to-start assignments on one generated
// map, separating the start position from the team index the engine processes it under.
//
// A team slot keeps what belongs to the seat rather than the colony (colour, controller
// bookkeeping), so a rotated map looks like any other. This only runs on a map freshly loaded at
// tick 0 that is saved straight afterwards; runtime caches a map file does not carry (gradients,
// explored areas, clearing claims) are never simulated from and are left alone.
void shiftTeams(Game &game, int shift)
{
	const int n = game.mapHeader.getNumberOfTeams();
	assert(n > 0 && n < 32 && game.stepCounter == 0 && game.gameHeader.getNumberOfPlayers() == 0);
	shift = ((shift % n) + n) % n;
	auto to = [n, shift](int t) { return (t + shift) % n; };
	const Uint32 low = (Uint32(1) << n) - 1;
	auto mask = [&](Uint32 bits)
	{
		Uint32 out = bits & ~low;
		for (int t = 0; t < n; ++t)
			if (bits & (Uint32(1) << t))
				out |= Uint32(1) << to(t);
		return out;
	};
	auto unitGid = [&](Uint16 gid)
	{
		return gid == NOGUID ? gid
							 : Uint16(Unit::GIDfrom(Unit::GIDtoID(gid), to(Unit::GIDtoTeam(gid))));
	};
	auto buildingGid = [&](Uint16 gid)
	{
		return gid == NOGBID
				   ? gid
				   : Uint16(Building::GIDfrom(Building::GIDtoID(gid), to(Building::GIDtoTeam(gid))));
	};
	struct Seat
	{
		BaseTeam::TeamType type;
		Sint32 numberOfPlayer;
		GAGCore::Color color;
		Uint32 playersMask;
	};
	std::vector<Seat> seats;
	const std::vector<Team *> colonies(game.teams, game.teams + n);
	for (const Team *team : colonies)
		seats.push_back({team->type, team->numberOfPlayer, team->color, team->playersMask});
	for (int t = 0; t < n; ++t)
		game.teams[to(t)] = colonies[t];
	for (int t = 0; t < n; ++t)
	{
		Team &team = *game.teams[t];
		team.teamNumber = t;
		team.type = seats[t].type;
		team.numberOfPlayer = seats[t].numberOfPlayer;
		team.color = seats[t].color;
		team.playersMask = seats[t].playersMask;
		for (Uint32 *bits : {&team.me, &team.allies, &team.enemies, &team.sharedVisionExchange,
							 &team.sharedVisionFood, &team.sharedVisionOther})
			*bits = mask(*bits);
		for (int i = 0; i < Unit::MAX_COUNT; ++i)
			if (Unit *unit = team.myUnits[i])
				unit->gid = unitGid(unit->gid);
		for (int i = 0; i < Building::MAX_COUNT; ++i)
			if (Building *building = team.myBuildings[i])
			{
				building->gid = buildingGid(building->gid);
				building->seenByMask = mask(building->seenByMask);
			}
	}
	for (auto &tile : game.map.tiles)
	{
		tile.building = buildingGid(tile.building);
		tile.groundUnit = unitGid(tile.groundUnit);
		tile.airUnit = unitGid(tile.airUnit);
		tile.forbidden = mask(tile.forbidden);
		tile.guardArea = mask(tile.guardArea);
		tile.clearArea = mask(tile.clearArea);
	}
	for (auto &bits : game.map.mapDiscovered)
		bits = mask(bits);
}

// Writes <prefix>-r<k>.map for k = 0..rotations-1, where colony t (the generator's team t, which
// the START and COLONY lines describe) plays as team (t + k) mod N. Each rotation is checked from
// its saved bytes, and going all the way round must reproduce rotation 0 byte for byte.
int saveRotatedMaps(Game &game, const GenerationResult &result, const GenerationRequest &request,
					const std::string &prefix, const std::string &name, int rotations)
{
	if (!result)
		return 4;
	const int n = game.mapHeader.getNumberOfTeams();
	if (n < 1 || n >= 32 || rotations < 1 || rotations > n)
		return 2;
	std::printf("REQUEST,%s,%u,%u,%d,%d,%d,%d\n", result.generatorId.c_str(), result.revision,
				result.seed, request.wDec, request.hDec, request.nbTeams, request.nbWorkers);
	for (const auto &option : request.options)
		std::printf("OPTION,%s,%d\n", option.first.c_str(), option.second);
	for (int t = 0; t < n; ++t)
		std::printf("START,%d,%d,%d\n", t, game.teams[t]->startPosX, game.teams[t]->startPosY);

	// No players: a headless driver assigns every colony's controller when it launches the map.
	GameHeader header;
	header.setRandomSeed(result.seed);
	game.setGameHeader(header);
	auto label = [&](int k) { return name + "-r" + std::to_string(k); };
	const std::string generated = saveMapBytes(game, label(0));

	Game canonical(nullptr);
	if (!loadMapBytes(canonical, generated))
		return 5;
	const std::string first = saveMapBytes(canonical, label(0));
	// A freshly generated Game and the same map read back from its file save identically except
	// for the header's content SHA1: a fresh Game still holds map offset 0 when Game::save hashes
	// its first header write, before the real offset is patched in. Games only ever start from
	// the file, so rotations work from the reloaded form, which must re-save byte for byte.
	const bool stable = first == generated;
	bool idempotent = false;
	{
		Game again(nullptr);
		idempotent = loadMapBytes(again, first) && saveMapBytes(again, label(0)) == first;
	}
	// GLOB2_STUDY_EXPLAIN=<prefix> writes both saves as named fields, to see where they differ.
	if (const char *explain = std::getenv("GLOB2_STUDY_EXPLAIN"))
	{
		std::ofstream(std::string(explain) + "-generated.map", std::ios::binary) << generated;
		std::ofstream(std::string(explain) + "-reloaded.map", std::ios::binary) << first;
		std::ofstream(std::string(explain) + "-generated.txt") << saveMapText(game, label(0));
		std::ofstream(std::string(explain) + "-reloaded.txt") << saveMapText(canonical, label(0));
	}
	const std::uint64_t world = worldHash(canonical);
	std::vector<std::uint64_t> colonies;
	for (int t = 0; t < n; ++t)
		colonies.push_back(colonySignature(canonical, t));
	bool consistent = mapReferencesConsistent(canonical), placed = true, roundTrip = false;

	auto write = [&](int k, const std::string &bytes)
	{
		const std::string path = prefix + "-r" + std::to_string(k) + ".map";
		std::ofstream out(path, std::ios::binary);
		out.write(bytes.data(), std::streamsize(bytes.size()));
		if (!out)
			return false;
		std::printf("MAPFILE,%d,%s,%zu,%llu\n", k, path.c_str(), bytes.size(),
					(unsigned long long)fnvBytes(bytes));
		return true;
	};
	if (!write(0, first))
		return 3;
	std::string previous = first;
	for (int k = 1; k <= n; ++k)
	{
		Game shifted(nullptr);
		if (!loadMapBytes(shifted, previous))
			return 5;
		shiftTeams(shifted, 1);
		const std::string bytes = saveMapBytes(shifted, label(k % n));
		Game check(nullptr);
		if (!loadMapBytes(check, bytes))
			return 5;
		consistent = consistent && mapReferencesConsistent(check);
		placed = placed && worldHash(check) == world;
		for (int t = 0; t < n; ++t)
			placed = placed && colonySignature(check, (t + k) % n) == colonies[t];
		if (k == n)
			roundTrip = bytes == first;
		else if (k < rotations && !write(k, bytes))
			return 3;
		previous = bytes;
	}
	std::printf("ROTATIONS,%d,%d,%d,%d,%d,%d,%d\n", n, rotations, int(stable), int(consistent),
				int(placed), int(roundTrip), int(idempotent));
	return consistent && placed && roundTrip && idempotent ? 0 : 6;
}
} // namespace
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
				std::printf("%s{\"id\":\"%s\",\"label\":\"%s\",\"kind\":\"%s\",\"min\":%d,\"max\":%d,"
							"\"step\":%d,\"default\":%d,"
							"\"group\":%d,\"powerOfTwo\":%s,\"values\":[",
							i ? "," : "", c.id.c_str(), c.label, c.isToggle() ? "toggle" : "range",
							c.minimum, c.maximum, c.step, c.defaultValue, int(c.group),
							c.powerOfTwo ? "true" : "false");
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
	std::string dump, savePrefix, mapName;
	int candidates = 0, rotations = 1;
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
			if (id == "dump" || id == "save" || id == "name")
			{
				(id == "dump" ? dump : id == "save" ? savePrefix : mapName) = arg.substr(eq + 1);
				continue;
			}
			if (id == "candidates" || id == "rotations")
			{
				(id == "candidates" ? candidates : rotations) = std::stoi(arg.substr(eq + 1));
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
	if (candidates > 0)
	{
		// The lobby's roll: the best-scoring of `candidates` seeds derived from the root seed
		// (CustomGameScreen::generateMap), so studied maps are the maps players actually get.
		descriptor.seed = GenerationService().bestSeed(descriptor, seed, candidates);
		std::printf("SAMPLED,%u,%u,%d\n", seed, descriptor.seed, candidates);
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
	if (!savePrefix.empty())
		return saveRotatedMaps(game, result, descriptor, savePrefix,
							   mapName.empty() ? "study-" + std::to_string(method) + "-" +
													 std::to_string(seed)
											   : mapName,
							   rotations);
	return 0;
}
