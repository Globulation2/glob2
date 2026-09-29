// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlacisGenerator.h"
#include "Contact.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "BuildingType.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Room.h"
#include "Routes.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Towers.h"
#include "Walls.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// The Glacis: star forts in open country.
//
// WHAT IT LOOKS LIKE. A bastioned star fort seen from the air, the way Vauban built them: an
// angular stone trace of arrowhead bastions joined by short curtains; a water moat following every
// salient and re-entrant of it; beyond the moat a pale covered way; and then the glacis itself, a
// broad ring of bare, cleared grass sloping away from the works, edged with a sand foot, that
// stands out against the woods and fields of the countryside around. Sand tracks leave the gates
// over causeways and cross the glacis to the country roads. Between the forts run meandering
// streams, and where a stream parts two forts' countryside the road crosses it at a ford. (The
// first version drew square compounds on a plain of bare grass with ruler-straight wadis, and
// nothing read as a fortress: the glacis cannot be seen without the country it is cleared from.)
//
// HOW IT PLAYS. Every colony starts with an ordinary swarm and workers in the courtyard of its
// own fort. Two bastions at the back are its gardens: pockets of grass inside the wall, watered by
// the moat a few tiles beyond it and closed off from the courtyard by a sand gorge line, so the
// opening wheat and wood regrow and never spread over the town. The courtyard holds a small town,
// not a city: growth means leaving through the gates. The glacis is buildable and nothing is
// planted on it, and the sand of the covered way and the foot keep the country's crops from ever
// growing onto it, so a forward inn or tower there is a deliberate move made in the open, next to
// the enemy's works. The country between the forts is where the wealth is - forests, fields, stone
// outcrops, and at every contested ford an orchard of the three fruits and a quarry on both banks -
// and the fords are where neighbours meet. The remaining bastions, those flanking the gates, are
// the tower positions; the starting-towers control puts completed towers there.
//
// FAIRNESS. The forts stand on a lattice (Orbits.h) and every fort is one stencil stamped by whole
// quarter turns, which carry tiles exactly onto tiles, so every colony's walls, moat, glacis,
// gardens and courtyard are identical. The country between them is noise, shared out by nearest
// fort; its streams follow the boundaries between neighbours and its fords are the midpoints of
// those boundaries, so every contested ford is about as far from both forts that share it. The
// lobby's best-of-five start scoring covers what the noise leaves uneven.
//
// FAILURE MODES this design watches for: a beach eating a wall tile (the berm between wall and
// moat is wide enough that it cannot, and design() proves it); crops spreading onto the glacis or
// out of a garden (validateWorld floods the finished grass to prove they cannot); a fort closed
// off (pieceLeak with the gates shut, then every colony walked to from the first); forts too
// close (the glacis narrows first, then the fort, then the map is refused).
namespace
{
// The trace, as shares of the bastion tip radius and of half a bastion's sector (pi / N): the
// curtain's corners (the flank bases), the shoulders where the faces meet the flanks, and the tip.
// Tuned on previews and a Nicowar game (2026-09-16): a slender trace (curtain at 0.56 of the tip)
// read best but left a courtyard too small to grow a town in (half Forts' wheat harvest in 30,000
// ticks); these fatter ones still read as bastions. Fewer bastions need a smaller curtain so the
// arrowheads still project.
struct TraceShape
{
	double curtain, shoulder, flankAngle, shoulderAngle;
};
TraceShape traceShape(int bastions)
{
	if (bastions <= 4)
		return {0.62, 0.82, 0.60, 0.42};
	if (bastions == 5)
		return {0.66, 0.84, 0.55, 0.40};
	return {0.68, 0.85, 0.52, 0.38};
}
// The works outwards from the trace, as distances from it in tiles: the wall (inside the trace),
// the berm of grass between wall and moat (wide enough that the moat's beach never reaches a wall
// tile, since stone stands on pure grass only), the moat's water, the covered way's sand, then the
// glacis of `glacis-width` tiles and its one-tile sand foot.
constexpr double kWall = 2.2, kBerm = 2.5, kMoat = 3, kCovered = 2, kFoot = 1;
// Tiles beyond the foot that belong to the fort's zone: no stream, road or deposit, so a track
// meets the foot square and the country's crops start a few tiles out.
constexpr double kZoneMargin = 2;
// Gates are three tiles wide through the wall; the causeway over the moat five vertices; the track
// over the covered way, glacis and foot three.
constexpr double kGateHalf = 1.5, kCausewayHalf = 2.0, kTrackHalf = 1.0;
// The kitchen gardens: every courtyard tile behind a line this far back of the fort's middle is
// garden, closed off from the town in front by one row of sand corners, and split into a wheat half
// and a wood half by a sand lane as wide as the back gate running from it to the garden line (so
// the gate, too, opens on sand and not on garden). Each half has a cistern, a
// pond of water corners within this radius of its middle, so the gardens regrow (the moat alone,
// five tiles beyond the wall, left the first design's bastion gardens food-capped at 28 units in a
// 30,000-tick Nicowar game).
constexpr double kGardenFront = 1.5, kCisternRadius = 2.4;
// Negotiation floors: a glacis narrower than 4 no longer reads as one, and a fort under 20 has no
// courtyard left; the country between two forts' zones needs room for a stream and its banks.
constexpr int kLeastGlacis = 4, kLeastFort = 20, kCountryBetween = 16;
// The gardens' crops: the wheat half is planted to this share of its ground and the wood half to
// this, around the cisterns, scaled by the amounts, never below the unscaled floors that feed an
// opening. A half must keep this much pure grass once its sand and beaches are laid.
constexpr int kGardenWheatPercent = 75, kGardenWoodPercent = 45, kGardenWheatFloor = 40,
			  kGardenWoodFloor = 20, kLeastGardenTiles = 40, kLeastWoodGardenTiles = 24,
			  kDefaultFort = 30, kLeastThreeGardens = 26;
// The wood garden is the outer part of the second half (its bastion), beyond an arc of sand at this
// share of the curtain's inner radius; the rest of that half is a second wheat garden. (Equal wheat and wood halves
// fed 34 to 42 Nicowar colonists and then starved them; wood was never short.)
constexpr double kWoodArc = 0.98;
// Streams wander this far from the boundary between two forts' country, over a noise field; their
// water is a stroke three vertices wide. A stream boundary shorter than this is a corner where
// three forts meet, not a front, and gets no stream.
constexpr int kLeastBoundary = 24;
// A stream's course: the boundary walk displaced sideways by two sine waves along it, of a long
// and a short wavelength drawn per stream, tapered to nothing at its ends so streams still meet
// where three forts' country does. Its water is a stroke this wide.
constexpr double kMeanderLong = 6, kMeanderShort = 1.5, kStreamHalfWidth = 1.4;
// Ponds in the country, one per this many tiles of it, each this many water corners, well away
// from the forts and the streams: the water that makes fields out in the country.
constexpr int kCountryTilesPerPond = 1800, kPondCorners = 40, kPondClearance = 9;
// The contested ford is a 5x5 of sand at a stream's middle; its prizes stand this far out on both
// banks: three fruit groves along the bank, a quarry behind them.
constexpr int kFordHalf = 2, kPrizeOut = 8, kQuarryOut = 12, kGroveSpacing = 4, kGroveTiles = 4;
// The country: forest on the woodiest share of it, then fields, outcrops and groves.
constexpr int kForestPercent = 30, kFieldTilesPer = 16, kOutcropTilesPer = 700, kGroveTilesPer = 1100;
// Towers: two open pads per colony in its tower bastions, four tiles apart.
constexpr int kTowerPads = 2, kTowerSpacing = 4;

enum TileKind : signed char
{
	kOutside,
	kCourt,
	kPocket, // inside a front bastion, beyond its chord: tower ground
	kGarden, // behind the garden line
	kWallTile,
	kGateTile,
	kBermTile,
	kMoatTile,
	kCoveredTile,
	kGlacisTile,
	kFootTile
};

double segmentDistance(double px, double py, double ax, double ay, double bx, double by)
{
	const double dx = bx - ax, dy = by - ay;
	const double len2 = dx * dx + dy * dy;
	const double s = len2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0, 1.0) : 0;
	return std::hypot(px - ax - s * dx, py - ay - s * dy);
}

