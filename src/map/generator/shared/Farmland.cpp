#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Farmland.h"
#include "GenerationContext.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Growth.h"
#include "Room.h"
#include "Map.h"
#include "ResourceSemantics.h"
#include "Resources.h"
#include "Planting.h"
#include "Drawing.h"
#include "Territories.h"
#include "Topology.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace MapGeneration
{
namespace
{
// How far from its point a field's seed may move to find open ground.
constexpr int kSeedSearch = 16;
} // namespace

std::vector<int> stampContainedPlot(TerrainSketch &sketch, const Torus &t,
									const std::vector<int> &corners, int margin)
{
	margin = std::max(1, margin);
	const std::set<int> inside(corners.begin(), corners.end());
	// Two corner rows include a pure-sand tile even on a straight edge. Using a square
	// stencil also seals diagonal steps; radial offsets alone need not seal raster corners.
	for (int i : inside)
	{
		::MapGeneration::generationCheckpoint();
		for (int dy = -margin; dy <= margin; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -margin; dx <= margin; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				sketch.at(t.at(t.remainderX(i) + dx, i / t.w + dy)) = SAND;
			}
		}
	}
	for (int i : inside)
	{
		::MapGeneration::generationCheckpoint();
		sketch.at(i) = GRASS;
	}
	std::vector<int> tiles;
	for (int i : inside)
	{
		::MapGeneration::generationCheckpoint();
		if (inside.count(t.at(t.remainderX(i) + 1, i / t.w)) && inside.count(t.at(t.remainderX(i), i / t.w + 1)) &&
			inside.count(t.at(t.remainderX(i) + 1, i / t.w + 1)))
			tiles.push_back(i);
	}
	return tiles;
}

