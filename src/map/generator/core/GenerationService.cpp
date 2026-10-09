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
	const auto *definition =
		(useRequestCatalog && request.catalog ? *request.catalog : registry).find(request.method);
	if (!definition)
	{
		result.error = GenerationError::InvalidRequest;
		result.detail = "Unknown generator";
		return finish();
	}
	result.generatorId = definition->id;
	result.revision = definition->revision;
	result.packageHash = definition->packageHash;
	result.apiVersion = definition->apiVersion;
	try
	{
		auto contract=*definition;
        contract.validateRequest={};
        result.detail=validateGenerationRequest(request,contract);
        if(result.detail.empty() && definition->validateRequest)result.detail=definition->validateRequest(request);
	}
	catch (const ScriptGenerationFailure &error)
	{
		result.error = error.error;
		result.detail = error.what();
		return finish();
	}
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
	game.gameHeader.setRandomSeed(request.seed);
	game.map.worldRandom.initialize(request.seed);
	game.map.setSize(request.wDec, request.hDec);
	// The drawn terrain look follows the request seed too, without touching the
	// simulation streams, so a regenerated map is identical on every client.
	game.map.setTerrainSeed(GenerationContext::deriveSeed(request.seed, "terrain-look"));
	game.map.setGame(&game);
	bool generated = false;
	try
	{
		generated = definition->generate(game, context);
	}
	catch (const ScriptGenerationFailure &error)
	{
		result.error = error.error;
		result.detail = error.what();
		result.stage = context.stage;
		return finish();
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
		try
		{
			result.detail = definition->validateWorld(game, context);
		}
		catch (const ScriptGenerationFailure &error)
		{
			result.error = error.error;
			result.detail = error.what();
			return finish();
		}
		if (!result.detail.empty())
		{
			result.error = GenerationError::InvalidWorld;
			return finish();
		}
	}
	result.quality = MapGeneration::scoreStarts(game, request.nbTeams);
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
