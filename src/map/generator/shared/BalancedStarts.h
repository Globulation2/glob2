// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
class Game;
struct GenerationContext;
namespace MapGeneration
{
struct SeparatedSites
{
	std::vector<int> indices; ///< indices in the caller's candidate list, in selection order
	int attempts = 0;
	std::string failure;
};
/// Greedy maximin selection on the torus, retrying up to maximumAttempts first anchors.
/// Candidates are tile indices already filtered for fit. Input order breaks ties; shuffle
/// it beforehand for seeded variety. This operation owns no RNG or colony/economy policy.
/// Stops at the first complete set; on failure returns the largest partial set and reason.
/// Separation is Chebyshev tile distance, including seams. Limits: 4096 candidates,
/// 32 requested sites, 64 attempts. Failure is not a proof that no packing exists.
SeparatedSites selectSeparatedSites(const Torus &, const std::vector<int> &candidates, int count,
									int minimumSeparation, int maximumAttempts = 64);

// Chooses each colony's boot tile so that every colony's walk to its primary resources is as
// nearly equal as the finished map allows, rather than handing the best land to whoever is
// picked first. Requires resources to already be on the map. Returns false (leaving bootX/bootY
// untouched) when no set of legal, mutually distant sites can reach both resources, so a caller
// can fall back to a terrain-only search.
bool chooseBalancedStarts(Game &game, GenerationContext &context, int minDistSquare);
} // namespace MapGeneration
