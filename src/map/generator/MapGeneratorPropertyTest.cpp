// SPDX-License-Identifier: GPL-3.0-or-later
// Generator properties across many seeds: what the map fingerprints only implied for a handful of
// fixed requests. Every playable landscape, at its defaults, rolls the same map twice from one
// seed, and every successful roll gives each colony a walk to the others, wheat, wood and room
// for a first base, and a start the fairness model scores above a floor. Raising a landscape's
// resource amounts never leaves less on the map. See docs/map-generators/verification.md.
//
// A failure names the generator and the seed; GLOB2_SEED=<n> replays that seed alone, and
// GLOB2_PROPERTY_ITERS=<k> runs k times as many seeds (test/README.md).
#include "HeadlessGlobals.h"
#include "Property.h"

#include "Game.h"
#include "GenerationContext.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "MapFingerprint.h"
#include "Pipeline.h"
#include "Utilities.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using D = GenerationRequest;

constexpr std::uint32_t kSeeds = 10;

// Fairness floors, from scoreStarts' StartQualityReport::fairness. Measured on master
// (linux-x86_64, default request at 256x256 with four colonies, seeds 1-10, 655 successful rolls
// over the 67 playable landscapes): the lowest single roll was 0.491 (shattered-coast seed 2,
// next 0.577 on its seed 5, then river at 0.648) and the lowest landscape mean was 0.747
// (shattered-coast, then encircled-kingdom 0.752). Each floor sits about 0.1 below what was
// observed, so a roll that hands one colony the map fails without the usual spread flaking.
constexpr double kRollFairnessFloor = 0.40;
constexpr double kMeanFairnessFloor = 0.65;

// Default rolls that must generate, of the ten. Measured on master: every landscape generated
// 10/10 except shattered-coast (seed 10 failed its validator) and orchard-commons (seed 8),
// at 9/10. The floor leaves a margin of two further failures before a lobby (which keeps the
// best of five rolls) starts showing players failed generations.
constexpr std::uint32_t kDefaultSuccessFloor = 7;

// Where a landscape's design deliberately does not promise one of the shared properties. Each
// entry says why, from the landscape's own validator; every other landscape is held to all of
// them. Measured on master: these are the only landscapes any of the ten default seeds failed.
struct Exception
{
	const char *id;
	bool coloniesWalk = true; ///< walkFromFirstColony: every colony walks to colony 0
	int woodRange = 32;       ///< startingFloorFailure's wood range
	int minimumSites = 16;    ///< startingFloorFailure's 4x4 build origins within 24 steps
	const char *why;
};
const Exception kExceptions[] = {
	// Colonies start apart by design: on islands, keys, plots cut by channels or moated homes.
	{"islands", false, 32, 16, "colonies start on separate islands"},
	{"rugged-archipelago", false, 32, 16, "colonies start on separate islands"},
	{"plantations", false, 32, 16, "every colony owns its islands (validator: coloniesApart)"},
	{"bastion-keys", false, 32, 16, "colonies start on separate keys"},
	{"contested-commons", false, 32, 16, "homes sit behind a moat; bridges are optional"},
	// Wood is a voyage away by design: their validators ask the floor with kReachableAnywhere.
	{"concrete-islands", false, MapGeneration::kReachableAnywhere, 16,
	 "plots cut apart by channels; wood may lie on another plot"},
	{"isles", true, MapGeneration::kReachableAnywhere, 16, "wood may lie on another isle"},
	// Without trails (the default) the forest is cut through, not walked: its validator checks
	// colony contact with chopping costs instead.
	{"old-growth", false, 32, 16, "colonies reach each other by cutting through the forest"},
};

const Exception &exceptionFor(const char *id)
{
	static const Exception none{"", true, 32, 16, ""};
	for (const auto &e : kExceptions)
		if (std::strcmp(e.id, id) == 0)
			return e;
	return none;
}

// Playable landscapes, split so each test case gets its own process and timeout. Portage
// Lakes and Drowned Forest take most of the time (about 11 s and 3 s a roll) and run alone.
const char *const kAlone[] = {"portage-lakes", "drowned-forest"};
constexpr int kShards = 6;

