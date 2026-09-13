// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <vector>
class Game;
namespace MapGeneration
{
// Where colonies' towers go. A defence tower is 2x2 and scans square rings out to its range beyond
// every side of its footprint, with no line of sight, so a tower beside a wall covers the ground on
// the other side of it. These choose the sites that cover the most of other colonies' ground over
// the walls between them, the same number for every colony, and keep open pads for more.

/// Every colony's chosen tower sites, and its open pads: 2x2 footprints left clear for players to
/// build towers on later. Tiles are the footprints' top-left corners.
struct TowerPlan
{
	std::vector<std::vector<int>> towers, pads;
	// Per tower, whether it starts with its bullets: only where no other colony's tower is in range.
	std::vector<std::vector<unsigned char>> stocked;
};

/// What a plan asks for.
struct TowerRequest
{
	int range = 7;   // the range the sites are scored at
	int towers = 0;  // towers per colony
	int pads = 0;    // open pads per colony, beyond its towers
	int spacing = 6; // the least Chebyshev distance between two of a colony's sites
	// What a target tile in range is worth: one of another colony's (offence, over the walls) and one
	// of the site's own colony's (defence, covering the ground attackers must cross).
	int otherWeight = 1, ownWeight = 0;
	// With a wall, every site (tower or pad) stands directly against it: some tile of `against` touches the
	// footprint, diagonals included, so the tower shoots over the wall rather than from inland.
	const std::vector<unsigned char> *against = nullptr;
};

/// Chooses tower sites for every colony. `owner` gives each tile's colony (-1 for none); a site is a
/// 2x2 footprint wholly on one colony's `buildable` ground, scored by the `target` tiles within its
/// range: each of another colony's worth `otherWeight` (the ground it can shoot at over the walls) and
/// each of its own colony's worth `ownWeight` (the ground it defends, such as its own lanes). Sites are taken
/// best first, a colony at a time in turn, so every colony gets its first choice before any gets its
/// second, and every colony ends with the same number. No site may reach a tile of another colony's
/// `keepOutOfRange` (its swarm). Towers facing each other across a wall are the point, but a tower
/// within range of another colony's tower starts empty (`stocked`), so the game does not open with
/// towers shooting each other down. Pads need only keep their spacing. A colony whose ground offers
/// no site that reaches anyone takes its best remaining ones anyway. Returns fewer than asked only
/// where a colony has no room; the caller decides whether that fails the map.
TowerPlan chooseTowerSites(const Torus &, const std::vector<int> &owner,
						   const std::vector<unsigned char> &buildable,
						   const std::vector<unsigned char> &target,
						   const std::vector<unsigned char> &keepOutOfRange, int colonies,
						   const TowerRequest &);

/// Keeps a plan from closing the ways a map promises: for every colony, while `goals` cannot be walked to
/// from `sources[k]` over `open` ground with the plan's footprints (towers and pads alike, since a pad is
/// where a tower will go) blocked, its last-chosen site is dropped. Returns how many sites were dropped.
/// Run evenTowerPlan afterwards so every colony still has the same count.
int dropBlockingSites(const Torus &, TowerPlan &, const std::vector<unsigned char> &open,
					  const std::vector<std::vector<int>> &sources,
					  const std::vector<std::vector<unsigned char>> &goals);

/// Evens a plan out where some colony got fewer sites than asked: every colony keeps only as many
/// towers, and as many pads, as the colony with fewest, its best ones, so no colony starts with more
/// battlements than another. Returns the towers per colony left.
int evenTowerPlan(TowerPlan &);

/// Ground roomy enough for a tower that blocks nothing: the tiles of `open` (land a unit walks on) at
/// least `room` steps from any tile that is not. A 2x2 tower on a strip narrower than that could close
/// the strip; a generator ands this into the buildable ground it hands chooseTowerSites.
std::vector<unsigned char> roomyGround(const Torus &, const std::vector<unsigned char> &open, int room);

/// Builds a plan's towers: every colony's tower sites get a completed tower of `level` (placeTower),
/// stocked where the plan says. Pads stay open. False, and nothing more built, at the first site that no longer
/// fits.
bool raiseTowers(Game &, const TowerPlan &, int level);

/// The tiles of every footprint in a plan (towers and pads): the ground a generator keeps clear of
/// deposits and roads so the sites stay buildable.
std::vector<unsigned char> towerFootprints(const Torus &, const TowerPlan &);
} // namespace MapGeneration
