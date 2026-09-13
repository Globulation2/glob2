// SPDX-License-Identifier: GPL-3.0-or-later
// Golden maps and a colony-count sweep for every registered generator.
//
//   MapGeneratorGoldenTest <profile-dir>              check this platform's rows of the table
//   MapGeneratorGoldenTest <profile-dir> --update     regenerate this platform's rows
//   MapGeneratorGoldenTest <profile-dir> --print      print this platform's rows to stdout
//   MapGeneratorGoldenTest <profile-dir> --sweep      every playable landscape at the colony
//                                                    counts and sizes the lobby offers
//
// The table (test/map-generator-golden.txt) records, per platform, the fingerprint of the map
// each generator produces for a few seeds, sizes and colony counts, keyed by the generator's
// revision. A generator whose output changes without its revision moving fails the check; a
// revision bump makes --update accept the new maps. Generation is deterministic per platform,
// not across platforms, so rows carry the platform they were made on and a platform without
// rows is reported, not failed.
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "GlobalContainer.h"
#include "MapGeneratorFrameworkChecks.h"
#include <SDL.h>
#include <Toolkit.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

GlobalContainer *globalContainer = nullptr;

namespace
{
const char *const kTablePath = "test/map-generator-golden.txt";

std::string platformTag()
{
#if defined(_WIN32)
	const char *os = "windows";
#elif defined(__APPLE__)
	const char *os = "macos";
#elif defined(__linux__)
	const char *os = "linux";
#else
	const char *os = "other";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
	const char *arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
	const char *arch = "x86_64";
#else
	const char *arch = "unknown";
#endif
	return std::string(os) + "-" + arch;
}

struct Row
{
	std::string platform;
	int id = 0;
	unsigned revision = 0;
	int wDec = 0, hDec = 0, teams = 0;
	std::uint32_t seed = 0;
	std::string status; // ok, invalid or failed
	std::uint64_t hash = 0;
	std::string key() const
	{
		return platform + " " + std::to_string(id) + " " + std::to_string(wDec) + " " +
			   std::to_string(hDec) + " " + std::to_string(teams) + " " + std::to_string(seed);
	}
};

std::vector<Row> readTable(const std::string &path)
{
	std::vector<Row> rows;
	std::ifstream in(path);
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#')
			continue;
		std::istringstream fields(line);
		Row row;
		if (fields >> row.platform >> row.id >> row.revision >> row.wDec >> row.hDec >> row.teams >>
			row.seed >> row.status >> row.hash)
			rows.push_back(row);
		else
			throw std::runtime_error("Malformed golden row: " + line);
	}
	return rows;
}

void writeTable(const std::string &path, const std::vector<Row> &rows)
{
	std::ofstream out(path);
	out << "# Golden map fingerprints per generator: platform id revision wDec hDec teams seed"
		   " status hash\n"
		   "# Regenerate this platform's rows with MapGeneratorGoldenTest <profile> --update"
		   " after a revision bump.\n";
	for (const auto &r : rows)
		out << r.platform << ' ' << r.id << ' ' << r.revision << ' ' << r.wDec << ' ' << r.hDec
			<< ' ' << r.teams << ' ' << r.seed << ' ' << r.status << ' ' << r.hash << '\n';
}

// One roll, as the lobby and editor would ask for it.
Row roll(int id, int wDec, int hDec, int teams, std::uint32_t seed)
{
	Row row;
	row.platform = platformTag();
	row.id = id;
	row.revision = GeneratorRegistry::builtins().at(id).revision;
	row.wDec = wDec;
	row.hDec = hDec;
	row.teams = teams;
	row.seed = seed;
	GenerationRequest request;
	request.setMethodDefaults(id);
	request.wDec = wDec;
	request.hDec = hDec;
	request.nbTeams = teams;
	request.seed = seed;
	Game game(nullptr);
	const auto result = GenerationService().generate(game, request);
	const bool seated = GeneratorRegistry::builtins().at(id).hasStartingColonies
							? game.teamsCount() == teams
							: game.teamsCount() == 1;
	if (result && seated)
	{
		row.status = "ok";
		row.hash = mapFingerprint(game);
	}
	else
		row.status = result.error == GenerationError::InvalidRequest ? "invalid" : "failed";
	return row;
}