// One fort in its own frame, stamped at every colony by quarter turns. Vertices and tiles are
// stored separately: a vertex at offset (dx, dy) is the frame point (dx, dy), a tile the point
// (dx + 0.5, dy + 0.5), so a quarter turn about the origin vertex carries both grids onto
// themselves exactly.
struct FortStencil
{
	int extent = 0; // offsets run from -extent to extent
	int bastions = 5;
	double tip = 0, glacis = 0;
	std::vector<unsigned char> vertex; // TerrainType
	std::vector<signed char> kind;     // TileKind of each tile
	std::vector<signed char> half;     // for kGarden tiles, the garden: 0 and 2 wheat, 1 wood
	std::vector<unsigned char> zone;   // tiles within the fort's zone (no country features)
	std::vector<unsigned char> core;   // tiles out to the foot (roads keep off)
	// Gardens: 0 and 2 are wheat, 1 is wood.
	std::array<ShapePoint, 3> cisterns{};  // frame points, by garden (the wood garden's is a seed)
	std::array<int, 3> gardenTiles{};      // pure grass in each garden, beaches laid
	bool threeGardens = true;
	ShapePoint swarm{};                    // frame point
	std::vector<ShapePoint> gateEnds;      // frame points just beyond each gate's foot
	int side() const { return 2 * extent + 1; }
	int index(int dx, int dy) const { return (dy + extent) * side() + dx + extent; }
};