std::vector<ShoreField> layShoreFields(TerrainSketch &terrain, const Torus &t,
									   const std::vector<unsigned char> &allowed,
									   std::vector<int> &plotOf, int firstPlot, bool contained)
{
	auto stamp = [&](const std::vector<int> &shape)
	{
		if (contained) return stampContainedPlot(terrain, t, shape, 1);
		for (int i : shape)
		{
			::MapGeneration::generationCheckpoint();
			terrain.at(i) = GRASS;
		}
		const auto inside = tileMask(t, shape);
		std::vector<int> tiles;
		for (int i : shape)
		{
			::MapGeneration::generationCheckpoint();
			if (inside.at(t.at(t.remainderX(i) + 1, i / t.w)) && inside.at(t.at(t.remainderX(i), i / t.w + 1)) &&
				inside.at(t.at(t.remainderX(i) + 1, i / t.w + 1)))
				tiles.push_back(i);
		}
		return tiles;
	};
	const auto water = pureTiles(terrain, t, WATER);
	const auto distance = stepsFrom(t, water);
	std::vector<unsigned char> corners(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!allowed.at(i) || terrain.at(i) == WATER || distance.at(i) < 2 || distance.at(i) > 8)
			continue;
		bool fits = true;
		for (int y = -2; y <= 2 && fits; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = -2; x <= 2 && fits; ++x)
			{
				::MapGeneration::generationCheckpoint();
				const int q = t.at(t.remainderX(i) + x, i / t.w + y);
				fits = plotOf.at(q) < 0 && (terrain.at(q) != GRASS || allowed.at(q)) &&
					   (std::abs(x) > 1 || std::abs(y) > 1 || terrain.at(q) != WATER);
			}
		}
		corners.at(i) = fits;
	}
	// Remove thin tendrils before drawing, rather than decorating their fringe.
	corners = openMask(t, corners, 1);
	const auto regions = connectedRegions(corners, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<std::vector<int>> groups;
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (regions.at(i) >= 0)
		{
			if (int(groups.size()) <= regions.at(i))
				groups.resize(regions.at(i) + 1);
			groups.at(regions.at(i)).push_back(i);
		}
	}
	const auto original = terrain;
	std::vector<ShoreField> fields(groups.size());
	for (size_t k = 0; k < groups.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		fields.at(k).tiles = stamp(groups.at(k));
	}
	const auto fertility = cropGrowthField(terrain, t);
	std::vector<ShoreField> accepted;
	std::vector<std::vector<int>> acceptedCorners;
	for (size_t k = 0; k < fields.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		auto &field = fields.at(k);
		auto inside = tileMask(t, field.tiles);
		const auto anchors = buildAnchors(t, inside, 4);
		int best = -1;
		for (int i : field.tiles)
		{
			::MapGeneration::generationCheckpoint();
			if (anchors.at(i) &&
				(best < 0 || fertility.at(t.remainderX(i), i / t.w) > fertility.at(t.remainderX(best), best / t.w)))
				best = i;
		}
		if (best < 0)
			continue;
		field.court = best;
		std::vector<int> queue, parent(t.size(), -2);
		for (int y = 0; y < 4; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < 4; ++x)
			{
				::MapGeneration::generationCheckpoint();
				int q = t.at(t.remainderX(best) + x, best / t.w + y);
				parent.at(q) = -1;
				queue.push_back(q);
			}
		}
		int exit = -1;
		for (size_t head = 0; head < queue.size() && exit < 0; ++head)
		{
			::MapGeneration::generationCheckpoint();
			for (auto step : kCardinalSteps)
			{
				::MapGeneration::generationCheckpoint();
				int i = queue.at(head), q = t.at(t.remainderX(i) + step[0], i / t.w + step[1]);
				if (!inside.at(q))
				{
					// Open toward land, not into a beach enclosed by a ring of crops.
					if (distance.at(q) > distance.at(i) && terrain.at(q) != WATER)
					{
						exit = i;
						break;
					}
					continue;
				}
				if (parent.at(q) == -2)
				{
					parent.at(q) = i;
					queue.push_back(q);
				}
			}
		}
		if (exit < 0)
			continue;
		std::vector<unsigned char> opening(t.size(), 0);
		for (int i = exit; i >= 0; i = parent.at(i))
		{
			::MapGeneration::generationCheckpoint();
			opening.at(i) = 1;
		}
		std::uint64_t potential = 0;
		int shore = 0;
		for (int i : field.tiles)
		{
			::MapGeneration::generationCheckpoint();
			const bool court = t.x(t.remainderX(i) - t.remainderX(best)) < 4 && t.y(i / t.w - best / t.w) < 4;
			const auto value = fertility.at(t.remainderX(i), i / t.w);
			if (!court && !opening.at(i) && value >= Fertility::kScale / 64)
			{
				field.seedTiles.push_back(i);
				potential += value;
				shore += distance.at(i) <= 3;
			}
		}
		if (field.seedTiles.size() < 24 || shore < 6 || potential < 2 * Fertility::kScale)
			continue;
		accepted.push_back(std::move(field));
		acceptedCorners.push_back(std::move(groups.at(k)));
	}
	// Reclaiming sandy banks can change another field's mirrored sand probe.
	// Recheck the completed terrain after rejected candidates are removed. Each
	// failed pass removes a field, so this terminates independently of wall time.
	for (;;)
	{
		::MapGeneration::generationCheckpoint();
		terrain = original;
		for (const auto &shape : acceptedCorners)
		{
			::MapGeneration::generationCheckpoint();
			stamp(shape);
		}
		const auto finalGrowth = cropGrowthField(terrain, t);
		bool removed = false;
		for (size_t k = accepted.size(); k-- > 0;)
		{
			::MapGeneration::generationCheckpoint();
			auto &field = accepted.at(k);
			field.growthPotential = 0;
			int shore = 0;
			field.seedTiles.erase(
				std::remove_if(
					field.seedTiles.begin(), field.seedTiles.end(), [&](int i)
					{ return finalGrowth.at(t.remainderX(i), i / t.w) < Fertility::kScale / 64; }),
				field.seedTiles.end());
			for (int i : field.seedTiles)
			{
				::MapGeneration::generationCheckpoint();
				field.growthPotential += finalGrowth.at(t.remainderX(i), i / t.w);
				shore += distance.at(i) <= 3;
			}
			if (field.seedTiles.size() < 24 || shore < 6 ||
				field.growthPotential < 2 * Fertility::kScale)
			{
				accepted.erase(accepted.begin() + k);
				acceptedCorners.erase(acceptedCorners.begin() + k);
				removed = true;
			}
		}
		if (!removed)
			break;
	}
	for (size_t k = 0; k < accepted.size(); ++k)
	{
		::MapGeneration::generationCheckpoint();
		for (int i : accepted.at(k).tiles)
		{
			::MapGeneration::generationCheckpoint();
			plotOf.at(i) = firstPlot + int(k);
		}
	}
	return accepted;
}

int plantShoreFields(Map &map, const Torus &t, const std::vector<ShoreField> &fields,
					 const Fertility::Field &fertility, int percent)
{
	int planted = 0;
	for (const auto &field : fields)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<int> fertile;
		for (int i : field.seedTiles)
		{
			::MapGeneration::generationCheckpoint();
			if (fertility.at(t.remainderX(i), i / t.w) >= Fertility::kScale / 64)
				fertile.push_back(i);
		}
		const int wanted = std::min(int(fertile.size()), int(scaledCount(fertile.size(), percent)));
		std::vector<int> rim;
		for (int i : fertile)
		{
			::MapGeneration::generationCheckpoint();
			const int dx = t.offsetX(t.remainderX(field.court), t.remainderX(i));
			const int dy = t.offsetY(field.court / t.w, i / t.w);
			if (dx >= -1 && dx <= 4 && dy >= -1 && dy <= 4)
				rim.push_back(i);
		}
		const int rimPlanted = plantFieldInteriors(map, t, rim, WHEAT, std::min(wanted, 12));
		planted += rimPlanted + plantFieldInteriors(map, t, fertile, WHEAT, wanted - rimPlanted);
	}
	return planted;
}

