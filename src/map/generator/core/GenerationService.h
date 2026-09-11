// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GenerationResult.h"
#include "GeneratorRegistry.h"
#include <cstdint>
class Game;
class GenerationService
{
	const GeneratorRegistry &registry;

  public:
	explicit GenerationService(const GeneratorRegistry &registry = GeneratorRegistry::builtins())
		: registry(registry)
	{
	}
	GenerationResult generate(Game &freshGame, const GenerationRequest &request) const;

	// Rolls of the same generator differ in how good a start they give, not only in whether
	// every colony fits, so a caller that can afford more than one roll should keep the best
	// rather than the first. Five is what the lobby's regenerate debounce affords on the
	// slowest generator; past that, generation has to leave the UI thread first.
	static constexpr int kSampledCandidates = 5;

	/// Seed of the best-scoring of kSampledCandidates rolls derived from rootSeed, for callers
	/// that generate into a Game they cannot hold a spare of. Generation is deterministic, so
	/// regenerating the returned seed reproduces that roll exactly. Falls back to the first
	/// derived seed when no roll succeeds, leaving the caller a failure it can report.
	std::uint32_t bestSeed(const GenerationRequest &request, std::uint32_t rootSeed,
						   int candidates = kSampledCandidates) const;
};