FortStencil buildStencil(int bastions, double tip, double glacis, int gates)
{
	FortStencil s;
	s.bastions = bastions;
	s.tip = tip;
	s.glacis = glacis;
	const TraceShape shape = traceShape(bastions);
	const double half = kPi / bastions;
	std::vector<ShapePoint> trace;
	std::vector<double> bastionAngle;
	for (int b = 0; b < bastions; ++b)
	{
		const double a = half + 2 * kPi * b / bastions;
		bastionAngle.push_back(a);
		const double pts[5][2] = {{shape.curtain, -shape.flankAngle},
								  {shape.shoulder, -shape.shoulderAngle},
								  {1, 0},
								  {shape.shoulder, shape.shoulderAngle},
								  {shape.curtain, shape.flankAngle}};
		for (const auto &p : pts)
			trace.push_back(polarPoint(0, 0, p[0] * tip, a + p[1] * half));
	}
	const auto signedDistance = [&](double px, double py)
	{
		double nearest = 1e9;
		bool in = false;
		const size_t n = trace.size();
		for (size_t i = 0; i < n; ++i)
		{
			const ShapePoint &a = trace[i], &b = trace[(i + 1) % n];
			nearest = std::min(nearest, segmentDistance(px, py, a.x, a.y, b.x, b.y));
			if ((a.y > py) != (b.y > py) && px < a.x + (py - a.y) * (b.x - a.x) / (b.y - a.y))
				in = !in;
		}
		return in ? -nearest : nearest;
	};
	// Gates open in the curtains facing the four ways out, in the order front, back, left, right, as
	// many as asked: the country roads run to the fords between neighbouring forts, which lie along
	// the lattice's axes, so every road leaves its fort straight out of a gate.
	std::vector<double> gateAngles;
	std::vector<int> gateCurtains;
	for (double target : {0.0, kPi, kPi / 2, 3 * kPi / 2})
	{
		if (int(gateCurtains.size()) >= gates)
			break;
		int best = -1;
		double nearest = 1e9;
		for (int c = 0; c < bastions; ++c)
		{
			const double off = std::abs(std::remainder(2 * kPi * c / bastions - target, 2 * kPi));
			if (std::find(gateCurtains.begin(), gateCurtains.end(), c) == gateCurtains.end() &&
				off < nearest - 1e-9)
			{
				nearest = off;
				best = c;
			}
		}
		gateCurtains.push_back(best);
		gateAngles.push_back(2 * kPi * best / bastions);
	}
	// The garden lane splits the gardens in two on the fort's true symmetry axis, always exactly
	// kPi: bastion b's tip at half + 2*kPi*b/bastions always mirrors bastion (bastions-1-b)'s about
	// the front-back line, for any bastion count. A curtain sits exactly on that axis only for an
	// even bastion count (kPi is then also the back gate's curtain, so the lane still runs to its
	// gate); an odd count has no curtain there, and snapping the axis to the nearest one instead
	// (36 degrees off, for five bastions) threw the two rear gardens out of balance badly enough
	// that one fell under its floor on every roll: five-pointed forts failed to generate at all on
	// default settings (garden tiles 319/58/14 against a 40-tile floor).
	const double back = kPi;
	const double court = shape.curtain * tip - kWall;
	// The cisterns shrink with a fort shrunk to fit a crowded map, so their beaches leave it gardens.
	const double cistern = std::clamp(kCisternRadius * court / 18, 1.6, kCisternRadius);
	// A cistern in each wheat garden, towards the back curtain, off the lane to its own side (garden 0 lies on the lane's left, (-sin, cos) of the way back); the wood
	// garden's seed out beyond the arc.
	const auto backFrame = [&](double along, double across)
	{
		return ShapePoint{along * std::cos(back) - across * std::sin(back),
						  along * std::sin(back) + across * std::cos(back)};
	};
	// A fort shrunk below kLeastThreeGardens has no room for the arc: its whole second half is wood.
	s.threeGardens = tip >= kLeastThreeGardens;
	// The cisterns stand at the back of their gardens, so the wheat between the garden line and
	// the water is one unbroken block (see the planting below).
	s.cisterns = {backFrame(0.7 * court, 0.4 * court),
				  s.threeGardens ? backFrame(0.85 * court, -0.75 * court)
								 : backFrame(0.7 * court, -0.4 * court),
				  backFrame(0.7 * court, -0.4 * court)};
	s.swarm = {0.35 * court, 0};

	const double glacisFrom = kBerm + kMoat + kCovered, footTo = glacisFrom + glacis + kFoot;
	s.extent = int(std::ceil(tip + footTo + kZoneMargin)) + 2;
	const int side = s.side();
	s.vertex.assign(size_t(side) * side, GRASS);
	s.kind.assign(size_t(side) * side, kOutside);
	s.half.assign(size_t(side) * side, -1);
	s.zone.assign(size_t(side) * side, 0);
	s.core.assign(size_t(side) * side, 0);
	const auto nearGate = [&](double px, double py, double halfWidth)
	{
		for (double g : gateAngles)
		{
			const double along = px * std::cos(g) + py * std::sin(g);
			const double across = -px * std::sin(g) + py * std::cos(g);
			if (along > 0 && std::abs(across) <= halfWidth)
				return true;
		}
		return false;
	};
	// A front bastion's interior beyond the chord between its flank bases: where towers go.
	const auto inFrontBastion = [&](double px, double py)
	{
		for (int b = 0; b < bastions; ++b)
		{
			if (std::cos(bastionAngle[b]) < 0.2 ||
				std::abs(std::remainder(std::atan2(py, px) - bastionAngle[b], 2 * kPi)) >= half)
				continue;
			const ShapePoint a = polarPoint(0, 0, shape.curtain * tip, bastionAngle[b] - shape.flankAngle * half);
			const ShapePoint c = polarPoint(0, 0, shape.curtain * tip, bastionAngle[b] + shape.flankAngle * half);
			const double cross = (c.x - a.x) * (py - a.y) - (c.y - a.y) * (px - a.x);
			const double origin = (c.x - a.x) * (0 - a.y) - (c.y - a.y) * (0 - a.x);
			if ((cross > 0) != (origin > 0))
				return true;
		}
		return false;
	};
	const double backX = std::cos(back), backY = std::sin(back);
	std::vector<unsigned char> lines(size_t(side) * side, 0);
	for (int dy = -s.extent; dy <= s.extent; ++dy)
		for (int dx = -s.extent; dx <= s.extent; ++dx)
		{
			const int at = s.index(dx, dy);
			// The vertex.
			{
				const double px = dx, py = dy, d = signedDistance(px, py);
				unsigned char terrain = GRASS;
				if (d > kBerm && d <= kBerm + kMoat)
					terrain = nearGate(px, py, kCausewayHalf) ? SAND : WATER;
				else if (d > kBerm + kMoat && d <= glacisFrom)
					terrain = SAND;
				else if (d > glacisFrom && d <= footTo - kFoot)
					terrain = nearGate(px, py, kTrackHalf) ? SAND : GRASS;
				else if (d > footTo - kFoot && d <= footTo)
					terrain = SAND;
				else if (d <= 0.3)
				{
					// The garden line across the courtyard, and the lane down the gardens' middle.
					const double along = px * backX + py * backY, across = -px * backY + py * backX;
					if (std::abs(px + kGardenFront) <= 0.6 ||
						(px < -kGardenFront && along > 0 && std::abs(across) <= kCausewayHalf))
						lines[at] = 1;
					if (s.threeGardens && px < -kGardenFront && across < -kCausewayHalf &&
						std::abs(std::hypot(px, py) - kWoodArc * court) <= 0.6)
						lines[at] = 1;
					for (const int h : {0, s.threeGardens ? 2 : 0})
						if (std::hypot(px - s.cisterns[h].x, py - s.cisterns[h].y) <= cistern)
							terrain = WATER;
				}
				s.vertex[at] = terrain;
			}
			// The tile.
			{
				const double px = dx + 0.5, py = dy + 0.5, d = signedDistance(px, py);
				TileKind kind = kOutside;
				if (d <= -kWall)
				{
					if (px < -kGardenFront)
					{
						kind = kGarden;
						const double across = -px * backY + py * backX;
						s.half[at] = across > 0 ? 0
									 : !s.threeGardens || std::hypot(px, py) > kWoodArc * court ? 1
																								: 2;
					}
					else
						kind = inFrontBastion(px, py) ? kPocket : kCourt;
				}
				else if (d <= 0)
					kind = nearGate(px, py, kGateHalf) ? kGateTile : kWallTile;
				else if (d <= kBerm)
					kind = kBermTile;
				else if (d <= kBerm + kMoat)
					kind = kMoatTile;
				else if (d <= glacisFrom)
					kind = kCoveredTile;
				else if (d <= footTo - kFoot)
					kind = kGlacisTile;
				else if (d <= footTo)
					kind = kFootTile;
				s.kind[at] = kind;
				s.zone[at] = d <= footTo + kZoneMargin;
				s.core[at] = d <= footTo;
			}
		}
	// A line vertex is sand unless it would spoil a wall tile (stone stands on pure grass only): the
	// line stops at the wall's inner face, where its last vertex still touches a wall tile, which
	// keeps the gardens sealed.
	for (int dy = -s.extent + 1; dy <= s.extent; ++dy)
		for (int dx = -s.extent + 1; dx <= s.extent; ++dx)
		{
			if (!lines[s.index(dx, dy)] || s.vertex[s.index(dx, dy)] != GRASS)
				continue;
			bool spoils = false;
			for (const auto &[tx, ty] : {std::pair{dx - 1, dy - 1}, {dx, dy - 1}, {dx - 1, dy}, {dx, dy}})
				spoils = spoils || s.kind[s.index(tx, ty)] == kWallTile;
			if (!spoils)
				s.vertex[s.index(dx, dy)] = SAND;
		}
	// A cistern's pond stops short of the wall's inner face the same way: stone stands on pure grass
	// only, so a cistern whose circle reaches within beach range of a wall vertex would drown the
	// wall instead of just its own garden. layBeaches spreads sand to every corner within one step
	// (all eight neighbours) of a water corner, and a tile is pure only once all four of its own
	// corners are, so a water vertex at (dx, dy) can spoil any wall tile with a corner in the 4x4
	// block from (dx-2, dy-2) to (dx+1, dy+1), not just the four tiles it is a corner of itself. The
	// along/across offsets that place a cistern are tuned for the curtain directly behind it; a
	// bastion count whose curtains do not sit at even multiples of a quarter turn (five, for one) can
	// swing a cistern's sideways offset close enough to a neighbouring curtain's wall to reach it, so
	// every cistern vertex is checked here rather than trusted from its offset alone.
	for (int dy = -s.extent + 1; dy <= s.extent; ++dy)
		for (int dx = -s.extent + 1; dx <= s.extent; ++dx)
		{
			if (s.vertex[s.index(dx, dy)] != WATER)
				continue;
			bool spoils = false;
			for (int ty = std::max(-s.extent, dy - 2); ty <= std::min(s.extent, dy + 1) && !spoils; ++ty)
				for (int tx = std::max(-s.extent, dx - 2); tx <= std::min(s.extent, dx + 1) && !spoils; ++tx)
					spoils = s.kind[s.index(tx, ty)] == kWallTile;
			if (spoils)
				s.vertex[s.index(dx, dy)] = GRASS;
		}
	// Where the garden line meets the wall's inner face at a slant, the corners it could not take
	// can leave a diagonal of pure grass between the gardens and the courtyard. Any tile a crop in
	// the gardens could step onto that is not garden becomes wall, until none is left.
	const auto vertexAfterBeach = [&](int vx, int vy)
	{
		const unsigned char v = s.vertex[s.index(vx, vy)];
		if (v != GRASS)
			return v;
		for (int ny = vy - 1; ny <= vy + 1; ++ny)
			for (int nx = vx - 1; nx <= vx + 1; ++nx)
				if (nx >= -s.extent && nx <= s.extent && ny >= -s.extent && ny <= s.extent &&
					s.vertex[s.index(nx, ny)] == WATER)
					return static_cast<unsigned char>(SAND);
		return v;
	};
	const auto pureAt = [&](int dx, int dy)
	{
		return vertexAfterBeach(dx, dy) == GRASS && vertexAfterBeach(dx + 1, dy) == GRASS &&
			   vertexAfterBeach(dx, dy + 1) == GRASS && vertexAfterBeach(dx + 1, dy + 1) == GRASS;
	};
	for (bool sealed = false; !sealed;)
	{
		sealed = true;
		for (int dy = -s.extent + 1; dy < s.extent - 1; ++dy)
			for (int dx = -s.extent + 1; dx < s.extent - 1; ++dx)
			{
				const int at = s.index(dx, dy);
				if (s.kind[at] == kGarden || s.kind[at] == kWallTile || s.kind[at] == kOutside ||
					!pureAt(dx, dy))
					continue;
				bool touches = false;
				for (int ny = dy - 1; ny <= dy + 1 && !touches; ++ny)
					for (int nx = dx - 1; nx <= dx + 1 && !touches; ++nx)
						touches = s.kind[s.index(nx, ny)] == kGarden && pureAt(nx, ny);
				if (touches)
				{
					s.kind[at] = kWallTile;
					sealed = false;
				}
			}
	}
	// Each half's pure grass once the cisterns' beaches are laid: tiles whose four corners are grass
	// and with no water corner in the ring round them.
	for (int dy = -s.extent + 1; dy < s.extent - 1; ++dy)
		for (int dx = -s.extent + 1; dx < s.extent - 1; ++dx)
		{
			const int at = s.index(dx, dy);
			if (s.kind[at] != kGarden)
				continue;
			bool pure = true;
			for (int vy = dy - 1; vy <= dy + 2 && pure; ++vy)
				for (int vx = dx - 1; vx <= dx + 2 && pure; ++vx)
				{
					const bool corner = vx >= dx && vx <= dx + 1 && vy >= dy && vy <= dy + 1;
					const unsigned char v = s.vertex[s.index(vx, vy)];
					pure = corner ? v == GRASS : v != WATER;
				}
			s.gardenTiles[s.half[at]] += pure;
		}
	for (double g : gateAngles)
	{
		double r = 0;
		while (signedDistance(r * std::cos(g), r * std::sin(g)) <= footTo + 0.5)
			r += 0.5;
		s.gateEnds.push_back(polarPoint(0, 0, r + 1, g));
	}
	return s;
}