int plantContainedPlot(Map &map, const Torus &t, const std::vector<int> &tiles,
					   const Fertility::Field &fertility, int resource, int wanted, bool renewable)
{
	std::vector<std::pair<int, int>> ranked;
	for (int i : tiles)
	{
		::MapGeneration::generationCheckpoint();
		const int f = fertility.at(t.remainderX(i), i / t.w);
		if ((!renewable || f > 0) && clearGround(map, t.remainderX(i), i / t.w))
			ranked.push_back({-f, i});
	}
	std::sort(ranked.begin(), ranked.end());
	const int count = std::min(std::max(0, wanted), int(ranked.size()));
	for (int k = 0; k < count; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const int i = ranked.at(k).second;
		map.setResourceByIndex(t.remainderX(i), i / t.w, resource, 1);
	}
	return count;
}

std::string containedPlotsMismatch(const Map &map, const Torus &t, const std::vector<int> &plotOf,
								   const Fertility::Field *dry,
								   const std::vector<unsigned char> *finiteWheat)
{
	if (int(plotOf.size()) != t.size() || (finiteWheat && int(finiteWheat->size()) != t.size()))
		return "Contained plot labels have the wrong dimensions.";
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		const int type = map.getResource(x, y).type;
		if (plotOf.at(i) < 0)
		{
			const bool finiteCrop =
				dry && dry->at(x, y) == 0 &&
				(type == WOOD || (type == WHEAT && finiteWheat && (*finiteWheat).at(i)));
			if ((type == WHEAT || type == WOOD) && !finiteCrop)
				return "A spreading crop was planted outside its contained plot.";
			continue;
		}
		if (!map.terrainSupportsResourceAtByIndex(x,y,WHEAT))
			return "Contained plot " + std::to_string(plotOf.at(i)) + " lost grass at (" +
				   std::to_string(x) + ", " + std::to_string(y) + ").";
		for (int dy = -1; dy <= 1; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -1; dx <= 1; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int j = t.at(x + dx, y + dy);
				if (map.terrainSupportsResourceAtByIndex(t.remainderX(j), j / t.w, WHEAT) &&
					plotOf.at(j) != plotOf.at(i))
					return "A contained plot has a grass growth connection across its margin.";
			}
		}
	}
	return "";
}

double ContourWobble::at(double angle) const
{
	// The weights sum to one, so the shift never exceeds the amplitude.
	return amplitude * (0.55 * ::MapGeneration::Numeric::sin(2 * angle + phase[0]) +
						0.3 * ::MapGeneration::Numeric::sin(3 * angle + phase[1]) +
						0.15 * ::MapGeneration::Numeric::sin(5 * angle + phase[2]));
}

double ContourFarmStyle::reach() const
{
	double amplitude = 0;
	for (const ContourWobble &w : wobbles)
	{
		::MapGeneration::generationCheckpoint();
		amplitude = std::max(amplitude, w.amplitude);
	}
	return outerRadius() + amplitude;
}

double contourNominal(const ContourFarmStyle &style, size_t centre, double distance, double angle)
{
	if (centre >= style.wobbles.size() || style.wobbles.at(centre).amplitude <= 0)
		return distance;
	const ContourWobble &w = style.wobbles.at(centre);
	// Ramped in over amplitude + 2 tiles past the cap: the shift then changes by less than a tile
	// per tile along a ray, so the mapping is monotone, and the summit and its cap stay round.
	const double ramp =
		std::clamp((distance - style.innerRadius - style.cap) / (w.amplitude + 2), 0.0, 1.0);
	return distance + w.at(angle) * ramp;
}