// The rows this platform keeps: every registered generator at its own defaults on the three
// lobby sizes and on a 512x256 rectangle, and at 256 with two and with eight colonies.
std::vector<Row> goldenRows()
{
	std::vector<Row> rows;
	for (int id : GeneratorRegistry::builtins().methods(true))
	{
		GenerationRequest defaults;
		defaults.setMethodDefaults(id);
		for (std::uint32_t seed = 1; seed <= 3; ++seed)
			rows.push_back(roll(id, 8, 8, defaults.nbTeams, seed));
		rows.push_back(roll(id, 7, 7, defaults.nbTeams, 1));
		rows.push_back(roll(id, 9, 9, defaults.nbTeams, 1));
		rows.push_back(roll(id, 9, 8, defaults.nbTeams, 1));
		rows.push_back(roll(id, 8, 8, 2, 1));
		rows.push_back(roll(id, 8, 8, 8, 1));
	}
	return rows;
}

int check(const std::string &path)
{
	const auto table = readTable(path);
	const auto platform = platformTag();
	std::map<std::string, Row> expected;
	std::map<int, unsigned> revisions;
	for (const auto &r : table)
	{
		revisions[r.id] = r.revision;
		if (r.platform == platform)
			expected[r.key()] = r;
	}
	int failures = 0;
	for (int id : GeneratorRegistry::builtins().methods(true))
	{
		const auto &definition = GeneratorRegistry::builtins().at(id);
		auto it = revisions.find(id);
		if (it == revisions.end())
		{
			std::printf("MISSING generator %s (%d): no golden rows; run --update\n", definition.id,
						id);
			++failures;
		}
		else if (it->second != definition.revision)
		{
			std::printf("STALE generator %s (%d): table revision %u, registered %u; run --update\n",
						definition.id, id, it->second, definition.revision);
			++failures;
		}
	}
	if (expected.empty())
	{
		std::printf("No golden rows for %s; nothing to compare. Run --update on this platform"
					" and commit its rows.\n",
					platform.c_str());
		return failures ? 1 : 0;
	}
	int compared = 0;
	for (const auto &[key, row] : expected)
	{
		if (row.revision != GeneratorRegistry::builtins().at(row.id).revision)
			continue; // already reported as stale
		const Row now = roll(row.id, row.wDec, row.hDec, row.teams, row.seed);
		++compared;
		if (now.status != row.status || now.hash != row.hash)
		{
			std::printf("CHANGED %s (%d) %dx%d %d colonies seed %u: was %s %llu, now %s %llu at"
						" the same revision %u\n",
						GeneratorRegistry::builtins().at(row.id).id, row.id, 1 << row.wDec,
						1 << row.hDec, row.teams, row.seed, row.status.c_str(),
						(unsigned long long)row.hash, now.status.c_str(),
						(unsigned long long)now.hash, row.revision);
			++failures;
		}
	}
	std::printf("%s: %d golden rows compared, %d failures\n", platform.c_str(), compared, failures);
	return failures ? 1 : 0;
}

