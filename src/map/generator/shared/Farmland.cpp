// SPDX-License-Identifier: GPL-3.0-or-later
#include "Farmland.h"
#include "Morphology.h"
#include "Map.h"
#include "Resources.h"
#include "Drawing.h"
#include "Territories.h"
#include "Topology.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
namespace
{
// How far from its point a field's seed may move to find open ground.
constexpr int kSeedSearch = 16;
// A field loses any strip narrower than twice this: the beach and wall each side would fill it.
constexpr int kOpening = 3;
} // namespace

FarmRows bestFarmRows(double angle)
{
	// Rows repeat every quarter turn and mirror about the diagonal, so only the angle from the nearest
	// axis matters, 0 to 45 degrees.
	double a = std::fmod(std::abs(angle), kPi / 2);
	a = std::min(a, kPi / 2 - a);
	const double diagonal = std::min(1.0, 2 * std::sin(2 * a));
	return {10 + 2 * diagonal, 8 + diagonal};
}

double farmYield(double angle)
{
	// tools/farm_row_fit.py's best yields at 0, 11.25, 22.5, 33.75 and 45 degrees from an axis.
	constexpr double yields[] = {0.1489, 0.1322, 0.1225, 0.1141, 0.1126};
	double a = std::fmod(std::abs(angle), kPi / 2);
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
		for (int dx = -margin; dx <= plot.width + margin; ++dx)
		{
			const int i = t.at(x0 + dx, y0 + dy);
			const bool grass = dx >= 0 && dx <= plot.width && dy >= 0 && dy <= plot.height;
			const bool ring = !grass && dx >= -plot.ring && dx <= plot.width + plot.ring &&
							  dy >= -plot.ring && dy <= plot.height + plot.ring;
			// The clearing trumps the rows and bridges alike: its grass is grass.
			if (farm.water[i] || (grass && farm.sand[i]))
			{
				farm.water[i] = 0;
				farm.sand[i] = 0;
				sketch[i] = GRASS;
			}
			if (ring)
			{
				farm.sand[i] = 1;
				sketch[i] = SAND;
			}
		}
	for (int dy = 0; dy < plot.height; ++dy)
		for (int dx = 0; dx < plot.width; ++dx)
			farm.plot[t.at(x0 + dx, y0 + dy)] = 1;
}

Farm layFarm(TerrainSketch &sketch, const Torus &t, const std::vector<unsigned char> &region,
			 double angle, ShapePoint origin, int rim, const FarmRows &rows, const FarmPlot *plot,
			 int bridgeSpacing, bool caps)
{
	const int n = t.size();
	Farm farm;
	farm.water.assign(n, 0);
	farm.row.assign(n, -1);
	std::vector<unsigned char> outside(n, 0);
	for (int i = 0; i < n; ++i)
		outside[i] = !region[i];
	const std::vector<int> fromEdge = stepsFrom(t, outside);
	// Across the rows: the rows run along `angle`, so a vertex's place across them is its offset along
	// the normal, shifted so the crop row at the origin is centred on it.
	const double nx = -std::sin(angle), ny = std::cos(angle), period = rows.period();
	const int ox = int(std::lround(origin.x)), oy = int(std::lround(origin.y));
	std::vector<int> cycle(n, 0);
	std::vector<unsigned char> wet(n, 0);
	int lowest = 0;
	bool any = false;
	for (int i = 0; i < n; ++i)
	{
		if (!region[i])
			continue;
		const double across =
			t.offsetX(ox, i % t.w) * nx + t.offsetY(oy, i / t.w) * ny + rows.crops / 2;
		cycle[i] = int(std::floor(across / period));
		wet[i] = across - cycle[i] * period >= rows.crops;
		lowest = any ? std::min(lowest, cycle[i]) : cycle[i];
		any = true;
	}
	// Rows are numbered from zero at the lowest, so a row's parity says crops (even) or water (odd).
	std::vector<unsigned char> seen;
	for (int i = 0; i < n; ++i)
	{
		if (!region[i])
			continue;
		farm.row[i] = 2 * (cycle[i] - lowest) + wet[i];
		if (wet[i] && fromEdge[i] >= rim)
		{
			farm.water[i] = 1;
			sketch[i] = WATER;
			if (farm.row[i] >= int(seen.size()))
				seen.resize(farm.row[i] + 1, 0);
			seen[farm.row[i]] = 1;
		}
	}
	farm.sand.assign(n, 0);
	farm.plot.assign(n, 0);
	// Caps: a ring of sand vertices round the field just inside its rim, where the water rows' beaches
	// begin, closing every crop row's ends and outer sides, so wheat and wood cannot grow out of the rows
	// into the ground round the farm (a home it opens into). Sand is walkable, so workers still cross.
	if (caps && rim >= 2)
		for (int i = 0; i < n; ++i)
			if (region[i] && !farm.water[i] && fromEdge[i] == rim - 1)
			{
				farm.sand[i] = 1;
				sketch[i] = SAND;
			}
	// Bridges: every `bridgeSpacing` tiles along the rows, a line of sand vertices straight across every
	// water row. The tiles either side of such a line are no longer pure water, so workers walk across,
	// two tiles wide, instead of round the end of a long row.
	if (bridgeSpacing > 0)
	{
		const double ax = std::cos(angle), ay = std::sin(angle);
		for (int i = 0; i < n; ++i)
		{
			if (!farm.water[i])
				continue;
			const double along = t.offsetX(ox, i % t.w) * ax + t.offsetY(oy, i / t.w) * ay;
			const double off = along - bridgeSpacing * std::round(along / bridgeSpacing);
			if (std::abs(off) < kBridgeHalfWidth)
			{
				farm.water[i] = 0;
				farm.sand[i] = 1;
				sketch[i] = SAND;
			}
		}
	}
	if (plot)
	{
		// The clearing's middle: the region's most inland vertex, the first on a tie.
		int middle = -1;
		for (int i = 0; i < n; ++i)
			if (region[i] && (middle < 0 || fromEdge[i] > fromEdge[middle]))
				middle = i;
		const int margin = plot->ring + 1;
		const int x0 = middle % t.w - plot->width / 2, y0 = middle / t.w - plot->height / 2;
		// The grass tiles span vertices x0..x0+width by y0..y0+height; the ring and a vertex more
		// round them must lie as far inside the region as its water rows, so the ring's sand never
		// meets the region's own beach.
		bool fits = middle >= 0;
		for (int dy = -margin; dy <= plot->height + margin && fits; ++dy)
			for (int dx = -margin; dx <= plot->width + margin && fits; ++dx)
				fits = fromEdge[t.at(x0 + dx, y0 + dy)] >= rim;
		if (fits)
		{
			farm.plotX = t.x(x0);
			farm.plotY = t.y(y0);
			stampFarmPlot(sketch, t, farm, x0, y0, *plot);
			seen.assign(seen.size(), 0);
			for (int i = 0; i < n; ++i)
				if (farm.water[i])
					seen[farm.row[i]] = 1;
		}
	}
	farm.rows = int(std::count(seen.begin(), seen.end(), 1));
	return farm;
}