ContourFarm layContourFarm(TerrainSketch &sketch, const Torus &t,
						   const std::vector<ShapePoint> &centres, const ContourFarmStyle &style)
{
	ContourFarm result;
	Farm &farm = result.farm;
	farm.water.assign(t.size(), 0);
	farm.sand.assign(t.size(), 0);
	farm.plot.assign(t.size(), 0);
	farm.row.assign(t.size(), -1);
	result.crossings.assign(t.size(), 0);
	farm.rows = style.bands;
	const double outer = style.outerRadius();
	const int extent = int(::MapGeneration::Numeric::ceil(style.reach()));
	// Scan each bounded disc rather than the whole torus for each centre. Integer
	// offsets give identical rasterization at every whole-corner translation, even
	// when a disc straddles the map seam. Discs must not overlap: silently choosing
	// a winning farm would cut the other farm's containment cap.
	std::vector<ShapePoint> directions;
	for (int s = 0; s < style.crossings; ++s)
	{
		::MapGeneration::generationCheckpoint();
		const double angle = style.phase + s * 2 * kPi / style.crossings;
		directions.push_back(
			{::MapGeneration::Numeric::cos(angle), ::MapGeneration::Numeric::sin(angle)});
	}
	for (size_t c = 0; c < centres.size(); ++c)
	{
		::MapGeneration::generationCheckpoint();
		const ShapePoint &centre = centres.at(c);
		for (int dy = -extent; dy <= extent; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -extent; dx <= extent; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const double distance =
					contourNominal(style, c, ::MapGeneration::Numeric::hypot(dx, dy),
								   ::MapGeneration::Numeric::atan2(double(dy), double(dx)));
				if (distance < style.innerRadius || distance > outer)
					continue;
				const int i = t.at(int(centre.x) + dx, int(centre.y) + dy);
				// A ray, not an infinite line: the positive projection selects the
				// outward half and the perpendicular projection controls true width.
				// This avoids stair widths shrinking with radius as angular wedges do.
				for (const ShapePoint &direction : directions)
				{
					::MapGeneration::generationCheckpoint();
					if (dx * direction.x + dy * direction.y > 0 &&
						std::fabs(-dx * direction.y + dy * direction.x) <= style.crossingHalfWidth)
						result.crossings.at(i) = 1;
				}
				const double radial = distance - style.innerRadius - style.cap;
				if (result.crossings.at(i) || radial < 0 || distance > outer - style.cap)
				{
					sketch.at(i) = SAND;
					farm.sand.at(i) = 1;
					continue;
				}
				// Each complete period begins with crops and ends with irrigation.
				// Keeping the last water band whole is important: an outer crop
				// fragment would have a different growth budget and harvesting edge.
				const int band = int(radial / style.rows.period());
				const bool water =
					::MapGeneration::Numeric::fmod(radial, style.rows.period()) >= style.rows.crops;
				farm.row.at(i) = 2 * band + int(water);
				farm.water.at(i) = water;
				sketch.at(i) = water ? WATER : GRASS;
			}
		}
	}
	return result;
}

FarmRows bestFarmRows(double angle)
{
	// Rows repeat every quarter turn and mirror about the diagonal, so only the angle from the nearest
	// axis matters, 0 to 45 degrees.
	double a = ::MapGeneration::Numeric::fmod(std::abs(angle), kPi / 2);
	a = std::min(a, kPi / 2 - a);
	const double diagonal = std::min(1.0, 2 * ::MapGeneration::Numeric::sin(2 * a));
	return {10 + 2 * diagonal, 8 + diagonal};
}

double farmYield(double angle)
{
	// tools/farm_row_fit.py's best yields at 0, 11.25, 22.5, 33.75 and 45 degrees from an axis.
	constexpr double yields[] = {0.1489, 0.1322, 0.1225, 0.1141, 0.1126};
	double a = ::MapGeneration::Numeric::fmod(std::abs(angle), kPi / 2);
	a = std::min(a, kPi / 2 - a);
	const double step = a / (kPi / 16);
	const int below = std::min(3, int(step));
	const double along = std::min(1.0, step - below);
	return yields[below] + (yields[below + 1] - yields[below]) * along;
}

void stampFarmPlot(TerrainSketch &sketch, const Torus &t, Farm &farm, int x0, int y0,
				   const FarmPlot &plot)
{
	const int margin = plot.ring + 1;
	for (int dy = -margin; dy <= plot.height + margin; ++dy)
	{
		::MapGeneration::generationCheckpoint();
		for (int dx = -margin; dx <= plot.width + margin; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			const int i = t.at(x0 + dx, y0 + dy);
			const bool grass = dx >= 0 && dx <= plot.width && dy >= 0 && dy <= plot.height;
			const bool ring = !grass && dx >= -plot.ring && dx <= plot.width + plot.ring &&
							  dy >= -plot.ring && dy <= plot.height + plot.ring;
			// The clearing trumps the rows and bridges alike: its grass is grass.
			if (farm.water.at(i) || (grass && farm.sand.at(i)))
			{
				farm.water.at(i) = 0;
				farm.sand.at(i) = 0;
				sketch.at(i) = GRASS;
			}
			if (ring)
			{
				farm.sand.at(i) = 1;
				sketch.at(i) = SAND;
			}
		}
	}
	for (int dy = 0; dy < plot.height; ++dy)
	{
		::MapGeneration::generationCheckpoint();
		for (int dx = 0; dx < plot.width; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			farm.plot.at(t.at(x0 + dx, y0 + dy)) = 1;
		}
	}
}

GeneratorControl waterCrossingsControl()
{
	return GeneratorControl::toggle("water-crossings", "Sand bridges over water", true,
									ControlGroup::Terrain)
		.withSearchValues({1});
}

GeneratorControl cropCrossingsControl()
{
	return GeneratorControl::toggle("crop-crossings", "Sand lanes through crops", true,
									ControlGroup::Terrain)
		.withSearchValues({1});
}