bool alone(const char *id)
{
	return std::any_of(std::begin(kAlone), std::end(kAlone),
					   [&](const char *a) { return std::strcmp(a, id) == 0; });
}

std::vector<int> landscapes(int shard)
{
	std::vector<int> result;
	int index = 0;
	for (int method : GeneratorRegistry::builtins().methods(false))
		if (!alone(GeneratorRegistry::builtins().at(method).id) && index++ % kShards == shard)
			result.push_back(method);
	return result;
}

void defaultProperties(int method)
{
	const auto &definition = GeneratorRegistry::builtins().at(method);
	const Exception &exception = exceptionFor(definition.id);
	INFO("generator " << std::string(definition.id) << " (" << method << ")");
	std::uint32_t rolls = 0, generated = 0;
	double fairness = 0;
	glob2test::forEachSeed(kSeeds, [&](std::uint32_t seed)
	{
		D request;
		request.setMethodDefaults(method);
		request.seed = seed;
		const auto surrounding = syncRandEngine();
		Game first(nullptr);
		const auto a = GenerationService().generate(first, request);
		// Generation draws from its own seeded streams, never the simulation's.
		CHECK(syncRandEngine() == surrounding);
		Game second(nullptr);
		const auto b = GenerationService().generate(second, request);
		++rolls;
		INFO(a.diagnostic());
		// Same seed, same map: what each platform's map fingerprints stand for.
		CHECK(bool(a) == bool(b));
		CHECK(a.stage == b.stage);
		CHECK(a.detail == b.detail);
		CHECK(mapFingerprint(first) == mapFingerprint(second));
		CHECK(first.checkSum(nullptr, nullptr, nullptr, true) ==
			  second.checkSum(nullptr, nullptr, nullptr, true));
		if (!a)
			return;
		++generated;
		const int teams = request.nbTeams;
		if (exception.coloniesWalk)
			CHECK(MapGeneration::walkFromFirstColony(first.map, teams, "the map", "").error == "");
		CHECK(MapGeneration::startingAccessFailure(
				  first.map, teams,
				  {{MaterialId::Food, 24, "food"}, {MaterialId::Wood, exception.woodRange, "wood"}},
				  exception.minimumSites) == "");
		REQUIRE(a.quality.measured);
		CHECK(a.quality.fairness >= kRollFairnessFloor);
		fairness += a.quality.fairness;
	}, 1);
	// The floors over the whole run hold only for the default seed count, not a single
	// GLOB2_SEED replay.
	if (rolls >= kSeeds)
	{
		CHECK(generated * kSeeds >= kDefaultSuccessFloor * rolls);
		if (generated)
			CHECK(fairness / generated >= kMeanFairnessFloor);
	}
}

long long totalAmount(const Game &game)
{
	long long total = 0;
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
			if (const auto r = game.map.getResource(x, y); r.type != NO_RES_TYPE)
				total += r.amount;
	return total;
}

// Resource amounts are the percentage controls (defaulting to 100) in the Resources group;
// the group's other members are on/off toggles and counts. Setting every amount to 0, 100 and
// 200 (or each control's maximum, if lower) must give strictly more resources at 100 than at 0
// and no fewer at 200 than at 100. Measured on master over seeds 1-5 of every landscape with
// such controls: this held without exception; above 200 it does not (portage-lakes seed 2 has
// less at 300 than at 200, watershed seed 5 likewise), so the check stops there.
void amountMonotonicity(int method)
{
	const auto &definition = GeneratorRegistry::builtins().at(method);
	std::vector<const GeneratorControl *> amounts;
	for (const auto &control : definition.controls)
		if (control.group == ControlGroup::Resources && control.defaultValue == 100 &&
			control.kind == GeneratorControl::Kind::Range && control.allowedValues.empty())
			amounts.push_back(&control);
	if (amounts.empty())
		return;
	INFO("generator " << std::string(definition.id) << " (" << method << ")");
	glob2test::forEachSeed(5, [&](std::uint32_t seed)
	{
		long long previous = -1;
		int previousAmount = -1;
		for (int amount : {0, 100, 200})
		{
			D request;
			request.setMethodDefaults(method);
			request.seed = seed;
			for (const auto *control : amounts)
				request.options[control->id] = std::min(amount, control->maximum);
			Game world(nullptr);
			const auto result = GenerationService().generate(world, request);
			INFO("amounts at " << amount << "%: " << result.diagnostic());
			// Generation at non-default amounts is the sweep's and the contracts' concern; a
			// roll that fails here has nothing to compare. Caravanserai seed 2 fails above
			// 100% on master.
			if (!result)
				continue;
			const long long total = totalAmount(world);
			CAPTURE(total);
			CAPTURE(previous);
			CAPTURE(previousAmount);
			if (previous >= 0)
			{
				if (previousAmount == 0)
					CHECK(total > previous);
				else
					CHECK(total >= previous);
			}
			previous = total;
			previousAmount = amount;
		}
	}, 1);
}
}

