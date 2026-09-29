// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapGeneratorContracts.h"
#include <map>
namespace GeneratorContracts
{
inline void hungryMarchesContracts()
{
	const auto &definition =
		GeneratorRegistry::builtins().at(GeneratorRegistry::builtins().idOf("hungry-marches"));
	D r;
	r.setMethodDefaults(definition.legacyId);
	r.wDec = r.hDec = 8;
	r.nbTeams = 4;
	r.seed = 101;
	GenerationService service;
	{
		Game plain(nullptr), observed(nullptr);
		assert(service.generate(plain, r));
		const auto result = service.generate(observed, r, true);
		assert(result && !result.telemetry.records().empty());
		assert(mapFingerprint(plain) == mapFingerprint(observed));
		std::map<int, long long> reported, actual;
		for (const auto &record : result.telemetry.records())
		{
			int type = -1;
			if (record.key == "hungry-marches.home.wheat" ||
				record.key == "hungry-marches.field.wheat")
				type = WHEAT;
			if (record.key == "hungry-marches.home.wood" ||
				record.key == "hungry-marches.renewable-timber" ||
				record.key == "hungry-marches.dry-scrub")
				type = WOOD;
			if (record.key == "hungry-marches.home.stone")
				type = STONE;
			if (type >= 0)
				reported[type] += std::get<std::int64_t>(record.value);
		}
		for (int y = 0; y < observed.map.getH(); ++y)
			for (int x = 0; x < observed.map.getW(); ++x)
			{
				const int type = observed.map.getResource(x, y).type;
				if (type == WOOD || type == WHEAT || type == STONE)
					++actual[type];
			}
		for (int type : {WOOD, WHEAT, STONE})
			assert(reported[type] == actual[type]);
	}
	for (auto shape :
		 {std::pair{7, 7}, std::pair{7, 8}, std::pair{8, 7}, std::pair{8, 8}, std::pair{7, 9},
		  std::pair{9, 7}, std::pair{8, 9}, std::pair{9, 8}, std::pair{9, 9}})
		for (int teams : {2, 4, 12})
		{
			D request = r;
			request.wDec = shape.first;
			request.hDec = shape.second;
			request.nbTeams = teams;
			Game world(nullptr);
			const auto result = service.generate(world, request);
			if (std::min(shape.first, shape.second) == 7 && teams > 4)
				assert(result.error == GenerationError::InvalidRequest);
			else
			{
				if (!result)
					fprintf(stderr, "Hungry Marches %d/%d/%d: %s\n", shape.first, shape.second,
							teams, result.diagnostic().c_str());
				assert(result);
			}
		}
	// Largest crowded layouts must leave room for central shoreline detours.
	{
		auto request = r;
		request.wDec = request.hDec = 9;
		request.nbTeams = 12;
		request.seed = 106;
		Game world(nullptr);
		assert(service.generate(world, request));
	}
	for (int amount : {0, 200})
	{
		auto request = r;
		for (const auto &control : definition.controls)
			if (control.group == ControlGroup::Resources)
				request.options[control.id] = amount;
		Game world(nullptr);
		assert(service.generate(world, request));
		GenerationContext check(request);
		setSyncRandSeed(2026);
		for (int tick = 0; tick < 4096; ++tick)
			world.map.growResources();
		const auto error = definition.validateWorld(world, check);
		if (!error.empty())
			fprintf(stderr, "Hungry Marches growth: %s\n", error.c_str());
		assert(error.empty());
	}
	{
		Game world(nullptr);
		assert(service.generate(world, r));
		const auto fertility = Fertility::forMap(world.map, false);
		bool removed = false;
		for (int y = 0; y < world.map.getH() && !removed; ++y)
			for (int x = 0; x < world.map.getW() && !removed; ++x)
				if (world.map.getResource(x, y).type == WHEAT && !fertility.at(x, y))
				{
					world.map.setNoResource(x, y, 1);
					removed = true;
				}
		GenerationContext check(r);
		assert(removed && !definition.validateWorld(world, check).empty());
	}
	{
		Game world(nullptr);
		assert(service.generate(world, r));
		const auto fertility = Fertility::forMap(world.map, false);
		for (int y = 0; y < world.map.getH(); ++y)
			for (int x = 0; x < world.map.getW(); ++x)
				if (world.map.getResource(x, y).type == WHEAT && fertility.at(x, y))
					world.map.setNoResource(x, y, 1);
		GenerationContext check(r);
		assert(!definition.validateWorld(world, check).empty());
	}
	puts("PASS Hungry Marches: supported shapes, refusal, telemetry identity, resource extremes, "
		 "late growth, finite-ration and shared-food corruption");
}
} // namespace GeneratorContracts
