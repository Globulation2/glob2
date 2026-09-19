// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapGeneratorFrameworkChecks.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include "Growth.h"
#include "Sketch.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace LastTreelineChecks
{
inline GenerationRequest request(unsigned seed = 71)
{
	GenerationRequest r;
	r.setMethodDefaults(GeneratorRegistry::builtins().idOf("last-treeline"));
	r.wDec = r.hDec = 8;
	r.nbTeams = 4;
	r.seed = seed;
	return r;
}
inline void run()
{
	using namespace MapGeneration;
	GenerationService service;
	auto r = request();
	const auto &definition = GeneratorRegistry::builtins().at(r.method);
	for (int w : {8, 9})
		for (int h : {8, 9})
			for (int teams : {2, 3, 4, 5, 6, 7, 8})
			{
				auto shape = r;
				shape.wDec = w;
				shape.hDec = h;
				shape.nbTeams = teams;
				shape.nbWorkers = 1 + (teams + w + h) % 8;
				Game game(nullptr);
				const auto result = service.generate(game, shape);
				if (!result)
					std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
				assert(result);
			}
	for (int teams : {1, 9, 12})
	{
		auto bad = r;
		bad.nbTeams = teams;
		assert(!definition.validateRequest(bad).empty());
	}
	for (int side : {6, 7, 10})
	{
		auto bad = r;
		bad.wDec = side;
		assert(!definition.validateRequest(bad).empty());
	}
	Game plain(nullptr), traced(nullptr);
	assert(service.generate(plain, r));
	const auto trace = service.generate(traced, r, true);
	assert(trace && mapFingerprint(plain) == mapFingerprint(traced));
	Game repeated(nullptr);
	const auto again = service.generate(repeated, r, true);
	assert(again && mapFingerprint(repeated) == mapFingerprint(traced));
	assert(trace.telemetry.records().size() == again.telemetry.records().size());
	for (size_t k = 0; k < trace.telemetry.records().size(); ++k)
	{
		const auto &a = trace.telemetry.records()[k], &b = again.telemetry.records()[k];
		assert(a.key == b.key && a.value == b.value && a.subject == b.subject);
	}
	for (int amount : {0, 300})
		for (int depth : {12, 14, 16})
		{
			auto extreme = r;
			extreme.options["woodland-depth"] = depth;
			for (const auto &control : definition.controls)
				if (control.group == ControlGroup::Resources)
					extreme.options[control.id] = amount;
			Game game(nullptr);
			const auto result = service.generate(game, extreme);
			if (!result)
				std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
			assert(result);
		}
	// Containment is visible terrain: even after long unattended growth, crops cannot
	// enter outside grass. Finite starter trees must be unchanged without harvesters.
	const Torus t(plain.map);
	TerrainSketch terrain(t.size());
	for (int i = 0; i < t.size(); ++i)
		terrain[i] = plain.map.getUMTerrain(i % t.w, i / t.w);
	const auto fertility = cropGrowthField(terrain, t);
	std::vector<unsigned char> renewable(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = plain.map.getResource(i % t.w, i / t.w).type;
		renewable[i] = (type == WOOD || type == WHEAT) && fertility.at(i % t.w, i / t.w) > 0;
	}
	const auto envelope = floodFrom(t, renewable, pureTiles(plain.map, GRASS));
	std::vector<int> finite;
	for (int i = 0; i < t.size(); ++i)
		if (plain.map.getResource(i % t.w, i / t.w).type == WOOD &&
			fertility.at(i % t.w, i / t.w) == 0)
			finite.push_back(i);
	assert(finite.size() >= 48 * 4);
	for (int tick = 0; tick < 2048; ++tick)
		plain.map.growResources();
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = plain.map.getResource(i % t.w, i / t.w).type;
		if (type == WHEAT || type == WOOD)
			assert(envelope.steps[i] >= 0 ||
				   std::find(finite.begin(), finite.end(), i) != finite.end());
	}
	for (int i : finite)
		assert(plain.map.getResource(i % t.w, i / t.w).type == WOOD);
	GenerationContext context(r);
	assert(definition.validateWorld(plain, context).empty());
	// Fault injection: introducing trees into a wheat plot must be rejected even
	// when they are watered and the surrounding sand remains intact.
	bool changed = false;
	for (int i = 0; i < t.size() && !changed; ++i)
		if (traced.map.getResource(i % t.w, i / t.w).type == WHEAT)
		{
			traced.map.setNoResource(i % t.w, i / t.w, 1);
			traced.map.setResource(i % t.w, i / t.w, WOOD, 1);
			changed = true;
		}
	assert(changed && !definition.validateWorld(traced, context).empty());
	Game cleared(nullptr), irrigated(nullptr);
	assert(service.generate(cleared, r));
	assert(service.generate(irrigated, r));
	for (int i = 0; i < t.size(); ++i)
		if (cleared.map.getResource(i % t.w, i / t.w).type == WOOD &&
			fertility.at(i % t.w, i / t.w) > 0)
			cleared.map.setNoResource(i % t.w, i / t.w, 1);
	assert(!definition.validateWorld(cleared, context).empty());
	int tx = -1, ty = -1, vx = 0, vy = 0;
	for (int tree : finite)
	{
		for (const auto &direction : kCardinalSteps)
		{
			bool outside = true;
			for (int sign : {-1, 1})
				for (int dy = -6; dy <= 6; ++dy)
					for (int dx = -6; dx <= 6; ++dx)
						outside &=
							envelope.steps[t.at(tree % t.w + sign * 7 * direction[0] + dx,
												tree / t.w + sign * 7 * direction[1] + dy)] < 0;
			if (outside)
			{
				tx = tree % t.w;
				ty = tree / t.w;
				vx = direction[0];
				vy = direction[1];
				break;
			}
		}
		if (tx >= 0)
			break;
	}
	assert(tx >= 0);
	// Include an explicit sand rim: controlSand erodes an unbuffered tiny pool.
	for (int dy = -3; dy <= 3; ++dy)
		for (int dx = -3; dx <= 3; ++dx)
		{
			irrigated.map.setUMTerrain(t.x(tx + 7 * vx + dx), t.y(ty + 7 * vy + dy),
									   std::abs(dx) <= 2 && std::abs(dy) <= 2 ? WATER : SAND);
			irrigated.map.setUMTerrain(t.x(tx - 7 * vx + dx), t.y(ty - 7 * vy + dy), GRASS);
		}
	irrigated.map.controlSand();
	irrigated.map.rebuildTerrain();
	for (int i = 0; i < t.size(); ++i)
		terrain[i] = irrigated.map.getUMTerrain(i % t.w, i / t.w);
	assert(cropGrowthField(terrain, t).at(tx, ty) > 0);
	assert(definition.validateWorld(irrigated, context) ==
		   "A spreading crop was planted outside its contained plot.");
	puts("PASS Last Treeline: shapes, colony counts, refusals, telemetry determinism, extremes, "
		 "dry wood and crop separation");
}
inline void profile()
{
	GenerationService service;
	for (const char *id : {"last-treeline", "orchard-commons"})
		for (bool telemetry : {false, true})
		{
			auto r = request();
			r.setMethodDefaults(GeneratorRegistry::builtins().idOf(id));
			r.wDec = r.hDec = 9;
			r.nbTeams = 8;
			const auto start = std::chrono::steady_clock::now();
			const auto cpu = std::clock();
			for (int k = 0; k < 20; ++k)
			{
				r.seed = 71 + k;
				Game game(nullptr);
				const auto result = service.generate(game, r, telemetry);
				if (!result)
					std::fprintf(stderr, "%s\n", result.diagnostic().c_str());
				assert(result);
			}
			std::printf(
				"PROFILE %s telemetry=%d mean_seconds=%.6f cpu_seconds=%.6f\n", id, telemetry,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() /
					20,
				double(std::clock() - cpu) / CLOCKS_PER_SEC / 20);
		}
}
} // namespace LastTreelineChecks
