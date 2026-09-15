// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScoredSettlements.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Utilities.h"
namespace MapGeneration
{
ScoredSettlementChoice
chooseScoredSettlements(GenerationContext &context, const std::vector<std::vector<int>> &proposals,
						const SettlementBuilder &build, const SettlementCheck &check,
						const StartQualityWeights &weights, const StartQualityScale &scale)
{
	struct EngineScope
	{
		boost::mt19937 initial = syncRandEngine();
		~EngineScope() { syncRandEngine() = initial; }
	} engine;
	GenerationContext initial(context);
	initial.telemetry = GenerationTelemetry(false);
	ScoredSettlementChoice result;
	double bestScore = -1;
	context.telemetry.measure("starts.scored.proposals", proposals.size());
	for (size_t k = 0; k < proposals.size(); ++k)
	{
		if (int(proposals[k].size()) != context.request.nbTeams)
		{
			context.telemetry.choice("starts.scored.outcome", "incomplete proposal", int(k));
			continue;
		}
		Game trial(nullptr);
		trial.map.setSize(context.request.wDec, context.request.hDec);
		trial.map.setGame(&trial);
		GenerationContext probe(initial);
		syncRandEngine() = engine.initial;
		if (!build(trial, probe, proposals[k]))
		{
			result.failure =
				probe.detail.empty() ? "Settlement construction failed." : probe.detail;
			context.telemetry.choice("starts.scored.outcome", result.failure, int(k));
			continue;
		}
		const auto quality = scoreStarts(trial, context.request.nbTeams, weights, scale);
		const std::string failure =
			quality.measured && int(quality.colonies.size()) == context.request.nbTeams
				? check(quality)
				: "No complete set of colonies to score.";
		if (!failure.empty())
		{
			result.failure = failure;
			context.telemetry.choice("starts.scored.outcome", failure, int(k));
			continue;
		}
		++result.viable;
		context.telemetry.choice("starts.scored.outcome", "viable", int(k));
		context.telemetry.measure("starts.scored.score", quality.score, int(k));
		if (quality.score > bestScore)
		{
			bestScore = quality.score;
			result.selected = int(k);
			result.sites = proposals[k];
			result.quality = quality;
		}
	}
	context.telemetry.measure("starts.scored.viable", result.viable);
	context.telemetry.measure("starts.scored.selected", result.selected);
	if (result.selected >= 0)
		result.failure.clear();
	else if (result.failure.empty())
		result.failure = "No complete settlement proposal fits.";
	return result;
}
} // namespace MapGeneration
