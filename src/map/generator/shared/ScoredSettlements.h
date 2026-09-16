// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StartQuality.h"
#include <functional>
#include <string>
#include <vector>
class Game;
struct GenerationContext;
namespace MapGeneration
{
// A terrain shortlist is only a proposal. This operation compares complete worlds:
// actual swarms, workers, resources, clearance and any permitted repairs. It is useful
// for asymmetric landscapes whose settlement geometry differs from BalancedStarts'
// legacy model. Site integers are opaque to this helper (tiles, regions or home IDs).
struct ScoredSettlementChoice
{
	std::vector<int> sites;
	StartQualityReport quality;
	int selected = -1, viable = 0;
	std::string failure;
};
using SettlementBuilder =
	std::function<bool(Game &, GenerationContext &, const std::vector<int> &)>;
using SettlementCheck = std::function<std::string(const StartQualityReport &)>;

/// Build each complete proposal in a fresh Game, starting with a COPY of context's
/// current named streams and the same engine RNG state. Select the highest score
/// passing check; ties retain the first proposal. Incomplete proposals are skipped.
/// No candidate changes the target world, caller's streams, boot positions or engine
/// RNG, even if a builder throws. Trial details stay out of the final attempt's trace;
/// bounded per-proposal outcomes and selected quality are recorded here instead.
///
/// The caller must then invoke the SAME builder with the returned sites and its
/// unchanged context. Do not draw from builder streams or mutate engine RNG between
/// selection and final materialization. A builder owns terrain through final repairs;
/// it must be deterministic, and check must retain absolute viability requirements.
/// The supplied proposal vector is the work budget: there are no implicit retries,
/// relaxed thresholds or wall-clock limits. Exceptions propagate with RNG restored.
ScoredSettlementChoice chooseScoredSettlements(GenerationContext &,
											   const std::vector<std::vector<int>> &proposals,
											   const SettlementBuilder &, const SettlementCheck &,
											   const StartQualityWeights & = {},
											   const StartQualityScale & = {});
} // namespace MapGeneration