Farm layFarm(TerrainSketch &sketch, const Torus &t, const std::vector<unsigned char> &region,
			 double angle, ShapePoint origin, int rim, const FarmRows &rows, const FarmPlot *plot,
			 const FarmBridges &bridges, bool caps, std::vector<int> *edgeDepthOut)
{
	const int n = t.size();
	Farm farm;
	farm.water.assign(n, 0);
	farm.row.assign(n, -1);
	std::vector<unsigned char> outside(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		outside.at(i) = !region.at(i);
	}
	std::vector<int> fromEdge = stepsFrom(t, outside);
	// Across the rows: the rows run along `angle`, so a vertex's place across them is its offset along
	// the normal, shifted so the crop row at the origin is centred on it.
	const double nx = -::MapGeneration::Numeric::sin(angle),
				 ny = ::MapGeneration::Numeric::cos(angle), period = rows.period();
	const int ox = int(::MapGeneration::Numeric::lround(origin.x)),
			  oy = int(::MapGeneration::Numeric::lround(origin.y));
	std::vector<int> cycle(n, 0);
	std::vector<unsigned char> wet(n, 0);
	int lowest = 0;
	bool any = false;
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!region.at(i))
			continue;
		const double across =
			t.offsetX(ox, t.remainderX(i)) * nx + t.offsetY(oy, i / t.w) * ny + rows.crops / 2;
		cycle.at(i) = int(::MapGeneration::Numeric::floor(across / period));
		wet.at(i) = across - cycle.at(i) * period >= rows.crops;
		lowest = any ? std::min(lowest, cycle.at(i)) : cycle.at(i);
		any = true;
	}
	// Rows are numbered from zero at the lowest, so a row's parity says crops (even) or water (odd).
	std::vector<unsigned char> seen;
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!region.at(i))
			continue;
		farm.row.at(i) = 2 * (cycle.at(i) - lowest) + wet.at(i);
		if (wet.at(i) && fromEdge.at(i) >= rim)
		{
			farm.water.at(i) = 1;
			sketch.at(i) = WATER;
			if (farm.row.at(i) >= int(seen.size()))
				seen.resize(farm.row.at(i) + 1, 0);
			seen.at(farm.row.at(i)) = 1;
		}
	}
	farm.sand.assign(n, 0);
	farm.plot.assign(n, 0);
	// Caps: a ring of sand vertices round the field just inside its rim, where the water rows' beaches
	// begin, closing every crop row's ends and outer sides, so wheat and wood cannot grow out of the rows
	// into the ground round the farm (a home it opens into). Sand is walkable, so workers still cross.
	if (caps && rim >= 2)
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (region.at(i) && !farm.water.at(i) && fromEdge.at(i) == rim - 1)
			{
				farm.sand.at(i) = 1;
				sketch.at(i) = SAND;
			}
		}
	// Bridges: every `bridges.spacing` tiles along the rows, a line of sand vertices straight across the
	// whole farm inside its cap, water rows and crop rows alike (FEEDBACK 2026-09-14: "extend those
	// same sand bridges across the grass farm portions as well, so they go clean across the whole
	// farm"). Across a water row the tiles either side of the line are no longer pure water, so
	// workers walk across, two tiles wide, instead of round the end of a long row; across a crop row
	// the line is a lane no crop grows over, so the farm is cut into bays that can be walked round
	// without cutting through the rows. Each half can be switched off (FarmBridges). The cap ring
	// (fromEdge == rim - 1) is sand already; the rim beyond it is the coast's or the wall's, and is
	// left alone.
	if (bridges.spacing > 0)
	{
		const double ax = ::MapGeneration::Numeric::cos(angle),
					 ay = ::MapGeneration::Numeric::sin(angle);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (!region.at(i) || fromEdge.at(i) < rim - 1 || !bridges.crosses(farm.water.at(i)))
				continue;
			const double along = t.offsetX(ox, t.remainderX(i)) * ax + t.offsetY(oy, i / t.w) * ay;
			const double off =
				along - bridges.spacing * ::MapGeneration::Numeric::round(along / bridges.spacing);
			if (std::abs(off) < kBridgeHalfWidth)
			{
				farm.water.at(i) = 0;
				farm.sand.at(i) = 1;
				sketch.at(i) = SAND;
			}
		}
	}
	if (plot)
	{
		// The clearing's middle: the region's most inland vertex, the first on a tie.
		int middle = -1;
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (region.at(i) && (middle < 0 || fromEdge.at(i) > fromEdge.at(middle)))
				middle = i;
		}
		const int margin = plot->ring + 1;
		const int x0 = t.remainderX(middle) - plot->width / 2, y0 = middle / t.w - plot->height / 2;
		// The grass tiles span vertices x0..x0+width by y0..y0+height; the ring and a vertex more
		// round them must lie as far inside the region as its water rows, so the ring's sand never
		// meets the region's own beach.
		bool fits = middle >= 0;
		for (int dy = -margin; dy <= plot->height + margin && fits; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -margin; dx <= plot->width + margin && fits; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				fits = fromEdge.at(t.at(x0 + dx, y0 + dy)) >= rim;
			}
		}
		if (fits)
		{
			farm.plotX = t.x(x0);
			farm.plotY = t.y(y0);
			stampFarmPlot(sketch, t, farm, x0, y0, *plot);
			seen.assign(seen.size(), 0);
			for (int i = 0; i < n; ++i)
			{
				::MapGeneration::generationCheckpoint();
				if (farm.water.at(i))
					seen.at(farm.row.at(i)) = 1;
			}
		}
	}
	farm.rows = int(std::count(seen.begin(), seen.end(), 1));
	if (edgeDepthOut)
		*edgeDepthOut = std::move(fromEdge);
	return farm;
}

