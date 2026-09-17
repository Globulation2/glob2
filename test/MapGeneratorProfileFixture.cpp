// SPDX-License-Identifier: GPL-3.0-or-later
// A CPU-bound round-robin driver over every registered map generator, used as a load
// generator for external sampling profilers (macOS `sample`, Linux `perf record`) and as
// a coarse per-generator wall-clock comparison when validating optimizations.
//
//   MapGeneratorProfileFixture <profile-dir> <seed> <rounds> [generator-id...]
//
// Naming generator ids restricts the rounds to those generators, for profiling one of them.
//
// Each round asks every registered generator (including editor-only ones) for one map at
// parameters drawn from its own registered controls: shared width/height/teams/workers are
// drawn from their declared domains, then GenerationRequest::randomizeControls draws the
// generator's own options and keeps only combinations validateGenerationRequest accepts, so
// every attempted request is one the lobby could actually offer. A combination of shared
// controls that no draw of a generator's own options can satisfy is redrawn; a generator
// that never validates at some size (a small map too small for its minimum layout) simply
// contributes fewer attempts, which is fine for a load generator (unlike goldenRows(), this
// tool makes no accepted-cell coverage claim).
//
// This mirrors production use: GenerationService::generate() from a fresh Game, discarding
// the result. It is deliberately not wired into CI: it is a load generator for interactive
// profiling sessions, not a regression check (see MapGeneratorGoldenTest and its --sweep and
// --performance modes for that).
#define SDL_MAIN_HANDLED
#include "Game.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Race.h"
#include "Utilities.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>

GlobalContainer *globalContainer = nullptr;

namespace
{
struct GeneratorStats
{
	std::string id;
	std::uint64_t attempts = 0, generated = 0, failed = 0, totalNs = 0, maxNs = 0;
};

// Draws shared controls (width/height/teams/workers) at random, then delegates the
// generator's own options to GenerationRequest::randomizeControls; retries a bounded number
// of times when no draw of the generator's own options validates at the drawn shared size.
// Returns false, request unchanged, if nothing validated.
bool randomizeRequest(GenerationRequest &request, std::mt19937 &rng, int sharedAttempts)
{
	const auto &shared = GenerationRequest::sharedControls();
	for (int attempt = 0; attempt < sharedAttempts; ++attempt)
	{
		GenerationRequest draft = request;
		for (const auto &c : shared)
		{
			const std::vector<int> domain = c.values();
			c.set(draft, domain[rng() % domain.size()]);
		}
		draft.seed = rng();
		if (draft.randomizeControls(rng()))
		{
			request = draft;
			return true;
		}
	}
	return false;
}
} // namespace

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: %s <profile-dir> <seed> <rounds> [generator-id...]\n", argv[0]);
		return 2;
	}
	const unsigned baseSeed = std::strtoul(argv[2], nullptr, 10);
	const int rounds = std::atoi(argv[3]);
	SDL_SetMainReady();
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();

	std::vector<int> methods;
	for (int method : GeneratorRegistry::builtins().methods(true))
	{
		bool named = argc == 4;
		for (int i = 4; i < argc; ++i)
			named |= std::string(GeneratorRegistry::builtins().at(method).id) == argv[i];
		if (named)
			methods.push_back(method);
	}
	if (methods.empty())
	{
		std::fprintf(stderr, "no registered generator matches the ids given\n");
		return 2;
	}
	std::mt19937 rng(baseSeed);
	std::vector<GeneratorStats> stats;
	stats.reserve(methods.size());
	for (int method : methods)
		stats.push_back({GeneratorRegistry::builtins().at(method).id});

	const GenerationService service;
	const auto start = std::chrono::steady_clock::now();
	for (int round = 0; round < rounds; ++round)
		for (std::size_t i = 0; i < methods.size(); ++i)
		{
			GenerationRequest request;
			request.setMethodDefaults(methods[i]);
			auto &s = stats[i];
			if (!randomizeRequest(request, rng, 32))
				continue;
			++s.attempts;
			Game game(nullptr);
			const auto before = std::chrono::steady_clock::now();
			const auto result = service.generate(game, request, false);
			const auto after = std::chrono::steady_clock::now();
			const auto ns = std::uint64_t(
				std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
			s.totalNs += ns;
			s.maxNs = std::max(s.maxNs, ns);
			if (result)
				++s.generated;
			else
				++s.failed;
		}
	const double elapsed =
		std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

	std::vector<const GeneratorStats *> ordered;
	for (const auto &s : stats)
		ordered.push_back(&s);
	std::sort(ordered.begin(), ordered.end(), [](const GeneratorStats *a, const GeneratorStats *b)
			  { return a->totalNs > b->totalNs; });
	std::uint64_t totalAttempts = 0, totalGenerated = 0, totalFailed = 0, totalNs = 0;
	std::printf("%-24s%10s%10s%10s%12s%12s\n", "generator", "attempts", "ok", "failed", "mean_ms",
				"max_ms");
	for (const auto *s : ordered)
	{
		if (!s->attempts)
			continue;
		std::printf("%-24s%10llu%10llu%10llu%12.3f%12.3f\n", s->id.c_str(),
					(unsigned long long)s->attempts, (unsigned long long)s->generated,
					(unsigned long long)s->failed, s->totalNs / 1e6 / s->attempts, s->maxNs / 1e6);
		totalAttempts += s->attempts;
		totalGenerated += s->generated;
		totalFailed += s->failed;
		totalNs += s->totalNs;
	}
	std::printf("# %d rounds, %llu generators, %llu attempts (%llu ok, %llu failed), %.3fs wall,"
				" %.3fs summed generation time\n",
				rounds, (unsigned long long)methods.size(), (unsigned long long)totalAttempts,
				(unsigned long long)totalGenerated, (unsigned long long)totalFailed, elapsed,
				totalNs / 1e9);
	return 0;
}
