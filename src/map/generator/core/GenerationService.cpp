// SPDX-License-Identifier: GPL-3.0-or-later
#include <PerformanceTelemetry.h>
#include "GenerationService.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GenerationValidation.h"
#include "StartQuality.h"
#include "Utilities.h"
#include <algorithm>
#include <chrono>
#include <string>
std::string GenerationResult::diagnostic() const
{
	return generatorId + " revision " + std::to_string(revision) + " seed " + std::to_string(seed) +
		   " [" + stage + "]: " + detail;
}
GenerationResult GenerationService::generate(Game &game, const GenerationRequest &request,
											 bool collectTelemetry) const
{
	PERF_SCOPE_TIME(Generation);
	GenerationResult result;
	GenerationContext context(request, collectTelemetry);
	const auto finish = [&]()
	{
		if (result.error != GenerationError::None && context.telemetry.enabled())
			context.telemetry.error("generation.failure", result.stage + ": " + result.detail);
		result.telemetry = std::move(context.telemetry);
		return std::move(result);
	};
	result.seed = request.seed;
	result.stage = "validation";
	const auto *definition = registry.find(request.method);
	if (!definition)
	{
		result.error = GenerationError::InvalidRequest;
		result.detail = "Unknown generator";
		return finish();
	}
	result.generatorId = definition->id;
	result.revision = definition->revision;
	result.detail = validateGenerationRequest(request, *definition);
	if (!result.detail.empty())
	{
		result.error = GenerationError::InvalidRequest;
		return finish();
	}
	if (game.mapHeader.getNumberOfTeams() != 0)
	{
		result.error = GenerationError::NonEmptyTarget;
		result.detail = "Generation requires a fresh Game";
		return finish();
	}
	// General engine mutation APIs still use the synchronized gameplay stream.
	// This synchronous, scoped bridge is not a concurrency API.
	struct EngineRandomScope
	{
		boost::mt19937 saved = syncRandEngine();
		~EngineRandomScope() { syncRandEngine() = saved; }
	} rngScope;
	setSyncRandSeed(GenerationContext::deriveSeed(request.seed, "engine"));
	game.gameHeader.setRandomSeed(request.seed);
	game.map.setSize(request.wDec, request.hDec);
	game.map.setGame(&game);
	bool generated = false;
	try
	{
		generated = definition->generate(game, context);
	}
	catch (const GenerationFailure &error)
	{
		context.detail = error.what();
	}
	if (!generated)
	{
		result.error = GenerationError::PlacementFailed;
		result.stage = context.stage;
		result.detail =
			context.detail.empty() ? "Could not place the requested map layout" : context.detail;
		return finish();
	}
	result.stage = "validation";
	result.detail = validateGeneratedWorld(game, request, *definition);
	if (!result.detail.empty())
	{
		result.error = GenerationError::InvalidWorld;
		return finish();
	}
	if (definition->validateWorld)
	{
		result.stage = "generator validation";
		result.detail = definition->validateWorld(game, context);
		if (!result.detail.empty())
		{
			result.error = GenerationError::InvalidWorld;
			return finish();
		}
	}
	result.quality = MapGeneration::scoreStarts(game, request.nbTeams, definition->qualityWeights,
												definition->qualityScale);
	result.stage = "script";
	const auto script = game.sgslScript.compileScript(&game);
	if (script.type != ErrorReport::ET_OK)
	{
		result.error = GenerationError::InvalidWorld;
		result.detail = script.getErrorString();
		return finish();
	}
	result.stage = "complete";
	return finish();
}

std::uint32_t GenerationService::bestSeed(const GenerationRequest &request, std::uint32_t rootSeed,
										  int candidates,
										  std::vector<CandidateRoll> *rolls) const
{
	std::uint32_t chosen = GenerationContext::deriveSeed(rootSeed, "attempt/0");
	double bestScore = -1.0;
	for (int attempt = 0; attempt < std::max(1, candidates); ++attempt)
	{
		Game game(nullptr);
		GenerationRequest roll = request;
		roll.seed = GenerationContext::deriveSeed(rootSeed, "attempt/" + std::to_string(attempt));
		const auto start = std::chrono::steady_clock::now();
		const auto result = generate(game, roll);
		const double seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (rolls)
			rolls->push_back({roll.seed, bool(result), result ? result.quality.score : 0.0, seconds});
		if (!result || result.quality.score <= bestScore)
			continue;
		bestScore = result.quality.score;
		chosen = roll.seed;
	}
	return chosen;
}