// One case per shard, so each gets its own process and timeout.
#define GLOB2_LANDSCAPE_SHARD(K)                                                                    \
	TEST_CASE("default rolls repeat and keep their promises: landscape shard " #K                  \
			  " [slow][map-generators]")                                                            \
	{                                                                                               \
		glob2test::HeadlessGlobals globals;                                                         \
		for (int method : landscapes(K))                                                            \
			defaultProperties(method);                                                              \
	}                                                                                               \
	TEST_CASE("raising resource amounts never leaves less on the map: landscape shard " #K         \
			  " [slow][map-generators]")                                                            \
	{                                                                                               \
		glob2test::HeadlessGlobals globals;                                                         \
		for (int method : landscapes(K))                                                            \
			amountMonotonicity(method);                                                             \
	}

TEST_SUITE("MapGeneratorProperties")
{
	GLOB2_LANDSCAPE_SHARD(0)
	GLOB2_LANDSCAPE_SHARD(1)
	GLOB2_LANDSCAPE_SHARD(2)
	GLOB2_LANDSCAPE_SHARD(3)
	GLOB2_LANDSCAPE_SHARD(4)
	GLOB2_LANDSCAPE_SHARD(5)
	TEST_CASE("default rolls repeat and keep their promises: portage-lakes [slow][map-generators]")
	{
		glob2test::HeadlessGlobals globals;
		defaultProperties(GeneratorRegistry::builtins().idOf("portage-lakes"));
	}
	TEST_CASE("default rolls repeat and keep their promises: drowned-forest [slow][map-generators]")
	{
		glob2test::HeadlessGlobals globals;
		defaultProperties(GeneratorRegistry::builtins().idOf("drowned-forest"));
	}
	TEST_CASE("raising resource amounts never leaves less on the map: portage-lakes [slow][map-generators]")
	{
		glob2test::HeadlessGlobals globals;
		amountMonotonicity(GeneratorRegistry::builtins().idOf("portage-lakes"));
	}
	TEST_CASE("raising resource amounts never leaves less on the map: drowned-forest [slow][map-generators]")
	{
		glob2test::HeadlessGlobals globals;
		amountMonotonicity(GeneratorRegistry::builtins().idOf("drowned-forest"));
	}
	TEST_CASE("every playable landscape is covered by exactly one property shard")
	{
		glob2test::HeadlessGlobals globals;
		std::vector<int> covered;
		for (int shard = 0; shard < kShards; ++shard)
			for (int method : landscapes(shard))
				covered.push_back(method);
		for (const char *id : kAlone)
		{
			REQUIRE(GeneratorRegistry::builtins().idOf(id) >= 0);
			covered.push_back(GeneratorRegistry::builtins().idOf(id));
		}
		auto playable = GeneratorRegistry::builtins().methods(false);
		std::sort(covered.begin(), covered.end());
		std::sort(playable.begin(), playable.end());
		CHECK(covered == playable);
		for (const auto &e : kExceptions)
			CHECK_MESSAGE(GeneratorRegistry::builtins().idOf(e.id) >= 0, e.id);
	}
}
