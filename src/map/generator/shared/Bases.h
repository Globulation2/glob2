// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include <string>
#include <vector>
class Game;
class Map;
struct GenerationContext;
struct GenerationRequest;
namespace MapGeneration
{
// Premade bases: a colony that starts with a whole base already standing - swarm, inns, hospital,
// school, barracks, racetrack, towers - or with the same base staked out as construction sites,
// and with far more colonists than the lobby's "Starting workers" control (1 to 8) allows. Every
// other landscape starts one swarm and a handful of workers (placeSettlement); a landscape built on
// this skips the first quarter hour of every match and asks its players to decide, from the first
// tick, what to do with a colony that already works.
//
// The shape follows Towers.h: a plan designed once in the base's own frame (tiles along and across
// its facing), a fit test a design runs on its sketch, a settle step that raises the buildings and
// puts the colonists down, footprint masks the later layers keep clear of, and a validator's proof
// that the base stands as planned. The frame turns by quarter turns (Canals' block frame), so one
// layout lands the same way round at every colony whatever way it faces, and rectangles stay
// rectangles, which keeps every footprint a legal building position.
//
// ENGINE FACTS the settle step is built on (src/building/Lifecycle.cpp, src/team/TeamLists.cpp,
// src/Game_editor.cpp; see docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md, "Premade bases"):
// - Game::addBuilding checks no room; checkRoomForBuilding must run first. A finished type comes
//   out of its constructor complete (hp = hpInit), with empty stock and in no call list; a level-0
//   construction-site type comes out as a valid site (hp 1). Higher-level sites need an UPGRADE
//   state the constructor does not set, so a plan never asks for one.
// - Stock is written straight into Building::resources (capped by the type's maxResource) and a
//   tower's bullets into Building::bullets; both persist through the lobby's save and reload, while
//   maxUnitWorking does not, so no plan relies on it.
// - Team::createLists asserts its lists are empty and then rebuilds them from myBuildings, running
//   every building's update() (which is what registers an inn to feed and a site to be worked). So
//   it runs exactly once per colony, after the last building; placeTower (Settlements.h) pushes into
//   the turret list by hand because it runs after that, which is why a design raises its bases
//   first and chooses wall towers second.
// - The structural check after generation counts a colony's WORKER units against
//   GeneratorDefinition::startingWorkers, so a landscape using this sets that hook to its own
//   colonists control; warriors and explorers are not counted.

/// One building of a base, in the base's frame: `type` and `level` name the finished building
/// ("inn", 1 is a level-2 inn as the game numbers it), or with `finished` false the level-0
/// construction site of that type (`level` must then be 0). (along, across) is the footprint's
/// top-left tile at facing 0: along runs down the facing (+x), across to its right (+y).
/// `stockPercent` fills that share of the type's wheat cap (swarm, inn) or bullet cap (tower).
struct BasePiece
{
	const char *type;
	int level;
	int along, across;
	bool finished;
	int stockPercent;
};

/// A deposit laid beside the buildings once the terrain is written: the quarry inside a compound, a
/// stack of wood beside a construction site. A clump of `radius` (placeResourceClump) centred on the
/// frame tile (along, across).
struct BaseDepot
{
	int resource;
	int along, across;
	int radius;
};

/// A whole base as designed: its pieces, its depots, which piece is the swarm, and its reach - the
/// Chebyshev half-extent of everything in it, footprints, their walking rings and depots included,
/// so a design knows how much clear ground a base needs round its centre.
struct BasePlan
{
	std::vector<BasePiece> pieces;
	std::vector<BaseDepot> depots;
	int swarm = 0;
	int reach = 0;
};

/// The size of colony a `colonists` value buys. The tiers are wide on purpose: a player chooses the
/// number of colonists and the base grows in three steps, so the map's ground negotiation (which
/// depends on the plan's reach) does not change at every notch.
enum class BaseTier
{
	Hamlet, // 16 to 20 colonists: swarm, one inn, hospital, barracks
	Town,   // 24 to 28: a second inn and a school besides
	City    // 32 to 48: three inns, a racetrack, and towers when asked
};
/// Finished: every piece a completed building. Sites: the swarm and one inn finished, every other
/// piece a level-0 construction site, so the colony's first job is to finish its own city.
enum class BaseKind
{
	Finished,
	Sites
};
BaseTier baseTier(int colonists);

/// The one base layout every premade-base landscape uses, at a tier and in a kind, with `towers`
/// (0 or 2) finished, stocked level-1 towers at its front corners and, with `quarry`, a stone clump
/// depot at its back. A tier drops pieces from the City layout without moving the rest, so a base
/// keeps the same shape at every size and a player learns it once.
BasePlan standardBasePlan(BaseTier, BaseKind, int towers, bool quarry);

/// The units a colony starts with. Workers are the colonists; with the garrison on, three eighths
/// as many level-1 warriors (12 at the default 32) and an eighth as many explorers (4), so the
/// garrison grows with the colony rather than being a fixed squad.
struct BaseGarrison
{
	int workers = 0;
	int warriors = 0, warriorLevel = 1;
	int explorers = 0, explorerLevel = 0;
};
BaseGarrison baseGarrison(int colonists, bool garrison);

/// Where a colony's base stands: the frame's origin tile (the swarm's middle) and a quarter-turn
/// facing, 0 to 3, turned as Canals turns its blocks: facing 1 sends along to +y, 2 to -x, 3 to -y.
struct BaseSite
{
	int x, y, facing;
};

/// The map tile of frame offset (u, v) at a site.
int baseTile(const Torus &, const BaseSite &, int u, int v);

/// A footprint as it lands on the map: its top-left offset from the site and its size, which is the
/// frame size with width and height swapped at odd facings.
struct BaseFootprint
{
	int dx, dy, w, h;
};
BaseFootprint baseFootprint(const BaseSite &, int along, int across, int w, int h);

/// Whether a plan fits at a site: every footprint wholly on `buildable` (pure grass with nothing on
/// it, as pureTiles of a sketch reports it), no two footprints sharing a tile, and every tile of the
/// one-tile ring round each footprint on `open` ground (land, so workers can walk round every
/// building) and on no other footprint - so every two buildings keep a gap of at least one tile,
/// which is also what keeps a unit from ever being walled in between them. Depot tiles must be open
/// and off every footprint. A design calls this on its sketch before it commits to a site.
bool basePlanFits(const Torus &, const BasePlan &, const BaseSite &,
				  const std::vector<unsigned char> &buildable,
				  const std::vector<unsigned char> &open);
/// The same test, naming what did not fit ("" when everything does): the piece whose footprint
/// is off buildable ground or on another's, the piece whose walking ring is closed, or the depot
/// with no room - so a design's telemetry says why a site was rejected.
std::string basePlanMisfit(const Torus &, const BasePlan &, const BaseSite &,
						   const std::vector<unsigned char> &buildable,
						   const std::vector<unsigned char> &open);

/// The footprint tiles of every site's pieces: the ground later layers keep clear of deposits and
/// roads, as towerFootprints is.
std::vector<unsigned char> baseFootprints(const Torus &, const BasePlan &,
										  const std::vector<BaseSite> &);
/// The footprints, their walking rings and the depots' tiles together: the ground a kit and the
/// ambient layers leave alone so the base stays walkable.
std::vector<unsigned char> baseSurroundings(const Torus &, const BasePlan &,
											const std::vector<BaseSite> &);

/// Raises one colony's base and puts its colonists down. For every piece in plan order (the swarm
/// first): checkRoomForBuilding, addBuilding (a finished type complete, otherwise a level-0 site),
/// stock as the piece asks. Then the garrison (below), then the colony's one Team::createLists, then
/// the colony's start position and context.bootX/Y from the swarm. `withinLabel`, when given, is a
/// per-tile label and units stand only where it equals `team` (a compound's interior); nullptr lets
/// them stand anywhere free. False, with context.detail "Colony k: ...", on the first piece that no
/// longer fits or unit that has no tile, and nothing is rolled back: the candidate is discarded.
bool raiseBase(Game &, GenerationContext &, int team, const BasePlan &, const BaseSite &,
			   const BaseGarrison &, const std::vector<int> *withinLabel, const char *stream);
/// raiseBase for every colony in team order; false on the first that fails.
bool raiseBases(Game &, GenerationContext &, const BasePlan &, const std::vector<BaseSite> &,
				const BaseGarrison &, const std::vector<int> *withinLabel, const char *stream);

/// Puts a colony's units down round its swarm: workers on the free tiles touching the swarm's
/// footprint (the ring placeSettlement uses), then a ring further out at a time until every worker
/// stands, drawn from `stream` within each ring so the same seed lands the same way; warriors of
/// `warriorLevel` the same way after them; explorers over the free air nearest the swarm's middle.
/// Only tiles a ground unit can stand on (isFreeForGroundUnit) inside `withinLabel == team` (or
/// anywhere with nullptr) are used. Returns the outermost ring it needed, or -1 when some unit found
/// no tile within kGarrisonReach rings.
int garrison(Game &, GenerationContext &, int team, int swarmGid, const BaseGarrison &,
			 const std::vector<int> *withinLabel, const char *stream);
/// How far out from the swarm the garrison may spill: 12 rings (a 28x28 square) seats 48 workers,
/// 18 warriors and their gaps many times over on open ground; a base that cannot seat its colonists
/// within it is not the base that was designed, and fails.
constexpr int kGarrisonReach = 12;

/// Plants a plan's depots at a site: a clump of each depot's resource on the terrain as written
/// (placeResourceClump respects buildings and units, so it runs after raiseBase).
void plantBaseDepots(Map &, GenerationContext &, const Torus &, const BasePlan &, const BaseSite &);

/// A validator's proof that a colony's base stands as planned: for every piece a building of that
/// type name, level and finished-ness on exactly its footprint, and exactly `workers` WORKER units
/// in the colony. "" when it does, else which colony lost what.
std::string validateBase(const Game &, const Torus &, int team, const BasePlan &, const BaseSite &,
						 int workers);
} // namespace MapGeneration