int update(const std::string &path, bool toStdout, bool force)
{
	std::vector<Row> table;
	try
	{
		table = readTable(path);
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
	const auto platform = platformTag();
	std::map<std::string, Row> previous;
	for (const auto &r : table)
		if (r.platform == platform)
			previous[r.key()] = r;
	const auto fresh = goldenRows();
	int silent = 0;
	for (const auto &row : fresh)
	{
		auto it = previous.find(row.key());
		if (it == previous.end() || it->second.revision != row.revision)
			continue;
		if (it->second.status != row.status || it->second.hash != row.hash)
		{
			std::printf("%s (%d) %dx%d %d colonies seed %u changed at unchanged revision %u\n",
						GeneratorRegistry::builtins().at(row.id).id, row.id, 1 << row.wDec,
						1 << row.hDec, row.teams, row.seed, row.revision);
			++silent;
		}
	}
	if (silent && !force)
	{
		std::printf("%d rows changed without a revision bump. Bump the generator's revision, or"
					" pass --force if the change is intended to stay unversioned.\n",
					silent);
		return 1;
	}
	std::vector<Row> merged;
	for (const auto &r : table)
		if (r.platform != platform)
			merged.push_back(r);
	merged.insert(merged.end(), fresh.begin(), fresh.end());
	std::stable_sort(merged.begin(), merged.end(),
					 [](const Row &a, const Row &b)
					 {
						 if (a.platform != b.platform)
							 return a.platform < b.platform;
						 if (a.id != b.id)
							 return a.id < b.id;
						 return a.key() < b.key();
					 });
	if (toStdout)
	{
		for (const auto &r : fresh)
			std::cout << r.platform << ' ' << r.id << ' ' << r.revision << ' ' << r.wDec << ' '
					  << r.hDec << ' ' << r.teams << ' ' << r.seed << ' ' << r.status << ' '
					  << r.hash << '\n';
		return 0;
	}
	writeTable(path, merged);
	std::printf("Wrote %zu rows for %s to %s\n", fresh.size(), platform.c_str(), path.c_str());
	return 0;
}

// The lobby offers 64 to 512 and 1 to 12 colonies and rolls five seeds, keeping the best. At
// its 256 default every playable landscape must seat every colony count it accepts on at
// least one of five seeds; the small and large sizes are checked at the counts a player is
// likely to ask for. The per-cell rates are printed so a landscape that only just scrapes by
// is visible before it starts failing.
int sweep()
{
	struct Cell
	{
		int wDec, teams;
		std::vector<std::uint32_t> seeds;
	};
	const std::vector<std::uint32_t> five{1, 2, 3, 4, 5}, three{1, 2, 3};
	const std::vector<Cell> cells = {
		{7, 2, five}, {7, 4, five}, {8, 2, five},  {8, 3, five},  {8, 4, five},
		{8, 6, five}, {8, 8, five}, {8, 12, five}, {9, 4, three}, {9, 12, three},
	};
	int failures = 0;
	for (int id : GeneratorRegistry::builtins().methods(false))
	{
		const auto &definition = GeneratorRegistry::builtins().at(id);
		std::string line;
		for (const auto &cell : cells)
		{
			int ok = 0;
			bool invalid = false;
			for (auto seed : cell.seeds)
			{
				const Row row = roll(id, cell.wDec, cell.wDec, cell.teams, seed);
				if (row.status == "invalid")
					invalid = true;
				else if (row.status == "ok")
					++ok;
			}
			char cellText[48];
			if (invalid)
				std::snprintf(cellText, sizeof cellText, " %d/%d:-", 1 << cell.wDec, cell.teams);
			else
				std::snprintf(cellText, sizeof cellText, " %d/%d:%d/%zu", 1 << cell.wDec,
							  cell.teams, ok, cell.seeds.size());
			line += cellText;
			if (!invalid && ok == 0)
			{
				std::printf("FAIL %s (%d) at %dx%d with %d colonies: no seed of %zu generated\n",
							definition.id, id, 1 << cell.wDec, 1 << cell.wDec, cell.teams,
							cell.seeds.size());
				++failures;
			}
		}
		std::printf("%-20s%s\n", definition.id, line.c_str());
	}
	std::printf("sweep: %d failing combinations\n", failures);
	return failures ? 1 : 0;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: %s <profile-dir> [--update [--force]|--print|--sweep]\n",
					 argv[0]);
		return 2;
	}
	SDL_SetMainReady();
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.load();
	std::string mode = argc > 2 ? argv[2] : "";
	const bool force = argc > 3 && std::strcmp(argv[3], "--force") == 0;
	if (mode == "--sweep")
		return sweep();
	if (mode == "--update")
		return update(kTablePath, false, force);
	if (mode == "--print")
		return update(kTablePath, true, false);
	return check(kTablePath);
}
