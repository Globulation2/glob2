// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
struct GenerationContext;
#include "Geometry.h"
#include "FertilityField.h"
#include <string>
#include "Grid.h"
#include "Sketch.h"
#include <utility>
#include <vector>
class Map;
struct GenerationContext;
namespace MapGeneration
{
// Farmland: ground laid out like a real farm, in long rows of crops with a strip of water between
// every two rows and only the beach the engine insists on in between. Wheat and wood regrow on a tile
// only when a random probe up to 15 tiles away lands on water and the opposite probe does not land
// on sand (Map::growResources), so a crop row next to water yields steadily, and a farm's yield is
// set by how wide its crop and water rows are.
//
// A farm is built in three steps a generator composes with its own walls: growFarmFields shares open
// ground out into fields joined to their homes (by equal yield for their rows' angles), layFarm lays
// the rows over a field, and plantFarm plants them once the terrain is written. Around a laid farm,
// sand does three jobs: a cap ring closes the crop rows so wheat and wood never spread into the ground
// round the farm; bridges cross the whole farm, water rows and crop rows alike (FarmBridges), so workers need not walk
// round a long row nor cut through one; and a ring round the building plot keeps crops off it. Walls
// round a farm are the map's business, not the farm's.

/// An irregular grass plot surrounded by sand. `corners` is an arbitrary, nonempty set of
/// undermap vertices; a Chebyshev margin seals diagonal growth across the wrap.
/// The default two rows preserve existing gardens; one row is a thinner, still sealed bund.
/// margin must be positive.
/// Returns the pure grass tiles inside it, suitable for a crop eligibility list. The caller must
/// reserve the plot AND its margin before stamping: this operation intentionally overwrites terrain.
/// Run beaches afterwards and check final tiles if later stages can overlap the plot.
std::vector<int> stampContainedPlot(TerrainSketch &, const Torus &,
									const std::vector<int> &corners, int margin = 2);

/// Plant up to `wanted` deposits in a contained plot, preferring exact crop fertility (tile index
/// breaks ties). With renewable=true, dry tiles are excluded; false also serves finite dry groves
/// and quarries. Returns actual deposits planted, so essential shortfalls can fail explicitly.
int plantContainedPlot(Map &, const Torus &, const std::vector<int> &tiles,
					   const Fertility::Field &, int resource, int wanted, bool renewable = true);

/// Prove on final terrain that eight-neighbour grass growth cannot leave or join differently
/// labelled plots. `plotOf` labels pure grass tiles (-1 outside); also reject wheat/wood planted
/// outside them, except a tree on a tile whose crop growth chance in `dry` is zero (the engine's
/// water probe never lets it spread). Empty means sealed. Does not assume a particular outline or
/// sand graphic.
std::string containedPlotsMismatch(const Map &, const Torus &, const std::vector<int> &plotOf,
								   const Fertility::Field *dry = nullptr);

/// The widths of a farm's crop rows and water rows, measured across the rows in tiles.
struct FarmRows
{
	double crops, water;
	double period() const { return crops + water; }
};

/// The row widths that make a farm yield most for its area, for rows running at `angle` (radians).
/// Wider rows hold more crops but more of them sit far from water; narrower ones lose a larger share
/// of their ground to beaches. tools/farm_row_fit.py sums every crop tile's exact chance of regrowing
/// for every width pair: rows along an axis do best at 10 tiles of crops and 8 of water, and diagonal
/// rows, whose stepped edges lose more tiles to the beach, at 12 and 9. The optimum is flat (the next
/// best pairs are within 2%), so this is a rough line between the two through the sine of twice the
/// angle, which the fit follows closely by 22 degrees.
FarmRows bestFarmRows(double angle);

/// How much a farm yields per tile of its area, laid at bestFarmRows for rows running at `angle`: every
/// crop tile's chance of regrowing each time the engine visits it, summed and divided by the area, as
/// tools/farm_row_fit.py measures it at 0, 11.25, 22.5, 33.75 and 45 degrees from an axis (0.149 down
/// to 0.113) and interpolated between. Rows along an axis yield about a third more per tile than
/// diagonal rows, whose stepped edges lose more tiles to the beach, so two farms given the same yield
/// need areas in the inverse ratio.
double farmYield(double angle);

/// A farm laid over a region: which undermap vertices are water, and for every vertex of the region
/// the row it lies in (even rows are crops, odd rows water; -1 outside the region).
struct Farm
{
	std::vector<unsigned char> water;
	std::vector<int> row;
	int rows = 0; // how many water rows the region holds
	// Every sand vertex the farm laid (its cap ring, its bridges and its plot's ring); the plot's
	// pure-grass tiles, when one was asked for and fits, and the top-left of those tiles (-1 without).
	std::vector<unsigned char> sand, plot;
	int plotX = -1, plotY = -1;
};

/// A low-harmonic wobble of one hill's contours: at each heading the bands shift in or out by up
/// to `amplitude` tiles, the same shift for every band so their widths hold, ramped in past the
/// summit's cap so the summit and its cap stay round. Three harmonics (two, three and five waves
/// round the hill) with their own phases make a hill lobed rather than circular, and the ramp keeps
/// the mapping monotone along every ray so no contour folds back on itself.
struct ContourWobble
{
	double amplitude = 0;
	double phase[3] = {0, 0, 0};
	double at(double angle) const;
};

/// Contour rows around a central clearing, circular or wobbled. Widths are undermap CORNERS, as
/// in layFarm; beaches and four-corner conversion consume crop ground at every boundary.
/// The inner and outer caps contain eight-neighbour crop spread. Every radial crossing
/// cuts BOTH crop and water rows, keeping circulation open after crops fill the bands.
struct ContourFarmStyle
{
	FarmRows rows{10, 8};
	double innerRadius = 16, cap = 2;
	int bands = 1, crossings = 3;
	double crossingHalfWidth = 2.5, phase = 0;
	std::vector<ContourWobble> wobbles; // one per centre; empty (or none for a centre) is a circle
	double outerRadius() const { return innerRadius + 2 * cap + bands * rows.period(); }
	/// The farthest any band reaches from a centre once the wobble is counted.
	double reach() const;
};

/// The radius the band arithmetic sees for a point `distance` from centre `centre` at `angle`:
/// the distance itself inside the summit's cap and for a centre without a wobble, the distance
/// shifted by the centre's wobble beyond it.
double contourNominal(const ContourFarmStyle &, size_t centre, double distance, double angle);

struct ContourFarm
{
	Farm farm;
	// Crossing corners only, separate from the caps in farm.sand. Callers use these to
	// preserve roads, score defenses or verify every crossing's finished walkable core.
	std::vector<unsigned char> crossings;
};

/// Stamps complete concentric crop/water bands at each centre into an existing sketch.
/// Central clearings and ground outside the outer cap are untouched. All centres use
/// the same style, so one farm mask can be planted with per-region eligibility policies.
/// Centres are whole-corner positions; distances wrap on the torus. The caller budgets
/// non-overlapping outer discs (including their copies across a seam) and supplies
/// positive row widths, cap, band count and crossing width. Crossings may be zero.
/// No beach pass is run: compose other terrain first, then call layBeaches once.
/// Like layFarm, this creates geometry only; plantFarm stocks the finished grass later.
ContourFarm layContourFarm(TerrainSketch &, const Torus &, const std::vector<ShapePoint> &centres,
						   const ContourFarmStyle &);

/// A clearing in the middle of a farm for buildings: `width` by `height` tiles of pure grass with a
/// ring of sand `ring` undermap vertices wide round it (two vertices make a full tile of sand), so
/// no crop grows onto it and nothing but the clearing is buildable. 10 by 4 seats a swarm or an inn
/// with room to walk round it.
struct FarmPlot
{
	int width = 10, height = 4, ring = 2;
};

/// Half the width, in vertices along the rows, of a bridge's sand line: wide enough that a diagonal line
/// stays unbroken.
constexpr double kBridgeHalfWidth = 0.75;

/// A farm's sand bridges: a line of sand every `spacing` tiles along its rows (0 lays none), over the
/// water rows (`water`) and through the crop rows (`crops`). Over water a bridge is a crossing; through
/// crops it is a lane no crop grows over, so a filled row is never a wall a worker must cut through or
/// walk the length of. Both are on by default, on every map that lays farm rows, and each is a
/// player control (waterCrossingsControl, cropCrossingsControl) so a map can be played with either off.
struct FarmBridges
{
	int spacing = 0;
	bool water = true, crops = true;
	/// Whether a bridge line is laid over a vertex in a water row (`waterRow`) or a crop row.
	bool crosses(bool waterRow) const { return spacing > 0 && (waterRow ? water : crops); }
};

/// The two toggles every farm-row map offers, both on by default: "water-crossings" switches the
/// bridges over its water rows and "crop-crossings" the lanes through its crop rows.
GeneratorControl waterCrossingsControl();
GeneratorControl cropCrossingsControl();

/// The least a field is opened by (growFarmFields): a beach and a wall each side fill any strip narrower
/// than twice this. A map with a wider rim round its rows passes that rim instead.
constexpr int kFarmOpening = 3;

/// Lays rows over `region` running at `angle` (radians), with the row at `origin` a crop row centred on
/// it, and no water within `rim` steps of the region's edge, so the region's own coast (or wall) keeps a
/// margin of land and every crop row joins the rim at both ends. Then, in order:
///  - with `caps` (on), a ring of sand vertices just inside the rim, where the water rows' beaches begin,
///    closes every crop row, so its wheat and wood never spread out of the farm;
///  - with a `bridges.spacing`, a line of sand vertices crosses the whole farm inside its cap every that
///    many tiles along the rows, starting at the origin: over the water (with `bridges.water`) the tiles
///    either side are no longer pure water, so workers walk across, two tiles wide, and through the
///    crops (with `bridges.crops`) the line is a lane no crop grows over, so the farm is cut into bays a
///    worker walks round without clearing anything;
///  - with a `plot`, a building clearing is stamped at the region's most inland point, clear of every
///    water row by its sand ring and a vertex more and at least `rim` from the edge (a region too small
///    for it gets none); its grass always wins over a cap or bridge running through it.
/// Stamps the water and the sand into `sketch`; lay beaches afterwards as usual, keeping the farm's sand
/// (`Farm::sand`) out of any beach flood, as a sand road is.
/// If requested, edgeDepthOut receives the computed distance to the region exterior (0 outside,
/// -1 when there is no exterior). Reuse it for planting clearance instead of repeating the flood.
Farm layFarm(TerrainSketch &sketch, const Torus &, const std::vector<unsigned char> &region,
			 double angle, ShapePoint origin, int rim, const FarmRows &rows,
			 const FarmPlot *plot = nullptr, const FarmBridges &bridges = {}, bool caps = true,
			 std::vector<int> *edgeDepthOut = nullptr);

/// Stamps one building plot with its top-left grass tile at (x0, y0) into a laid farm: the plot's
/// grass tiles win over any water row, bridge or cap running through them, and its ring of sand
/// closes them off from the crops. layFarm places one plot at a region's most inland point; a map that
/// wants plots all over its fields (Old town's farm hubs) stamps them itself, after checking the
/// fit it needs. Marks the plot's tiles in `farm.plot` and its sand in `farm.sand`.
void stampFarmPlot(TerrainSketch &sketch, const Torus &, Farm &farm, int x0, int y0,
				   const FarmPlot &);

/// Farm fields grown into open ground straight out of the homes they belong to. `occupied` marks all the
/// designed land and `homeOf` its owner where it is a home (0 or more; -1 elsewhere). Field `s` belongs
/// to home `owners[s]` and starts from the open tile near `seeds[s]` (searched 16 tiles round it) in the
/// biggest stretch of open ground. In order:
///  - every open tile that `area` allows is shared out between the fields by growth (growTerritories):
///    by equal area, or, given every field's `rowAngles`, by equal yield (farmYield), so a field whose
///    rows run on the diagonal gets more ground than one whose rows run along an axis;
///  - every field keeps `gap` tiles from all designed land but its own home, and from every other field
///    (separateTerritories), while it may run right up to its own home, so the two are one piece of land;
///  - a field keeps only the ground whose core, the field and its home shrunk by `opening` tiles, is
///    joined to the home's core, grown back out as far as the field went: any strip narrower than
///    2 * opening + 1, and any part joined to the rest only through such a strip, goes. The opening is
///    the rim a map keeps between a farm's rows and its coast (a beach, a coast wall and the cap), so
///    what goes is ground that would have been all rim, with no crop row in it - and where a strip
///    joins the rest of a field through an isthmus, the walls of the two coasts either side would meet
///    across it and seal the strip off (Switchbacks at 128x256 lost a fifth of a farm that way);
///  - every field is joined to its home by a neck `neckHalfWidth` wide from the home's middle (`anchors`)
///    to the field's nearest ground, keeping the same gaps, so the opening is always broad.
/// Returns every tile's field (the seed's index) or -1. A seed with no open ground near it gets no field,
/// so a caller checks every field's size before using them.
std::vector<int>
growFarmFields(const Torus &, const std::vector<unsigned char> &occupied,
			   const std::vector<int> &homeOf, const std::vector<unsigned char> &area,
			   const std::vector<ShapePoint> &seeds, const std::vector<int> &owners,
			   const std::vector<ShapePoint> &anchors, int gap, double neckHalfWidth,
			   const std::vector<double> &rowAngles = {}, int opening = kFarmOpening);

/// How much of a farm a colony can work: the share of the farm's crop-row grass (and the whole of its
/// plot, if any, or 0 is returned) that a unit from `sources` can walk to once crops are cleared -
/// water, stone and buildings block, wheat and wood do not. A validator's proof that a farm is joined
/// to its home.
double farmReachable(const Map &, const Torus &, const Farm &, const std::vector<int> &sources);

/// Clears any deposit a later layer left on a farm's building plot, so every plot stays buildable. Run
/// it after the last deposits go down.
void clearFarmPlots(Map &, const Torus &, const std::vector<Farm> &);

/// Plants a laid farm, once the terrain is written. A farm is a wheat farm: `wheat` tiles go on the
/// crop rows' pure grass that `eligible` allows, nearest the water first (where it regrows best), and
/// `wood` tiles make one small woodlot along a single crop row, the one with the most room, nearest its
/// water. Returns how many tiles were planted.
template <typename Eligible>
int plantFarm(Map &map, const Torus &t, const Farm &farm, int wheat, int wood, Eligible eligible);

/// A sealed oval garden against a shore or a wall (Central Quarry's isle, Hidden Oasis' basin): a
/// holder's foothold of wheat and wood that can never spread over the building ground round it.
/// The plot is an oval `stretch` times as long across `angle` as along it, centred on (x, y), which
/// a caller puts on the rim of its ground so the rim's own beach or rock closes the far half. A line
/// of sand corners `sealWidth` wide closes the near half, its radius swaying by up to `sway` tiles on
/// `swayNoise` (a round plot with a ruled ring read as a bullseye).
struct SealedOval
{
	double x = 0, y = 0, angle = 0, radius = 4, stretch = 1.6, sealWidth = 1.2, sway = 0.6;
};
/// Lays the seal into the sketch on the corners of `ground`, and returns the garden's tiles inside it.
std::vector<unsigned char> stampSealedOval(TerrainSketch &, const Torus &,
										   const std::vector<unsigned char> &ground, const SealedOval &,
										   const std::vector<int> &swayNoise);
/// Plants a sealed garden on the written map: up to `wheat` wheat (at most half its clear tiles) and
/// `wood` wood (at most two fifths), dealt over the plot by noise rather than grown as two blobs.
/// Draws a noise field from `cropsStream`, then one from `splitStream`. Returns {wheat, wood} tiles
/// standing in the garden afterwards.
std::pair<int, int> plantSealedGarden(Map &, const Torus &, GenerationContext &,
									  const std::vector<unsigned char> &garden, int wheat, int wood,
									  const std::string &cropsStream, const std::string &splitStream);

/// Crops regrow only within the engine's square growth probe of water, and kits plant the most fertile
/// ground first, so fields end in ruler-straight lines along the probe's square contours. This takes
/// off every wheat and wood deposit on watered ground farther than a round distance from pure water,
/// `leastReach` to `leastReach + reachSpread` tiles by `noise`, so fields round a pond are round.
/// Tiles of `keep` (the fields round a home) are never touched. Returns how many it took.
int trimFieldsBeyondWater(Map &, const Torus &, const std::vector<unsigned char> &keep,
						  const Fertility::Field &watered, int leastReach, int reachSpread,
						  const std::vector<int> &noise);
/// Frays every field's edge: wheat and wood up to `depth` tiles in from a field's edge are taken off
/// where `noise` says so, so every edge wanders. Never on `keep`. Returns how many it took.
int frayFieldEdges(Map &, const Torus &, const std::vector<unsigned char> &keep, int depth,
				   const std::vector<int> &noise);
/// Where trails and routes cut through fields they leave one-tile strips of crop between two lanes. In
/// two passes, every wheat or wood tile open (clear grass) on two opposite sides goes, except on
/// `protect` (the kits). Returns how many went.
int removeCropSlivers(Map &, const Torus &, const std::vector<unsigned char> &protect);
} // namespace MapGeneration