std::vector<int>
growFarmFields(const Torus &t, const std::vector<unsigned char> &occupied,
			   const std::vector<int> &homeOf, const std::vector<unsigned char> &area,
			   const std::vector<ShapePoint> &seeds, const std::vector<int> &owners,
			   const std::vector<ShapePoint> &anchors, int gap, double neckHalfWidth,
			   const std::vector<double> &rowAngles, int opening)
{
	const int n = t.size();
	// Open ground: not designed, allowed, and clear of all land that is no home by the gap. Homes'
	// surroundings stay open here; each field is held off other colonies' homes once grown.
	std::vector<unsigned char> foreign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		foreign.at(i) = occupied.at(i) && homeOf.at(i) < 0;
	}
	const std::vector<int> fromForeign = stepsFrom(t, foreign);
	std::vector<unsigned char> free(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		free.at(i) =
			area.at(i) && !occupied.at(i) && (fromForeign.at(i) < 0 || fromForeign.at(i) >= gap);
	}
	// Each seed starts from the free tile near its point that lies in the biggest stretch of free ground
	// (nearest the point on a tie), so a field never starts boxed in a pocket beside its home.
	const std::vector<int> stretch = connectedRegions(free, t.w, t.h, true);
	std::vector<int> stretchSize;
	for (int label : stretch)
	{
		::MapGeneration::generationCheckpoint();
		if (label >= 0)
		{
			if (label >= int(stretchSize.size()))
				stretchSize.resize(label + 1, 0);
			++stretchSize.at(label);
		}
	}
	std::vector<std::vector<int>> starts(seeds.size());
	std::vector<ShapePoint> snapped(seeds);
	for (size_t s = 0; s < seeds.size(); ++s)
	{
		::MapGeneration::generationCheckpoint();
		const int sx = int(::MapGeneration::Numeric::lround(seeds.at(s).x)),
				  sy = int(::MapGeneration::Numeric::lround(seeds.at(s).y));
		int best = -1, bestSize = 0, bestDistance = 0;
		for (int dy = -kSeedSearch; dy <= kSeedSearch; ++dy)
		{
			::MapGeneration::generationCheckpoint();
			for (int dx = -kSeedSearch; dx <= kSeedSearch; ++dx)
			{
				::MapGeneration::generationCheckpoint();
				const int i = t.at(sx + dx, sy + dy);
				if (!free.at(i))
					continue;
				const int size = stretchSize.at(stretch.at(i)), distance = dx * dx + dy * dy;
				if (best < 0 || size > bestSize || (size == bestSize && distance < bestDistance))
				{
					best = i;
					bestSize = size;
					bestDistance = distance;
				}
			}
		}
		if (best < 0)
			continue;
		snapped.at(s) = {double(sx + t.offsetX(sx, t.remainderX(best))),
						 double(sy + t.offsetY(sy, best / t.w))};
		starts.at(s).push_back(best);
	}
	std::vector<double> worth;
	for (double angle : rowAngles)
	{
		::MapGeneration::generationCheckpoint();
		worth.push_back(farmYield(angle));
	}
	std::vector<int> labels =
		growTerritories(
			t, free, starts, [](int) { return 0; }, worth.size() == seeds.size() ? &worth : nullptr)
			.labels;
	// Every field keeps the gap from every home but its own.
	int colonies = 0;
	for (int owner : owners)
	{
		::MapGeneration::generationCheckpoint();
		colonies = std::max(colonies, owner + 1);
	}
	for (int k = 0; k < colonies; ++k)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> others(n, 0);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			others.at(i) = homeOf.at(i) >= 0 && homeOf.at(i) != k;
		}
		const std::vector<int> fromOthers = stepsFrom(t, others);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (labels.at(i) >= 0 && owners.at(labels.at(i)) == k && fromOthers.at(i) >= 0 &&
				fromOthers.at(i) < gap)
				labels.at(i) = -1;
		}
	}
	separateTerritories(t, labels, gap);
	// A strip of field narrower than its rim on each side would be all beach, wall and cap with no crop
	// row in it, and where such a strip joined the field the walls of the coasts either side would meet
	// across it and seal it off. So a field keeps only the ground whose core (the field and its home
	// shrunk by the opening) is joined to the home's core, grown back out over the field; that is the
	// field opened, with the joining judged on the cores rather than the outlines, since two cores that
	// do not touch can still overlap once grown back, through a waist their walls would close.
	for (size_t s = 0; s < seeds.size(); ++s)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<unsigned char> inside(n, 0), home(n, 0);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			inside.at(i) = labels.at(i) == int(s) || homeOf.at(i) == owners.at(s);
			home.at(i) = homeOf.at(i) == owners.at(s);
		}
		const std::vector<unsigned char> core = erode(t, inside, opening);
		std::vector<unsigned char> homeCore(n, 0);
		bool any = false;
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			homeCore.at(i) = core.at(i) && home.at(i);
			any = any || homeCore.at(i);
		}
		// A home too small to have a core of its own (none of the maps' homes are) joins from its whole.
		const std::vector<int> joined = stepsFrom(t, any ? homeCore : home, core);
		std::vector<unsigned char> kept(n, 0);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			kept.at(i) = joined.at(i) >= 0;
		}
		kept = dilate(t, kept, opening);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (labels.at(i) == int(s) && !kept.at(i))
				labels.at(i) = -1;
		}
	}
	// However the trimming left it, every field is joined to its home by a neck the full width from the
	// home's middle to the field's nearest ground, wide enough that its beaches and walls leave a broad
	// opening between them.
	for (size_t s = 0; s < seeds.size() && s < anchors.size(); ++s)
	{
		::MapGeneration::generationCheckpoint();
		const int ax = int(::MapGeneration::Numeric::lround(anchors.at(s).x)),
				  ay = int(::MapGeneration::Numeric::lround(anchors.at(s).y));
		int nearest = -1, best = 0;
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (labels.at(i) == int(s))
			{
				const int d = t.dist2(ax, ay, t.remainderX(i), i / t.w);
				if (nearest < 0 || d < best)
				{
					nearest = i;
					best = d;
				}
			}
		}
		if (nearest < 0)
			continue;
		std::vector<unsigned char> neck(n, 0), otherFields(n, 0);
		for (int i = 0; i < n; ++i)
		{
			::MapGeneration::generationCheckpoint();
			otherFields.at(i) = labels.at(i) >= 0 && labels.at(i) != int(s);
		}
		const std::vector<int> fromOtherFields = stepsFrom(t, otherFields);
		const double nx = ax + t.offsetX(ax, t.remainderX(nearest)), ny = ay + t.offsetY(ay, nearest / t.w);
		strokePath(neck, t,
				   {{anchors.at(s).x, anchors.at(s).y, neckHalfWidth}, {nx, ny, neckHalfWidth}});
		for (int i = 0; i < n; ++i)
			// The neck keeps the same gap from land that is no home, and from every other field, as the
			// field does, so it never runs round the end of a wall or into someone else's farm.
		{
			::MapGeneration::generationCheckpoint();
			if (neck.at(i) && !occupied.at(i) && labels.at(i) < 0 &&
				(fromForeign.at(i) < 0 || fromForeign.at(i) >= gap) &&
				(fromOtherFields.at(i) < 0 || fromOtherFields.at(i) >= gap))
				labels.at(i) = int(s);
		}
	}
	return labels;
}