struct Ford
{
	int tile;
	ShapePoint along; // the stream's direction at the ford, a unit vector
	int a, b;         // the two forts whose country the stream parts
};

struct Layout
{
	Torus t{1, 1};
	FortStencil stencil;
	std::vector<ShapePoint> homes;
	std::vector<int> facings;
	TerrainSketch sketch; // undermap corners
	std::vector<signed char> kind;
	std::vector<int> fortOf;    // colony of every tile in a fort's zone, else -1
	std::vector<int> gardenOf;  // colony * 3 + garden for garden tiles, else -1
	std::vector<unsigned char> core, zone, wall, gate, glacis, roads;
	std::vector<Ford> fords;
	int streams = 0;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const GlacisOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// Sequence named-stream draws in separate statements: argument evaluation order differs
	// between compilers.
	const int bastions =
		o.bastions == 0 ? 4 + int(context.bounded("glacis-bastions", 3)) : o.bastions + 3;
	const int offsetX = int(context.bounded("glacis-layout", std::uint32_t(t.w)));
	const int offsetY = int(context.bounded("glacis-layout", std::uint32_t(t.h)));
	L.homes = latticeSites(t.w, t.h, teams, offsetX, offsetY).sites;
	dealStarts(context, L.homes);
	// One facing for every colony, drawn once per map. A home stencil turned by different quarter
	// turns covers identical tiles, but the AIs scan along the map's axes: with a facing per colony,
	// Numbi colonies on The Glacis grew to 60 in one facing and 20 to 30 in the others (rotation
	// tournaments, 2026-09-16). The same facing makes every home an exact translation of the others.
	const int facing = int(context.bounded("glacis-facing", 4));
	L.facings.assign(teams, facing);
	context.telemetry.measure("glacis.fort.facing", facing);
	context.telemetry.measure("glacis.bastions", bastions);

