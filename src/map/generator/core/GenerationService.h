// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GenerationResult.h"
#include "GeneratorRegistry.h"
#include <cstdint>
#include <vector>
class Game;
class GenerationService
{
	const GeneratorRegistry &registry;

  public:
	explicit GenerationService(const GeneratorRegistry &registry = GeneratorRegistry::builtins())
		: registry(registry)
	{
	}
	GenerationResult generate(Game &freshGame, const GenerationRequest &request,
							  bool collectTelemetry = false) const;

	// Rolls of the same generator differ in how good a start they give, not only in whether
	// every colony fits, so a caller that can afford more than one roll should keep the best
	// rather than the first. Five is what the lobby's regenerate debounce affords on the
	// slowest generator; past that, generation has to leave the UI thread first.
	static constexpr int kSampledCandidates = 5;

	/// One attempted roll, for callers measuring what sampling more rolls buys.
	struct CandidateRoll
	{
		std::uint32_t seed = 0;
		bool generated = false;  ///< false when that roll failed to produce a world at all
		double score = 0;        ///< the roll's map score; meaningless when it failed
		double seconds = 0;      ///< wall time this roll cost, generation only
	};

	/// Seed of the best-scoring of kSampledCandidates rolls derived from rootSeed, for callers
	/// that generate into a Game they cannot hold a spare of. Generation is deterministic, so
	/// regenerating the returned seed reproduces that roll exactly. Falls back to the first
	/// derived seed when no roll succeeds, leaving the caller a failure it can report.
	/// `rolls`, when given, receives every attempt in order, which is what the sampling study
	/// reads best-of-k curves off: the best of the first k entries is what k candidates buy.
	std::uint32_t bestSeed(const GenerationRequest &request, std::uint32_t rootSeed,
						   int candidates = kSampledCandidates,
						   std::vector<CandidateRoll> *rolls = nullptr) const;
};
