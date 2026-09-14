// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class Game;
struct GenerationContext;
namespace MapGeneration
{
// Chooses each colony's boot tile so that every colony's walk to its primary resources is as
// nearly equal as the finished map allows, rather than handing the best land to whoever is
// picked first. Requires resources to already be on the map. Returns false (leaving bootX/bootY
// untouched) when no set of legal, mutually distant sites can reach both resources, so a caller
// can fall back to a terrain-only search.
bool chooseBalancedStarts(Game &game, GenerationContext &context, int minDistSquare);
} // namespace MapGeneration