double farmReachable(const Map &map, const Torus &t, const Farm &farm,
					 const std::vector<int> &sources)
{
	const int n = t.size();
	std::vector<unsigned char> open(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		open.at(i) = map.terrainPropertiesAt(x, y).walkable && map.getBuilding(x, y) == NOGBID &&
					 !permanentResourceBarrier(map, i);
	}
	const std::vector<int> steps = stepsFrom(t, tileMask(t, sources), open);
	int land = 0, reached = 0;
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (farm.plot.at(i) && steps.at(i) < 0)
			return 0;
		// Crop land is the rows' grass: the sand lane outside a sealed coast is not the farm's to work.
		if (farm.row.at(i) < 0 || farm.row.at(i) % 2 || !open.at(i) ||
			!map.terrainSupportsMaterialAt(t.remainderX(i), i / t.w, MaterialId::Food))
			continue;
		++land;
		reached += steps.at(i) >= 0;
	}
	return land ? double(reached) / land : 0;
}

void clearFarmPlots(Map &map, const Torus &t, const std::vector<Farm> &farms)
{
	for (const Farm &farm : farms)
	{
		::MapGeneration::generationCheckpoint();
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (farm.plot.at(i) && map.isResource(t.remainderX(i), i / t.w))
				map.setNoResource(t.remainderX(i), i / t.w, 1);
		}
	}
}