	// Negotiate the fort into the lattice: the glacis narrows first, then the fort.
	double nearest = std::min(t.w, t.h);
	for (int a = 0; a < teams; ++a)
		for (int b = a + 1; b < teams; ++b)
			nearest = std::min(nearest, siteDistance(t, L.homes[a], L.homes[b]));
	int tip = o.fortSize, glacis = o.glacisWidth;
	const auto reach = [&]
	{ return tip + kBerm + kMoat + kCovered + glacis + kFoot + kZoneMargin; };
	while (2 * reach() + kCountryBetween > nearest)
	{
		if (glacis > kLeastGlacis)
		{
			glacis -= 2;
			context.telemetry.fallback("glacis.glacis.narrowed", "Forts too close for the glacis");
		}
		else if (tip > kLeastFort)
		{
			tip -= 2;
			context.telemetry.fallback("glacis.fort.shrunk", "Forts too close for their size");
		}
		else
		{
			L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
			return L;
		}
	}
	context.telemetry.measure("glacis.fort.tip", tip);
	context.telemetry.measure("glacis.glacis.width", glacis);
	L.stencil = buildStencil(bastions, tip, glacis, o.gates);
	context.telemetry.measure("glacis.garden.wheat-tiles",
							  L.stencil.gardenTiles[0] + L.stencil.gardenTiles[2]);
	context.telemetry.measure("glacis.garden.wood-tiles", L.stencil.gardenTiles[1]);
	// The floors are for the default fort; a fort shrunk to fit a crowded map keeps gardens in
	// proportion to its courtyard, never below a quarter of them.
	const double scale = std::max(0.3, double(tip * tip) / (kDefaultFort * kDefaultFort));
	if (L.stencil.gardenTiles[0] < kLeastGardenTiles * scale ||
		(L.stencil.threeGardens && L.stencil.gardenTiles[2] < kLeastGardenTiles * scale) ||
		L.stencil.gardenTiles[1] < kLeastWoodGardenTiles * scale)
	{
		L.failure = "The fort is too small for its gardens; raise Fort size.";
		return L;
	}
	const FortStencil &s = L.stencil;
	context.telemetry.measure("glacis.bastions.actual", s.bastions);

	// Stamp every fort.
	L.sketch.assign(n, GRASS);
	L.kind.assign(n, kOutside);
	L.fortOf.assign(n, -1);
	L.gardenOf.assign(n, -1);
	L.core.assign(n, 0);
	L.zone.assign(n, 0);
	L.wall.assign(n, 0);
	L.gate.assign(n, 0);
	L.glacis.assign(n, 0);
	L.roads.assign(n, 0);
	for (int k = 0; k < teams; ++k)
	{
		const int cx = int(std::lround(L.homes[k].x)), cy = int(std::lround(L.homes[k].y));
		for (int dy = -s.extent; dy <= s.extent; ++dy)
			for (int dx = -s.extent; dx <= s.extent; ++dx)
			{
				const int at = s.index(dx, dy);
				const auto [vx, vy] = turnStencilVertex(L.facings[k], dx, dy);
				const int v = t.at(cx + vx, cy + vy);
				if (s.vertex[at] != GRASS)
					L.sketch[v] = TerrainType(s.vertex[at]);
				const auto [tx, ty] = turnStencilTile(L.facings[k], dx, dy);
				const int i = t.at(cx + tx, cy + ty);
				if (!s.zone[at])
					continue;
				if (L.fortOf[i] >= 0 && L.fortOf[i] != k)
				{
					L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
					return L;
				}
				L.fortOf[i] = k;
				L.zone[i] = 1;
				L.core[i] = s.core[at];
				L.kind[i] = s.kind[at];
				L.wall[i] = s.kind[at] == kWallTile;
				L.gate[i] = s.kind[at] == kGateTile;
				L.glacis[i] = s.kind[at] == kGlacisTile;
				if (s.kind[at] == kGarden)
					L.gardenOf[i] = k * 3 + s.half[at];
			}
	}