std::vector<int> growFarmFields(const Torus &t, const std::vector<unsigned char> &occupied,
								const std::vector<int> &homeOf,
								const std::vector<unsigned char> &area,
								const std::vector<ShapePoint> &seeds,
								const std::vector<int> &owners,
								const std::vector<ShapePoint> &anchors, int gap,
								double neckHalfWidth, const std::vector<double> &rowAngles)
{
	const int n = t.size();
	// Open ground: not designed, allowed, and clear of all land that is no home by the gap. Homes'
	// surroundings stay open here; each field is held off other colonies' homes once grown.
	std::vector<unsigned char> foreign(n, 0);
	for (int i = 0; i < n; ++i)
		foreign[i] = occupied[i] && homeOf[i] < 0;
	const std::vector<int> fromForeign = stepsFrom(t, foreign);
	std::vector<unsigned char> free(n, 0);
	for (int i = 0; i < n; ++i)
		free[i] = area[i] && !occupied[i] && (fromForeign[i] < 0 || fromForeign[i] >= gap);
	// Each seed starts from the free tile near its point that lies in the biggest stretch of free ground
	// (nearest the point on a tie), so a field never starts boxed in a pocket beside its home.
	const std::vector<int> stretch = connectedRegions(free, t.w, t.h, true);
	std::vector<int> stretchSize;
	for (int label : stretch)
		if (label >= 0)
		{
			if (label >= int(stretchSize.size()))
				stretchSize.resize(label + 1, 0);
			++stretchSize[label];
		}
	std::vector<std::vector<int>> starts(seeds.size());
	std::vector<ShapePoint> snapped(seeds);
	for (size_t s = 0; s < seeds.size(); ++s)
	{
		const int sx = int(std::lround(seeds[s].x)), sy = int(std::lround(seeds[s].y));
		int best = -1, bestSize = 0, bestDistance = 0;
		for (int dy = -kSeedSearch; dy <= kSeedSearch; ++dy)
			for (int dx = -kSeedSearch; dx <= kSeedSearch; ++dx)
			{
				const int i = t.at(sx + dx, sy + dy);
				if (!free[i])
					continue;
				const int size = stretchSize[stretch[i]], distance = dx * dx + dy * dy;
				if (best < 0 || size > bestSize || (size == bestSize && distance < bestDistance))
				{
					best = i;
					bestSize = size;
					bestDistance = distance;
				}
			}
		if (best < 0)
			continue;
		snapped[s] = {double(sx + t.offsetX(sx, best % t.w)),
					  double(sy + t.offsetY(sy, best / t.w))};
		starts[s].push_back(best);
	}
	std::vector<double> worth;
	for (double angle : rowAngles)
		worth.push_back(farmYield(angle));
	std::vector<int> labels =
		growTerritories(
			t, free, starts, [](int) { return 0; }, worth.size() == seeds.size() ? &worth : nullptr)
			.labels;
	// Every field keeps the gap from every home but its own.
	int colonies = 0;
	for (int owner : owners)
		colonies = std::max(colonies, owner + 1);
	for (int k = 0; k < colonies; ++k)
	{
		std::vector<unsigned char> others(n, 0);
		for (int i = 0; i < n; ++i)
			others[i] = homeOf[i] >= 0 && homeOf[i] != k;
		const std::vector<int> fromOthers = stepsFrom(t, others);
		for (int i = 0; i < n; ++i)
			if (labels[i] >= 0 && owners[labels[i]] == k && fromOthers[i] >= 0 &&
				fromOthers[i] < gap)
				labels[i] = -1;
	}
	separateTerritories(t, labels, gap);
	// A strip of field narrower than a beach and a wall on each side would be all sand and stone, and
	// cut the ground beyond it off: every field is opened (shrunk by kOpening, then grown back as far
	// within itself), which removes such strips and keeps the rest.
	for (size_t s = 0; s < seeds.size(); ++s)
	{
		std::vector<unsigned char> inside(n, 0);
		for (int i = 0; i < n; ++i)
			inside[i] = labels[i] == int(s) || homeOf[i] == owners[s];
		const std::vector<unsigned char> opened = openMask(t, inside, kOpening);
		for (int i = 0; i < n; ++i)
			if (labels[i] == int(s) && !opened[i])
				labels[i] = -1;
	}
	// Trimming a field to its gaps can leave slivers cut off from the rest; a field keeps only the
	// ground joined to its own home.
	for (size_t s = 0; s < seeds.size(); ++s)
	{
		std::vector<unsigned char> mine(n, 0), home(n, 0);
		for (int i = 0; i < n; ++i)
		{
			mine[i] = labels[i] == int(s) || homeOf[i] == owners[s];
			home[i] = homeOf[i] == owners[s];
		}
		const std::vector<int> joined = stepsFrom(t, home, mine);
		for (int i = 0; i < n; ++i)
			if (labels[i] == int(s) && joined[i] < 0)
				labels[i] = -1;
	}
	// However the trimming left it, every field is joined to its home by a neck the full width from the
	// home's middle to the field's nearest ground, wide enough that its beaches and walls leave a broad
	// opening between them.
	for (size_t s = 0; s < seeds.size() && s < anchors.size(); ++s)
	{
		const int ax = int(std::lround(anchors[s].x)), ay = int(std::lround(anchors[s].y));
		int nearest = -1, best = 0;
		for (int i = 0; i < n; ++i)
			if (labels[i] == int(s))
			{
				const int d = t.dist2(ax, ay, i % t.w, i / t.w);
				if (nearest < 0 || d < best)
				{
					nearest = i;
					best = d;
				}
			}
		if (nearest < 0)
			continue;
		std::vector<unsigned char> neck(n, 0), otherFields(n, 0);
		for (int i = 0; i < n; ++i)
			otherFields[i] = labels[i] >= 0 && labels[i] != int(s);
		const std::vector<int> fromOtherFields = stepsFrom(t, otherFields);
		const double nx = ax + t.offsetX(ax, nearest % t.w), ny = ay + t.offsetY(ay, nearest / t.w);
		strokePath(neck, t, {{anchors[s].x, anchors[s].y, neckHalfWidth}, {nx, ny, neckHalfWidth}});
		for (int i = 0; i < n; ++i)
			// The neck keeps the same gap from land that is no home, and from every other field, as the
			// field does, so it never runs round the end of a wall or into someone else's farm.
			if (neck[i] && !occupied[i] && labels[i] < 0 &&
				(fromForeign[i] < 0 || fromForeign[i] >= gap) &&
				(fromOtherFields[i] < 0 || fromOtherFields[i] >= gap))
				labels[i] = int(s);
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
		const int x = i % t.w, y = i / t.w;
		const bool deposit = map.isResource(x, y);
		open[i] =
			!map.isWater(x, y) && map.getBuilding(x, y) == NOGBID &&
			(!deposit || map.getResource(x, y).type == CORN || map.getResource(x, y).type == WOOD);
	}
	const std::vector<int> steps = stepsFrom(t, tileMask(t, sources), open);
	int land = 0, reached = 0;
	for (int i = 0; i < n; ++i)
	{
		if (farm.plot[i] && steps[i] < 0)
			return 0;
		// Crop land is the rows' grass: the sand lane outside a sealed coast is not the farm's to work.
		if (farm.row[i] < 0 || farm.row[i] % 2 || !open[i] || !map.isGrass(i % t.w, i / t.w))
			continue;
		++land;
		reached += steps[i] >= 0;
	}
	return land ? double(reached) / land : 0;
}

void clearFarmPlots(Map &map, const Torus &t, const std::vector<Farm> &farms)
{
	for (const Farm &farm : farms)
		for (int i = 0; i < t.size(); ++i)
			if (farm.plot[i] && map.isResource(i % t.w, i / t.w))
				map.setNoResource(i % t.w, i / t.w, 1);
}
} // namespace MapGeneration