std::vector<unsigned char> stampSealedOval(TerrainSketch &terrain, const Torus &t,
										   const std::vector<unsigned char> &ground,
										   const SealedOval &oval, const std::vector<int> &swayNoise)
{
	const int n = t.size();
	std::vector<unsigned char> garden(n, 0);
	// Distance in the oval's frame: along the rim's tangent squeezed by the stretch.
	const double ca = ::MapGeneration::Numeric::cos(oval.angle),
				 sa = ::MapGeneration::Numeric::sin(oval.angle);
	const auto ovalDistance = [&](double dx, double dy)
	{
		const double across = dx * ca + dy * sa, along = -dx * sa + dy * ca;
		return ::MapGeneration::Numeric::hypot(along / oval.stretch, across);
	};
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!ground.at(i))
			continue;
		const double dx = t.offsetX(int(::MapGeneration::Numeric::lround(oval.x)), t.remainderX(i)) -
						  (oval.x - ::MapGeneration::Numeric::lround(oval.x));
		const double dy = t.offsetY(int(::MapGeneration::Numeric::lround(oval.y)), i / t.w) -
						  (oval.y - ::MapGeneration::Numeric::lround(oval.y));
		const double radius = oval.radius + (swayNoise.at(i) / 65536.0 * 2 - 1) * oval.sway;
		const double d = ovalDistance(dx, dy);
		if (d >= radius && d < radius + oval.sealWidth)
			terrain.at(i) = SAND;
		// A tile's middle is half a tile past its corner.
		if (ovalDistance(dx + 0.5, dy + 0.5) < radius - 0.7)
			garden.at(i) = 1;
	}
	return garden;
}

std::pair<int, int> plantSealedGarden(Map &map, const Torus &t, GenerationContext &context,
									  const std::vector<unsigned char> &garden, int wheat, int wood,
									  const std::string &cropsStream, const std::string &splitStream)
{
	const int n = t.size();
	const auto plot = [&](int i) { return garden.at(i) && clearGround(map, t.remainderX(i), i / t.w); };
	const int tiles = int(std::count(garden.begin(), garden.end(), 1));
	const int wheatWanted = std::min(wheat, tiles / 2);
	const int woodWanted = std::min(wood, tiles * 2 / 5);
	const std::vector<int> order = periodicNoise(t.w, t.h, 3, context.stream(cropsStream));
	const std::vector<int> split = periodicNoise(t.w, t.h, 4, context.stream(splitStream));
	std::vector<int> plotTiles;
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (plot(i))
			plotTiles.push_back(i);
	}
	std::stable_sort(plotTiles.begin(), plotTiles.end(),
					 [&](int a, int b) { return order.at(a) > order.at(b); });
	plantFields(map, t, plotTiles, wheatWanted, woodWanted, [&](int i) { return split.at(i); });
	std::pair<int, int> standing{0, 0};
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (garden.at(i))
		{
			standing.first += map.materialAmountAt(i, MaterialId::Food) > 0;
			standing.second += map.materialAmountAt(i, MaterialId::Wood) > 0;
		}
	}
	return standing;
}

int trimFieldsBeyondWater(Map &map, const Torus &t, const std::vector<unsigned char> &keep,
						  const Fertility::Field &watered, int leastReach, int reachSpread,
						  const std::vector<int> &noise)
{
	const std::vector<std::int64_t> toWater = distanceSquaredTo(t, pureTiles(map, WATER));
	int trimmed = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		const int type = map.getResource(x, y).type;
		if (type != WHEAT && type != WOOD)
			continue;
		const std::int64_t reach = leastReach + noise.at(i) * (reachSpread + 1) / 65536;
		if (!keep.at(i) && watered.at(x, y) > 0 && toWater.at(i) > reach * reach)
		{
			map.setNoResource(x, y, 0);
			++trimmed;
		}
	}
	return trimmed;
}

int frayFieldEdges(Map &map, const Torus &t, const std::vector<unsigned char> &keep, int depth,
				   const std::vector<int> &noise)
{
	const int n = t.size();
	std::vector<unsigned char> unplanted(n, 0);
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int type = map.getResource(t.remainderX(i), i / t.w).type;
		unplanted.at(i) = type != WHEAT && type != WOOD;
	}
	const std::vector<int> intoField = stepsFrom(t, unplanted);
	int frayed = 0;
	for (int i = 0; i < n; ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		const int type = map.getResource(x, y).type;
		if ((type == WHEAT || type == WOOD) && !keep.at(i) &&
			intoField.at(i) <= noise.at(i) * (depth + 1) / 65536)
		{
			map.setNoResource(x, y, 0);
			++frayed;
		}
	}
	return frayed;
}

int removeCropSlivers(Map &map, const Torus &t, const std::vector<unsigned char> &protect)
{
	int slivers = 0;
	for (int pass = 0; pass < 2; ++pass)
	{
		::MapGeneration::generationCheckpoint();
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			const int x = t.remainderX(i), y = i / t.w;
			const int type = map.getResource(x, y).type;
			if ((type != WHEAT && type != WOOD) || protect.at(i))
				continue;
			const auto open = [&](int dx, int dy)
			{
				const int j = t.at(x + dx, y + dy);
				return map.terrainSupportsResourceAtByIndex(t.remainderX(j),j / t.w,WHEAT) && !map.isResource(t.remainderX(j), j / t.w) &&
					   map.getBuilding(t.remainderX(j), j / t.w) == NOGBID;
			};
			if ((open(-1, 0) && open(1, 0)) || (open(0, -1) && open(0, 1)))
			{
				map.setNoResource(x, y, 0);
				++slivers;
			}
		}
	}
	return slivers;
}
} // namespace MapGeneration