#include "Map.h"
#include "Resources.h"
#include <algorithm>
#include <utility>
template <typename Eligible>
int MapGeneration::plantFarm(Map &map, const Torus &t, const Farm &farm, int wheat, int wood,
							 Eligible eligible)
{
	const std::vector<int> fromWater = stepsFrom(t, farm.water);
	std::vector<std::pair<int, int>> crops;
	std::vector<int> rowRoom;
	for (int i = 0; i < t.size(); ++i)
		if (farm.row[i] >= 0 && farm.row[i] % 2 == 0 && fromWater[i] >= 0 && !farm.plot[i] &&
			eligible(i) && map.isGrass(i % t.w, i / t.w))
		{
			crops.push_back({fromWater[i], i});
			if (farm.row[i] >= int(rowRoom.size()))
				rowRoom.resize(farm.row[i] + 1, 0);
			++rowRoom[farm.row[i]];
		}
	std::stable_sort(crops.begin(), crops.end());
	// The woodlot's row: the crop row with the most room, the first on a tie.
	const int woodRow =
		rowRoom.empty() ? -1
						: int(std::max_element(rowRoom.begin(), rowRoom.end()) - rowRoom.begin());
	int planted = 0, woods = 0, wheats = 0;
	for (const auto &entry : crops)
	{
		const int i = entry.second;
		if (woods < wood && farm.row[i] == woodRow)
		{
			map.setResource(i % t.w, i / t.w, WOOD, 1);
			++woods;
			++planted;
		}
	}
	for (const auto &entry : crops)
	{
		const int i = entry.second;
		if (wheats >= wheat)
			break;
		if (map.isResource(i % t.w, i / t.w))
			continue;
		map.setResource(i % t.w, i / t.w, WHEAT, 1);
		++wheats;
		++planted;
	}
	return planted;
}