	// The country: every tile belongs to its nearest fort, and a stream follows every long
	// boundary between two forts' country, wandering over a noise field.
	const std::vector<int> meander = fractalNoise(t.w, t.h, 32, 3, context.stream("glacis-streams"));
	const std::vector<int> uplands = fractalNoise(t.w, t.h, 64, 3, context.stream("glacis-uplands"));
	std::vector<unsigned char> water(n, 0);
	if (teams > 1)
	{
		std::vector<int> owner(n, 0);
		for (int i = 0; i < n; ++i)
		{
			int best = INT_MAX;
			for (int k = 0; k < teams; ++k)
			{
				const int d = t.dist2(i % t.w, i / t.w, int(std::lround(L.homes[k].x)),
									  int(std::lround(L.homes[k].y)));
				if (d < best)
				{
					best = d;
					owner[i] = k;
				}
			}
		}
		std::vector<int> pairKey(n, -1);
		for (int i = 0; i < n; ++i)
		{
			const int x = i % t.w, y = i / t.w;
			for (const int j : {t.at(x + 1, y), t.at(x, y + 1)})
				if (owner[j] != owner[i])
				{
					const int a = std::min(owner[i], owner[j]), b = std::max(owner[i], owner[j]);
					pairKey[i] = a * teams + b;
				}
		}
		std::vector<unsigned char> seen(n, 0);
		std::vector<unsigned char> blocked(n, 0);
		for (int i = 0; i < n; ++i)
			blocked[i] = L.zone[i];
		const std::vector<int> fromZone = stepsFrom(t, blocked);
		for (int start = 0; start < n; ++start)
		{
			if (pairKey[start] < 0 || seen[start])
				continue;
			// One boundary: its tiles, eight-connected, with the same pair of forts.
			std::vector<int> component{start};
			seen[start] = 1;
			for (size_t q = 0; q < component.size(); ++q)
			{
				const int x = component[q] % t.w, y = component[q] / t.w;
				for (int ddy = -1; ddy <= 1; ++ddy)
					for (int ddx = -1; ddx <= 1; ++ddx)
					{
						const int j = t.at(x + ddx, y + ddy);
						if (!seen[j] && pairKey[j] == pairKey[start])
						{
							seen[j] = 1;
							component.push_back(j);
						}
					}
			}
			if (int(component.size()) < kLeastBoundary)
				continue;
			const std::vector<unsigned char> mask = tileMask(t, component);
			const auto farthest = [&](int from)
			{
				const std::vector<int> steps = stepsFrom(t, tileMask(t, {from}), mask);
				int best = from;
				for (int i : component)
					if (steps[i] > steps[best])
						best = i;
				return best;
			};
			const int end1 = farthest(component.front()), end2 = farthest(end1);
			const std::vector<int> fromBoundary = stepsFrom(t, mask);
			std::vector<int> path = cheapestWalk(
				t, GridNeighbors::Cardinal, {end1}, tileMask(t, {end2}),
				[&](int, int to, int, int)
				{
					if (fromBoundary[to] > 2 || (fromZone[to] >= 0 && fromZone[to] <= 3))
						return -1;
					return 10 + 4 * fromBoundary[to];
				});
			if (path.size() < 8)
				continue;
			// Unwrap the walk and displace it.
			const int m = int(path.size());
			std::vector<ShapePoint> base(m);
			base[0] = {double(path[0] % t.w), double(path[0] / t.w)};
			for (int j = 1; j < m; ++j)
				base[j] = {base[j - 1].x + t.offsetX(path[j - 1] % t.w, path[j] % t.w),
						   base[j - 1].y + t.offsetY(path[j - 1] / t.w, path[j] / t.w)};
			const double longWave = 40 + context.bounded("glacis-meander", 32);
			const double shortWave = 14 + context.bounded("glacis-meander", 10);
			const double longPhase = context.bounded("glacis-meander", 628) / 100.0;
			const double shortPhase = context.bounded("glacis-meander", 628) / 100.0;
			std::vector<StrokePoint> course(m);
			std::vector<ShapePoint> tangent(m);
			for (int j = 0; j < m; ++j)
			{
				const ShapePoint &a = base[std::max(0, j - 5)], &b = base[std::min(m - 1, j + 5)];
				const double len = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
				tangent[j] = {(b.x - a.x) / len, (b.y - a.y) / len};
				const double along = double(j) / (m - 1);
				const double taper = std::sqrt(std::sin(kPi * along));
				const double side =
					taper * (kMeanderLong * std::sin(2 * kPi * j / longWave + longPhase) +
							 kMeanderShort * std::sin(2 * kPi * j / shortWave + shortPhase));
				course[j] = {base[j].x - tangent[j].y * side, base[j].y + tangent[j].x * side,
							 kStreamHalfWidth};
			}
			std::vector<unsigned char> stream(n, 0);
			strokePath(stream, t, course);
			for (int i = 0; i < n; ++i)
				if (stream[i] && (fromZone[i] < 0 || fromZone[i] > 2))
					water[i] = 1;
			++L.streams;
			const int mid = m / 2;
			const double ax = tangent[mid].x, ay = tangent[mid].y, len = 1;
			path[mid] = t.at(int(std::lround(course[mid].x)), int(std::lround(course[mid].y)));
			L.fords.push_back({path[mid], {ax / len, ay / len}, pairKey[start] / teams,
							   pairKey[start] % teams});
		}
	}
	// Ponds out in the country.
	{
		std::vector<unsigned char> keepOff(n, 0);
		int country = 0;
		for (int i = 0; i < n; ++i)
		{
			keepOff[i] = L.zone[i] || water[i];
			country += !L.zone[i];
		}
		const std::vector<int> clearance = stepsFrom(t, keepOff);
		std::vector<int> queued(n, 0);
		const int ponds = country / kCountryTilesPerPond;
		int placed = 0;
		for (int p = 0; p < ponds; ++p)
			for (int attempt = 0; attempt < 40; ++attempt)
			{
				const int seed = int(context.bounded("glacis-ponds", std::uint32_t(n)));
				if (clearance[seed] < kPondClearance || water[seed])
					continue;
				bool apart = true;
				for (int i = 0; i < n && apart; ++i)
					apart = !(queued[i] && t.chebyshev(i % t.w, i / t.w, seed % t.w, seed / t.w) < 2 * kPondClearance);
				if (!apart)
					continue;
				growWater(
					t, water, seed, kPondCorners, [&](int i) { return clearance[i] >= 4; },
					[&](int i)
					{
						const long long dx = t.offsetX(seed % t.w, i % t.w),
										dy = t.offsetY(seed / t.w, i / t.w);
						return dx * dx + dy * dy + meander[i] / 512;
					},
					queued, p + 1);
				++placed;
				break;
			}
		context.telemetry.measure("glacis.country.ponds", placed);
	}
	for (int i = 0; i < n; ++i)
		if (water[i])
			L.sketch[i] = WATER;
	// Every contested ford is sand, and a country road runs from it to the nearest gate of each
	// fort it lies between, over the cheapest ground (roads share their way where they can).
	for (const Ford &ford : L.fords)
		for (int ddy = -kFordHalf; ddy <= kFordHalf; ++ddy)
			for (int ddx = -kFordHalf; ddx <= kFordHalf; ++ddx)
			{
				const int j = t.at(ford.tile % t.w + ddx, ford.tile / t.w + ddy);
				L.sketch[j] = SAND;
				L.roads[j] = 1;
			}
	const std::vector<int> fromCore = stepsFrom(t, L.core);
	for (const Ford &ford : L.fords)
		for (const int k : {ford.a, ford.b})
		{
			const int cx = int(std::lround(L.homes[k].x)), cy = int(std::lround(L.homes[k].y));
			int end = -1, best = INT_MAX;
			for (const ShapePoint &e : s.gateEnds)
			{
				const ShapePoint p = turnStencilPoint(L.facings[k], e);
				const int tile = t.at(cx + int(std::lround(p.x)), cy + int(std::lround(p.y)));
				const int d = t.dist2(tile % t.w, tile / t.w, ford.tile % t.w, ford.tile / t.w);
				if (d < best)
				{
					best = d;
					end = tile;
				}
			}
			const std::vector<int> path = cheapestWalk(
				t, GridNeighbors::Eight, {end}, tileMask(t, {ford.tile}),
				[&](int, int to, int ddx, int ddy)
				{
					if (L.core[to])
						return -1;
					if (L.roads[to])
						return ddx && ddy ? 4 : 3;
					// Keep off the foot of the glacis, so tracks come in square to the gate.
					const int nearFoot = fromCore[to] >= 0 ? std::max(0, 8 - fromCore[to]) : 0;
					return (ddx && ddy ? 14 : 10) + uplands[to] / 4096 + (water[to] ? 200 : 0) +
						   8 * nearFoot;
				});
			if (path.empty())
			{
				L.failure = "A fort cannot reach its ford; use a bigger map or fewer colonies.";
				return L;
			}
			for (int i : path)
				for (int ddy = -1; ddy <= 1; ++ddy)
					for (int ddx = -1; ddx <= 1; ++ddx)
					{
						const int j = t.at(i % t.w + ddx, i / t.w + ddy);
						if (!L.core[j])
						{
							L.roads[j] = 1;
							L.sketch[j] = SAND;
						}
					}
		}
	context.telemetry.measure("glacis.streams", L.streams);
	context.telemetry.measure("glacis.fords", int(L.fords.size()));

	// The design's own invariant on the sketch as the game will see it: every wall tile stays pure
	// grass for its stone.
	TerrainSketch beached = L.sketch;
	layBeaches(beached, t);
	const std::vector<unsigned char> pure = pureTiles(beached, t, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.wall[i] && !pure[i])
		{
			L.failure = "A wall would stand on a beach; try another seed.";
			return L;
		}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "glacis layout";
	const GlacisOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("glacis.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const FortStencil &s = L.stencil;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "glacis terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	const DesignedStone walls = designedStone(map, t, L.wall);
	if (walls.gaps)
	{
		context.detail = "a fort wall has a gap at (" + std::to_string(walls.firstGap % t.w) + ", " +
						 std::to_string(walls.firstGap / t.w) + ")";
		return false;
	}
	for (int i = 0; i < n; ++i)
		if (walls.stone[i])
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "glacis colonies";
	std::vector<int> courtOf(n, -1);
	for (int i = 0; i < n; ++i)
		if (L.kind[i] == kCourt)
			courtOf[i] = L.fortOf[i];
	if (!settleColonies(
			game, context, "glacis-starts",
			[&](int k) { return homeGrassMask(map, t, courtOf, k); },
			[&](int k)
			{
				const ShapePoint p = turnStencilPoint(L.facings[k], L.stencil.swarm);
				return MapGeneratorPoint(int(std::lround(L.homes[k].x + p.x)) - 2,
										 int(std::lround(L.homes[k].y + p.y)) - 2);
			}))
		return false;
	std::vector<unsigned char> reserved = swarmSurroundings(t, context);

	// Towers in the bastions that flank the gates, against the wall, scored by the ground they
	// cover outside it.
	context.stage = "glacis towers";
	{
		std::vector<int> owner(n, -1);
		std::vector<unsigned char> buildable(n, 0), target(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int x = i % t.w, y = i / t.w;
			owner[i] = L.fortOf[i];
			const bool towerBastion = L.kind[i] == kPocket;
			buildable[i] = towerBastion && map.isGrass(x, y) && !map.isResource(x, y) &&
						   map.getBuilding(x, y) == NOGBID && !reserved[i];
			target[i] = L.fortOf[i] >= 0 && L.kind[i] != kCourt && L.kind[i] != kPocket &&
						L.kind[i] != kGarden &&
						!L.wall[i] && !map.isWater(x, y);
		}
		TowerRequest request = startingTowerRequest(o.towerLevel, o.towerLevel > 0 ? 2 : 0,
													kTowerPads, kTowerSpacing);
		request.otherWeight = 0;
		request.ownWeight = 1;
		request.against = &L.wall;
		TowerPlan towers = chooseTowerSites(t, owner, buildable, target,
											swarmSurroundings(t, context, 0), teams, request);
		if (!settleStartingTowers(game, context, towers, o.towerLevel, o.towerLevel > 0, &L.gate))
			return false;
		const std::vector<unsigned char> pads = towerFootprints(t, towers);
		for (int i = 0; i < n; ++i)
			reserved[i] = reserved[i] || pads[i];
	}

	context.stage = "glacis gardens";
	std::vector<unsigned char> topup(n, 0);
	for (int i = 0; i < n; ++i)
		topup[i] = L.gardenOf[i] >= 0;
	for (int k = 0; k < teams; ++k)
	{
		const int cx = int(std::lround(L.homes[k].x)), cy = int(std::lround(L.homes[k].y));
		for (int h = 0; h < (s.threeGardens ? 3 : 2); ++h)
		{
			const ShapePoint c = turnStencilPoint(L.facings[k], s.cisterns[h]);
			const bool wood = h == 1;
			const int wanted =
				wood ? std::max(kGardenWoodFloor,
								int(scaledCount(s.gardenTiles[h] * kGardenWoodPercent / 100, o.wood)))
					 : std::max(kGardenWheatFloor / 2,
								int(scaledCount(s.gardenTiles[h] * kGardenWheatPercent / 100, o.wheat)));
			const auto eligible = [&](int i)
			{
				return L.gardenOf[i] == k * 3 + h && map.isGrass(i % t.w, i / t.w) && !reserved[i] &&
					   clearGround(map, i % t.w, i / t.w);
			};
			int placed = 0;
			if (wood)
				placed = plantPatchNear(map, t, {cx + int(std::lround(c.x)), cy + int(std::lround(c.y)), 10},
										WOOD, std::min(wanted, s.gardenTiles[h]), eligible);
			else
			{
				// Wheat in a solid band from the garden line back, nearest the swarm: a patch grown
				// round the cistern left Numbi's scan of its food (estimateFood, one contiguous
				// rectangle from the nearest wheat) running into the cistern's water in two of the
				// four facings, and those forts' colonies stalled at 15 in a Numbi tournament
				// while the other two facings reached 60 (2026-09-16).
				const int sx = game.teams[k]->startPosX + 2, sy = game.teams[k]->startPosY + 2;
				std::vector<std::pair<int, int>> garden;
				for (int i = 0; i < n; ++i)
					if (eligible(i))
						garden.push_back({t.dist2(i % t.w, i / t.w, sx, sy), i});
				std::stable_sort(garden.begin(), garden.end());
				for (const auto &[d, i] : garden)
					if (placed < std::min(wanted, s.gardenTiles[h]))
					{
						map.setResource(i % t.w, i / t.w, WHEAT, 1);
						++placed;
					}
			}
			context.telemetry.measure(wood ? "glacis.garden.wood-planted" : "glacis.garden.wheat-planted",
									  placed, k * 3 + h);
		}
	}

	context.stage = "glacis country";
	const Fertility::Field fertility = cropGrowthField(terrain, t);
	const std::vector<int> woods = fractalNoise(t.w, t.h, 48, 3, context.stream("glacis-woods"));
	const PeriodicNoise fields(t.w, t.h, 12, context.stream("glacis-fields"));
	const auto country = [&](int i)
	{ return !L.zone[i] && !L.roads[i] && clearGround(map, i % t.w, i / t.w); };
	std::vector<int> open;
	for (int i = 0; i < n; ++i)
		if (country(i))
			open.push_back(i);
	// Woods crowd up to the foot of every glacis, the ground its builders cleared, so the ring reads
	// against them; further out the noise alone decides.
	const std::vector<int> fromZone = stepsFrom(t, L.zone);
	const auto woodiness = [&](int i)
	{ return woods[i] + (fromZone[i] >= 0 ? std::max(0, 10 - fromZone[i]) * 3000 : 0); };
	std::stable_sort(open.begin(), open.end(),
					 [&](int a, int b) { return woodiness(a) > woodiness(b); });
	const int forest =
		std::min(int(open.size()), int(scaledCount(int(open.size()) * kForestPercent / 100, o.wood)));
	for (int j = 0; j < forest; ++j)
		map.setResource(open[j] % t.w, open[j] / t.w, WOOD, 1);
	context.telemetry.measure("glacis.country.forest-tiles", forest);
	// The contested fords' prizes, before the fields take the banks.
	for (size_t f = 0; f < L.fords.size(); ++f)
	{
		const Ford &ford = L.fords[f];
		const int fx = ford.tile % t.w, fy = ford.tile / t.w;
		const double nx = -ford.along.y, ny = ford.along.x;
		for (const int bank : {-1, 1})
		{
			for (int fruit = 0; fruit < 3; ++fruit)
			{
				const double along = (fruit - 1) * kGroveSpacing;
				const int seed = seedNear(
					t, fx + int(std::lround(bank * kPrizeOut * nx + along * ford.along.x)),
					fy + int(std::lround(bank * kPrizeOut * ny + along * ford.along.y)), 3,
					[&](int i) { return country(i); });
				if (seed >= 0)
					growPatch(map, t, seed, CHERRY + fruit, int(scaledCount(kGroveTiles, o.fruit)),
							  [&](int i) { return country(i); });
			}
			const int quarry = seedNear(t, fx + int(std::lround(bank * kQuarryOut * nx)),
										fy + int(std::lround(bank * kQuarryOut * ny)), 4,
										[&](int i) { return country(i); });
			if (quarry >= 0 && scaledCount(1, o.stone) > 0)
				placeResourceClump(map, context, MapGeneratorPoint(quarry % t.w, quarry / t.w), STONE,
								   1);
		}
	}
	furnishGround(
		map, t, context, fertility, country, [&](int i) { return fields.at(i % t.w, i / t.w); },
		[&](int i) { return fields.at(i % t.w + 7919, i / t.w + 104729); },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(area / kFieldTilesPer, o.wheat)), 0,
								 int(scaledCount(area / kOutcropTilesPer, o.stone)),
								 int(scaledCount(area / kGroveTilesPer, o.fruit))};
		},
		"glacis-outcrops", "glacis-groves");
	seedAlgae(map, context, t, "glacis-algae", o.algae, AlgaeBand::anyWater(50));

	secureStartingCrops(game, context, t, 24, 32, 0, &walls.stone, &topup);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&walls.stone);
	context.stage = "glacis routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1}, 0, &walls.stone);
	return true;
}

// Every pure-grass tile a crop on `from` could ever spread to, eight-connected, never onto stone.
std::vector<int> grassReach(const Map &map, const Torus &t, const std::vector<unsigned char> &from)
{
	const int n = t.size();
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		open[i] = map.isGrass(x, y) &&
				  !(map.isResource(x, y) && map.getResource(x, y).type == STONE);
	}
	std::vector<unsigned char> source(n, 0);
	for (int i = 0; i < n; ++i)
		source[i] = from[i] && open[i];
	return stepsFrom(t, source, open);
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const GlacisOptions o(context.request);
	const Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	if (const std::string mismatch = designMismatch(L, map, "glacis"); !mismatch.empty())
		return mismatch;
	if (const std::string broken = wallStanding(map, t, L.wall, L.gate, "fort wall"); !broken.empty())
		return broken;
	std::vector<int> pieces(n, -1);
	for (int i = 0; i < n; ++i)
		pieces[i] = (L.kind[i] == kCourt || L.kind[i] == kPocket || L.kind[i] == kGarden) ? L.fortOf[i]
					: (!L.wall[i] && !L.gate[i])                  ? teams
																  : -1;
	if (pieceLeak(map, t, pieces, L.gate) >= 0)
		return "A fort can be entered other than by its gates.";
	// The glacis stays bare: nothing planted on it, and no grass joins it to anything else a crop
	// could spread from.
	const std::vector<int> fromGlacis = grassReach(map, t, L.glacis);
	for (int i = 0; i < n; ++i)
	{
		if (L.glacis[i] && map.isResource(i % t.w, i / t.w))
			return "Something was planted on a glacis.";
		if (fromGlacis[i] >= 0 && L.kind[i] != kGlacisTile && L.kind[i] != kCoveredTile &&
			L.kind[i] != kFootTile)
			return "A glacis is joined to grass a crop could spread from.";
	}
	// The gardens keep their crops off the town.
	{
		std::vector<unsigned char> gardens(n, 0);
		for (int i = 0; i < n; ++i)
			gardens[i] = L.gardenOf[i] >= 0;
		const std::vector<int> reach = grassReach(map, t, gardens);
		for (int i = 0; i < n; ++i)
			if (reach[i] >= 0 && !gardens[i])
				return "A fort's gardens are open to its courtyard.";
	}
	int fewest = INT_MAX, most = 0;
	for (int k = 0; k < teams; ++k)
	{
		int towers = 0;
		for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
			if (const Building *b = game.teams[k]->myBuildings[slot];
				b && b->type->shootingRange && !b->type->isBuildingSite)
				++towers;
		fewest = std::min(fewest, towers);
		most = std::max(most, towers);
	}
	if (teams > 0 && (fewest != most || (o.towerLevel > 0 && fewest < 1)))
		return "The colonies do not start with the same towers.";
	if (const ColonyWalk walk = walkFromFirstColony(map, teams, "the country", "over the fords");
		!walk.error.empty())
		return walk.error;
	return startingAccessFailure(map, teams, {{WHEAT, 24, "wheat"}, {WOOD, 32, "wood"}}, 16, 24);
}
} // namespace

GlacisOptions::GlacisOptions(const GenerationRequest &r)
	: fortSize(r.option("fort-size")), bastions(r.option("bastions")),
	  glacisWidth(r.option("glacis-width")), gates(r.option("gates")),
	  towerLevel(r.option("starting-towers")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition glacisDefinition()
{
	return {
			"glacis",
			39,
			"The Glacis",
			3,
			false,
			// Fort size is the bastion tip radius: 28 holds a small town and three gardens; 20 is
			// the least with a courtyard. Bastions Mixed deals four, five or six per map. A glacis of
			// 10 reads as a cleared ring against the country; under 6 it does not. Starting towers
			// off by default: a tower's empty stone store recruits workers before the first harvest.
			{{"fort-size", "Fort size", 20, 36, 2, 30, ControlGroup::Layout},
			 GeneratorControl::choice("bastions", "Bastions", {"Mixed", "Four-pointed", "Five-pointed", "Six-pointed"}, 0),
			 {"glacis-width", "Glacis width", 6, 14, 2, 10, ControlGroup::Layout},
			 {"gates", "Gates", 1, 4, 1, 4, ControlGroup::Layout},
			 {"starting-towers", "Starting tower level", 0, 3, 1, 0, ControlGroup::Layout},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			{"terrain:arena", "feature:stone-walls", "feature:forest", "feature:river",
			 "style:fortified", "style:siege", "fairness:stamped-lattice"}};
}
