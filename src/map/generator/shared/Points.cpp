#include "GenerationWork.h"
#include "GenerationNumeric.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
#include <PerformanceTelemetry.h>
#include "Points.h"
#include <cmath>
#include "GenerationContext.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <functional>
#include <cstdlib>
#include <utility>
namespace MapGeneration
{
std::vector<int> spreadRankedSites(const Torus &t, const std::vector<RankedSite> &sites, int count,
								   int minimumSpacing, size_t first)
{
	if (count < 1 || minimumSpacing < 1 || first >= sites.size())
		return {};
	double maximumWeight = 0;
	for (const auto &site : sites)
	{
		::MapGeneration::generationCheckpoint();
		if (site.tile < 0 || site.tile >= t.size() || !std::isfinite(site.weight) ||
			site.weight <= 0)
			return {};
		maximumWeight = std::max(maximumWeight, site.weight);
	}
	std::vector<int> picked{sites.at(first).tile};
	while (int(picked.size()) < count)
	{
		::MapGeneration::generationCheckpoint();
		double best = -1;
		int chosen = -1;
		for (const auto &site : sites)
		{
			::MapGeneration::generationCheckpoint();
			int distance = INT_MAX;
			for (int p : picked)
			{
				::MapGeneration::generationCheckpoint();
				distance =
					std::min(distance, t.dist2(t.remainderX(site.tile), site.tile / t.w, t.remainderX(p), p / t.w));
			}
			if (distance < static_cast<long long>(minimumSpacing) * minimumSpacing)
				continue;
			// Normalize by one common factor to avoid overflow for large finite weights.
			const double score = distance * (site.weight / maximumWeight);
			if (score > best)
			{
				best = score;
				chosen = site.tile;
			}
		}
		if (chosen < 0)
			break;
		picked.push_back(chosen);
	}
	return picked;
}

namespace
{
// Bucket rows or columns to search around bucket c: all of them when a window would wrap onto
// itself.
std::vector<int> bucketWindow(int c, int count, int radius)
{
	std::vector<int> result;
	if (count <= 2 * radius + 1)
	{
		for (int i = 0; i < count; ++i)
		{
			::MapGeneration::generationCheckpoint();
			result.push_back(i);
		}
	}
	else
	{
		for (int d = -radius; d <= radius; ++d)
		{
			::MapGeneration::generationCheckpoint();
			result.push_back(((c + d) % count + count) % count);
		}
	}
	return result;
}
} // namespace

NearestSites nearestTwoSites(const Torus &t, const std::vector<Site> &sites, int x, int y)
{
	NearestSites result;
	for (int k = 0; k < int(sites.size()); ++k)
	{
		::MapGeneration::generationCheckpoint();
		const int d = t.dist2(sites.at(k).x, sites.at(k).y, x, y);
		// Strict comparisons retain the lower input index on ties. Demote the
		// former closest site as a unit so owner and distance never get separated.
		if (result.first < 0 || d < result.firstDistanceSquared)
		{
			result.second = result.first;
			result.secondDistanceSquared = result.firstDistanceSquared;
			result.first = k;
			result.firstDistanceSquared = d;
		}
		else if (result.second < 0 || d < result.secondDistanceSquared)
		{
			result.second = k;
			result.secondDistanceSquared = d;
		}
	}
	return result;
}

std::vector<Site> spreadPoints(const Torus &t, int spacing, GenerationContext &context,
							   const std::string &stream, int minimumPercent, int dartsPerSite)
{
	const int minimum = std::max(4, spacing * minimumPercent / 100);
	const std::int64_t expected =
		std::max<std::int64_t>(2, std::int64_t(t.w) * t.h / (std::int64_t(spacing) * spacing));
	const int gx = std::max(1, t.w / minimum), gy = std::max(1, t.h / minimum);
	std::vector<std::vector<int>> columns(gx), rows(gy);
	for (int c = 0; c < gx; ++c)
	{
		::MapGeneration::generationCheckpoint();
		columns.at(c) = bucketWindow(c, gx, 1);
	}
	for (int r = 0; r < gy; ++r)
	{
		::MapGeneration::generationCheckpoint();
		rows.at(r) = bucketWindow(r, gy, 1);
	}
	generationAllocation(std::uint64_t(gx) * gy * sizeof(std::vector<int>));
	std::vector<std::vector<int>> buckets(size_t(gx) * gy);
	std::vector<Site> sites;
	for (std::int64_t attempt = 0; attempt < expected * dartsPerSite; ++attempt)
	{
		::MapGeneration::generationCheckpoint();
		const int x = int(context.bounded(stream, t.w));
		const int y = int(context.bounded(stream, t.h));
		const int bx = x * gx / t.w, by = y * gy / t.h;
		bool clear = true;
		for (int row : rows.at(by))
		{
			::MapGeneration::generationCheckpoint();
			for (int column : columns.at(bx))
			{
				::MapGeneration::generationCheckpoint();
				for (int s : buckets.at(size_t(row) * gx + column))
				{
					::MapGeneration::generationCheckpoint();
					if (t.dist2(x, y, sites.at(s).x, sites.at(s).y) < minimum * minimum)
					{
						clear = false;
						break;
					}
				}
				if (!clear)
					break;
			}
			if (!clear)
				break;
		}
		if (!clear)
			continue;
		buckets.at(size_t(by) * gx + bx).push_back(int(sites.size()));
		sites.push_back({x, y});
	}
	return sites;
}

namespace
{
// The eight headings: east, then round by about 22 degrees to north-east of west. Small whole
// numbers, so every landform along one lands the same way on every platform.
constexpr int kHeadingSteps[kGrainHeadings][2] = {{1, 0}, {2, 1},  {1, 1},  {1, 2},
												  {0, 1}, {-1, 2}, {-1, 1}, {-2, 1}};
} // namespace

Grain grainHeading(int index, int stretchPercent)
{
	const int k = ((index % kGrainHeadings) + kGrainHeadings) % kGrainHeadings;
	return Grain{kHeadingSteps[k][0], kHeadingSteps[k][1], std::max(100, stretchPercent)};
}

Grain grainForChoice(int choice, int stretchPercent, GenerationContext &context,
					 const std::string &stream)
{
	const int fixed[] = {0, 4, 2};
	const int index = choice >= 1 && choice <= 3 ? fixed[choice - 1]
												 : int(context.bounded(stream, kGrainHeadings));
	return grainHeading(index, stretchPercent);
}

int widestGrainHeading(const Torus &t, const std::vector<Site> &sites, int stretchPercent)
{
	int best = 0;
	double widest = -1;
	for (int k = 0; k < kGrainHeadings; ++k)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<double> nearest =
			nearestSiteDistances(t, sites, grainHeading(k, stretchPercent));
		const double least = nearest.empty() ? double(std::min(t.w, t.h))
											 : *std::min_element(nearest.begin(), nearest.end());
		if (least > widest)
		{
			widest = least;
			best = k;
		}
	}
	return best;
}

std::vector<int> farthestSites(const Torus &t, const std::vector<Site> &sites,
							   const std::vector<int> &seeds,
							   const std::vector<unsigned char> &eligible, int count)
{
	const size_t n = sites.size();
	std::vector<std::int64_t> nearest(n, -1);
	const auto measureFrom = [&](int from)
	{
		for (size_t s = 0; s < n; ++s)
		{
			::MapGeneration::generationCheckpoint();
			const std::int64_t d =
				t.dist2(sites.at(s).x, sites.at(s).y, sites.at(from).x, sites.at(from).y);
			nearest.at(s) = nearest.at(s) < 0 ? d : std::min(nearest.at(s), d);
		}
	};
	std::vector<unsigned char> taken(n, 0);
	for (int seed : seeds)
	{
		::MapGeneration::generationCheckpoint();
		measureFrom(seed);
		taken.at(seed) = 1; // a seed is never chosen, whatever `eligible` says of it
	}
	std::vector<int> chosen;
	for (int k = 0; k < count; ++k)
	{
		::MapGeneration::generationCheckpoint();
		int pick = -1;
		for (size_t s = 0; s < n; ++s)
		{
			::MapGeneration::generationCheckpoint();
			if (eligible.at(s) && !taken.at(s) && (pick < 0 || nearest.at(s) > nearest.at(pick)))
				pick = int(s);
		}
		if (pick < 0)
			break;
		taken.at(pick) = 1;
		chosen.push_back(pick);
		measureFrom(pick);
	}
	return chosen;
}

std::vector<Site> spreadPoints(const Torus &t, int spacing, const Grain &grain,
							   GenerationContext &context, const std::string &stream,
							   const std::vector<Site> &fixed, int fixedMinimum, int minimumPercent,
							   int dartsPerSite)
{
	PERF_SCOPE_TIME(Sites);
	const int minimum = std::max(4, spacing * minimumPercent / 100);
	const int stretch = std::max(100, grain.stretchPercent);
	// One site is expected per spacing squared across by spacing times the stretch along.
	const std::int64_t expected = std::max<std::int64_t>(
		2, std::int64_t(t.w) * t.h * 100 / (std::int64_t(spacing) * spacing * stretch));
	// Under the grain a dart within `minimum` of a site lies within `minimum` tiles of it across
	// the grain and within `minimum` times the stretch along it, so in map tiles it lies within
	// minimum * stretch / 100 either way: buckets of `minimum` tiles searched that many buckets
	// out. A fixed site's exclusion is wider, so the window is sized to the wider of the two.
	const int widest = std::max(minimum, fixedMinimum);
	const int gx = std::max(1, t.w / minimum), gy = std::max(1, t.h / minimum);
	const int window = (widest * stretch / 100 + minimum - 1) / minimum;
	std::vector<std::vector<int>> columns(gx), rows(gy);
	for (int c = 0; c < gx; ++c)
	{
		::MapGeneration::generationCheckpoint();
		columns.at(c) = bucketWindow(c, gx, window);
	}
	for (int r = 0; r < gy; ++r)
	{
		::MapGeneration::generationCheckpoint();
		rows.at(r) = bucketWindow(r, gy, window);
	}
	generationAllocation(std::uint64_t(gx) * gy * sizeof(std::vector<int>));
	std::vector<std::vector<int>> buckets(size_t(gx) * gy);
	std::vector<Site> sites;
	const std::int64_t clear = grain.metric2(minimum), clearFixed = grain.metric2(fixedMinimum);
	const auto place = [&](int x, int y)
	{
		buckets.at(size_t(y * gy / t.h) * gx + x * gx / t.w).push_back(int(sites.size()));
		sites.push_back({x, y});
	};
	for (const Site &site : fixed)
	{
		::MapGeneration::generationCheckpoint();
		place(t.x(site.x), t.y(site.y));
	}
	const size_t fixedCount = sites.size();
	for (std::int64_t attempt = 0; attempt < expected * dartsPerSite; ++attempt)
	{
		::MapGeneration::generationCheckpoint();
		const int x = int(context.bounded(stream, t.w));
		const int y = int(context.bounded(stream, t.h));
		const int bx = x * gx / t.w, by = y * gy / t.h;
		bool free = true;
		for (int row : rows.at(by))
		{
			::MapGeneration::generationCheckpoint();
			for (int column : columns.at(bx))
			{
				::MapGeneration::generationCheckpoint();
				for (int s : buckets.at(size_t(row) * gx + column))
				{
					::MapGeneration::generationCheckpoint();
					const std::int64_t d = grain.distance2(t, sites.at(s).x, sites.at(s).y, x, y);
					if (d < (size_t(s) < fixedCount ? clearFixed : clear))
					{
						free = false;
						break;
					}
				}
				if (!free)
					break;
			}
			if (!free)
				break;
		}
		if (free)
			place(x, y);
	}
	return sites;
}

std::vector<double> nearestSiteDistances(const Torus &t, const std::vector<Site> &sites,
										 const Grain &grain)
{
	std::vector<double> nearest(sites.size(), double(std::min(t.w, t.h)));
	for (size_t a = 0; a < sites.size(); ++a)
	{
		::MapGeneration::generationCheckpoint();
		for (size_t b = a + 1; b < sites.size(); ++b)
		{
			::MapGeneration::generationCheckpoint();
			const double d =
				grain.distance(t, sites.at(a).x, sites.at(a).y, sites.at(b).x, sites.at(b).y);
			nearest.at(a) = std::min(nearest.at(a), d);
			nearest.at(b) = std::min(nearest.at(b), d);
		}
	}
	return nearest;
}

std::vector<int> nearestSiteLabels(const Torus &t, const std::vector<Site> &sites, int spacing,
								   const Grain &grain, int reach)
{
	PERF_SCOPE_TIME(Sites);
	std::vector<int> label(size_t(t.w) * t.h, 0);
	if (sites.empty())
		return label;
	if (reach <= 0)
		reach = 2 * spacing;
	// The window: the bounding box, in map tiles, of the ellipse `reach` under the grain (reach
	// across the grain, reach times the stretch along it), which is how far a site within reach can
	// lie on each axis; as buckets of a spacing, one more for the tile's own place in its bucket.
	// A bucket window along the grain is the stretch times deeper than across it, so a slanted
	// grain costs more buckets than an axis-aligned one, never more than the ellipse needs.
	const double stretch = std::max(100, grain.stretchPercent) / 100.0;
	const double c = ::MapGeneration::Numeric::cos(grain.heading()),
				 sn = ::MapGeneration::Numeric::sin(grain.heading());
	const double extentX = reach * ::MapGeneration::Numeric::hypot(c * stretch, sn),
				 extentY = reach * ::MapGeneration::Numeric::hypot(sn * stretch, c);
	const int gx = std::max(1, t.w / spacing), gy = std::max(1, t.h / spacing);
	const int windowX = int(::MapGeneration::Numeric::ceil(extentX * gx / t.w)) + 1,
			  windowY = int(::MapGeneration::Numeric::ceil(extentY * gy / t.h)) + 1;
	generationAllocation(std::uint64_t(gx) * gy * sizeof(std::vector<int>));
	std::vector<std::vector<int>> buckets(size_t(gx) * gy), columns(gx), rows(gy);
	for (size_t s = 0; s < sites.size(); ++s)
	{
		::MapGeneration::generationCheckpoint();
		buckets.at(size_t(sites.at(s).y * gy / t.h) * gx + sites.at(s).x * gx / t.w)
			.push_back(int(s));
	}
	for (int col = 0; col < gx; ++col)
	{
		::MapGeneration::generationCheckpoint();
		columns.at(col) = bucketWindow(col, gx, windowX);
	}
	for (int r = 0; r < gy; ++r)
	{
		::MapGeneration::generationCheckpoint();
		rows.at(r) = bucketWindow(r, gy, windowY);
	}
	const std::int64_t within = grain.metric2(reach);
	const int halfW = t.w / 2, halfH = t.h / 2;
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			std::int64_t best = INT64_MAX;
			int nearest = -1;
			// The inner loop of a whole-map labelling, run a few dozen times per tile: the wrap
			// is a comparison rather than Torus::offsetX's two remainders, and the far images
			// are tried only when Grain::distance2's bound says one could be nearer.
			const auto consider = [&](int s)
			{
				int dx = x - sites.at(s).x, dy = y - sites.at(s).y;
				if (dx > halfW)
					dx -= t.w;
				else if (dx < -halfW)
					dx += t.w;
				if (dy > halfH)
					dy -= t.h;
				else if (dy < -halfH)
					dy += t.h;
				std::int64_t d = grain.distance2(dx, dy);
				const std::int64_t reach2 = (std::int64_t(dx) * dx + std::int64_t(dy) * dy) *
												grain.stretchPercent * grain.stretchPercent /
												10000 +
											1;
				const int otherX = dx > 0 ? dx - t.w : dx + t.w,
						  otherY = dy > 0 ? dy - t.h : dy + t.h;
				const bool tryX = std::int64_t(otherX) * otherX <= reach2,
						   tryY = std::int64_t(otherY) * otherY <= reach2;
				if (tryX)
					d = std::min(d, grain.distance2(otherX, dy));
				if (tryY)
					d = std::min(d, grain.distance2(dx, otherY));
				if (tryX && tryY)
					d = std::min(d, grain.distance2(otherX, otherY));
				if (d < best || (d == best && s < nearest))
				{
					best = d;
					nearest = s;
				}
			};
			for (int row : rows.at(y * gy / t.h))
			{
				::MapGeneration::generationCheckpoint();
				for (int column : columns.at(x * gx / t.w))
				{
					::MapGeneration::generationCheckpoint();
					for (int s : buckets.at(size_t(row) * gx + column))
					{
						::MapGeneration::generationCheckpoint();
						consider(s);
					}
				}
			}
			// Nothing within reach in the window: nothing within reach anywhere, so the nearest
			// site, wherever it is, needs the whole list.
			if (nearest < 0 || best > within)
				for (size_t s = 0; s < sites.size(); ++s)
				{
					::MapGeneration::generationCheckpoint();
					consider(int(s));
				}
			label.at(size_t(y) * t.w + x) = nearest;
		}
	}
	return label;
}

std::vector<Site> relaxPoints(const Torus &t, std::vector<Site> sites, const Grain &grain,
							  int spacing, int iterations, int fixedCount, int reach)
{
	PERF_SCOPE_TIME(Relax);
	const int n = int(sites.size());
	for (int round = 0; round < iterations && n > 0; ++round)
	{
		::MapGeneration::generationCheckpoint();
		const std::vector<int> label = nearestSiteLabels(t, sites, spacing, grain, reach);
		std::vector<std::int64_t> sumX(n, 0), sumY(n, 0), count(n, 0);
		for (int y = 0; y < t.h; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < t.w; ++x)
			{
				::MapGeneration::generationCheckpoint();
				const int s = label.at(size_t(y) * t.w + x);
				// Offsets from the site, so a cell across the wrap averages the short way round.
				sumX.at(s) += t.offsetX(sites.at(s).x, x);
				sumY.at(s) += t.offsetY(sites.at(s).y, y);
				++count.at(s);
			}
		}
		for (int s = fixedCount; s < n; ++s)
		{
			::MapGeneration::generationCheckpoint();
			if (count.at(s))
			{
				// Round half away from zero, in integers, as relaxPoints does.
				const auto mean = [&](std::int64_t sum)
				{
					return int(sum >= 0 ? (2 * sum + count.at(s)) / (2 * count.at(s))
										: -((-2 * sum + count.at(s)) / (2 * count.at(s))));
				};
				sites.at(s).x = t.x(sites.at(s).x + mean(sumX.at(s)));
				sites.at(s).y = t.y(sites.at(s).y + mean(sumY.at(s)));
			}
		}
	}
	return sites;
}

std::vector<double> packLandforms(const Torus &t, const std::vector<Site> &sites,
								  const Grain &grain, const std::vector<double> &fixed, double gap,
								  double minimum, double maximum)
{
	const size_t n = sites.size();
	std::vector<double> radius(n, 0);
	// Each pair's distance under the grain, and the room the pair must keep under the grain for
	// `gap` map tiles between them: the gap scaled by the pair's distance under the grain over its
	// map distance, which is 1 for a pair across the grain and 1 / stretch for a pair along it,
	// because that ratio is exactly how much a map length in the pair's direction shrinks under
	// the grain.
	generationAllocation(std::uint64_t(n) *
						 (2 * sizeof(std::vector<double>) + std::uint64_t(n) * 2 * sizeof(double)));
	std::vector<std::vector<double>> distance(n, std::vector<double>(n, 0)),
		room(n, std::vector<double>(n, 0));
	for (size_t a = 0; a < n; ++a)
	{
		::MapGeneration::generationCheckpoint();
		for (size_t b = a + 1; b < n; ++b)
		{
			::MapGeneration::generationCheckpoint();
			// The pair's map distance is measured on the same image of the pair as its distance
			// under the grain, which may not be the nearest image in map tiles.
			const int dx = t.offsetX(sites.at(a).x, sites.at(b).x),
					  dy = t.offsetY(sites.at(a).y, sites.at(b).y);
			const int otherX = dx > 0 ? dx - t.w : dx + t.w, otherY = dy > 0 ? dy - t.h : dy + t.h;
			double under = 0, across = 0;
			for (const int ox : {dx, otherX})
			{
				::MapGeneration::generationCheckpoint();
				for (const int oy : {dy, otherY})
				{
					::MapGeneration::generationCheckpoint();
					if (const double d = grain.distance(ox, oy); across == 0 || d < under)
					{
						under = d;
						across = ::MapGeneration::Numeric::hypot(ox, oy);
					}
				}
			}
			distance.at(a).at(b) = distance.at(b).at(a) = under;
			room.at(a).at(b) = room.at(b).at(a) = across > 0 ? gap * under / across : gap;
		}
	}
	// Every free site starts at half its nearest neighbour's distance less that pair's gap: two
	// neighbours each taking that keep the gap between them whatever else is round them.
	for (size_t s = 0; s < n; ++s)
	{
		::MapGeneration::generationCheckpoint();
		if (fixed.at(s) >= 0)
		{
			radius.at(s) = fixed.at(s);
			continue;
		}
		double nearest = std::min(t.w, t.h);
		for (size_t o = 0; o < n; ++o)
		{
			::MapGeneration::generationCheckpoint();
			if (o != s)
				nearest = std::min(nearest, (distance.at(s).at(o) - room.at(s).at(o)) / 2);
		}
		radius.at(s) = std::clamp(nearest, 0.0, maximum);
	}
	// Then each grows into the slack its neighbours leave. Radii only ever grow, and each is set
	// against the others' current radii, so every pair keeps its gap after every step; three rounds
	// take up nearly all the slack (the fourth changes radii by under a tenth of a tile in
	// practice). Sites that end up under `minimum` are dropped, and one more round lets their
	// neighbours use the ground they gave up.
	const auto grow = [&](const std::vector<unsigned char> &dropped)
	{
		for (size_t s = 0; s < n; ++s)
		{
			::MapGeneration::generationCheckpoint();
			if (fixed.at(s) >= 0 || dropped.at(s))
				continue;
			double slack = maximum;
			for (size_t o = 0; o < n; ++o)
			{
				::MapGeneration::generationCheckpoint();
				if (o != s && !dropped.at(o))
					slack = std::min(slack, distance.at(s).at(o) - radius.at(o) - room.at(s).at(o));
			}
			radius.at(s) = std::max(radius.at(s), std::min(slack, maximum));
		}
	};
	std::vector<unsigned char> dropped(n, 0);
	for (int round = 0; round < 3; ++round)
	{
		::MapGeneration::generationCheckpoint();
		grow(dropped);
	}
	for (size_t s = 0; s < n; ++s)
	{
		::MapGeneration::generationCheckpoint();
		if (fixed.at(s) < 0 && radius.at(s) < minimum)
			dropped.at(s) = 1;
	}
	grow(dropped);
	for (size_t s = 0; s < n; ++s)
	{
		::MapGeneration::generationCheckpoint();
		if (dropped.at(s))
			radius.at(s) = -1;
	}
	return radius;
}

std::vector<int> nearestSiteLabels(const Torus &t, const std::vector<Site> &sites, int spacing,
								   GenerationContext &context, const std::string &stream,
								   int warpPeriodPercent, int warpPercent)
{
	PERF_SCOPE_TIME(Sites);
	const int period = std::max(1, spacing * warpPeriodPercent / 100);
	const std::vector<int> warpX = fractalNoise(t.w, t.h, period, 3, context.stream(stream));
	const std::vector<int> warpY = fractalNoise(t.w, t.h, period, 3, context.stream(stream));
	return nearestSiteLabels(t, sites, spacing, warpX, warpY, warpPercent);
}

std::vector<int> nearestSiteLabels(const Torus &t, const std::vector<Site> &sites, int spacing,
								   const std::vector<int> &warpX, const std::vector<int> &warpY,
								   int warpPercent)
{
	PERF_SCOPE_TIME(Sites);
	const std::int64_t amplitude = std::int64_t(spacing) * 16 * warpPercent / 100;
	const int W = t.w * 16, H = t.h * 16;
	const int gx = std::max(1, t.w / spacing), gy = std::max(1, t.h / spacing);
	generationAllocation(std::uint64_t(gx) * gy * sizeof(std::vector<int>));
	std::vector<std::vector<int>> buckets(size_t(gx) * gy), columns(gx), rows(gy);
	for (size_t s = 0; s < sites.size(); ++s)
	{
		::MapGeneration::generationCheckpoint();
		buckets.at(size_t(sites.at(s).y * gy / t.h) * gx + sites.at(s).x * gx / t.w)
			.push_back(int(s));
	}
	for (int c = 0; c < gx; ++c)
	{
		::MapGeneration::generationCheckpoint();
		columns.at(c) = bucketWindow(c, gx, 2);
	}
	for (int r = 0; r < gy; ++r)
	{
		::MapGeneration::generationCheckpoint();
		rows.at(r) = bucketWindow(r, gy, 2);
	}
	std::vector<int> label(size_t(t.w) * t.h, 0);
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const size_t i = size_t(y) * t.w + x;
			int px = x * 16 + 8 + int((warpX.at(i) - 32768) * amplitude / 32768);
			int py = y * 16 + 8 + int((warpY.at(i) - 32768) * amplitude / 32768);
			px = dimensionRemainder(((dimensionRemainder(px, W)) + W), W);
			py = dimensionRemainder(((dimensionRemainder(py, H)) + H), H);
			const int bx = int(std::int64_t(px) * gx / W), by = int(std::int64_t(py) * gy / H);
			std::int64_t best = INT64_MAX;
			int nearest = 0;
			for (int row : rows.at(by))
			{
				::MapGeneration::generationCheckpoint();
				for (int column : columns.at(bx))
				{
					::MapGeneration::generationCheckpoint();
					for (int s : buckets.at(size_t(row) * gx + column))
					{
						::MapGeneration::generationCheckpoint();
						int dx = std::abs(px - (sites.at(s).x * 16 + 8)),
							dy = std::abs(py - (sites.at(s).y * 16 + 8));
						dx = std::min(dx, W - dx);
						dy = std::min(dy, H - dy);
						const std::int64_t d = std::int64_t(dx) * dx + std::int64_t(dy) * dy;
						if (d < best || (d == best && s < nearest))
						{
							best = d;
							nearest = s;
						}
					}
				}
			}
			label.at(i) = nearest;
		}
	}
	return label;
}

std::vector<Site> relaxPoints(const Torus &t, std::vector<Site> sites, int iterations)
{
	PERF_SCOPE_TIME(Relax);
	const int n = int(sites.size());
	for (int round = 0; round < iterations && n > 0; ++round)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<std::int64_t> sumX(n, 0), sumY(n, 0), count(n, 0);
		for (int y = 0; y < t.h; ++y)
		{
			::MapGeneration::generationCheckpoint();
			for (int x = 0; x < t.w; ++x)
			{
				::MapGeneration::generationCheckpoint();
				int nearest = 0, best = INT32_MAX;
				for (int s = 0; s < n; ++s)
				{
					::MapGeneration::generationCheckpoint();
					const int d = t.dist2(x, y, sites.at(s).x, sites.at(s).y);
					if (d < best)
					{
						best = d;
						nearest = s;
					}
				}
				// Offsets from the site, so a cell across the wrap averages the short way round.
				sumX.at(nearest) += t.offsetX(sites.at(nearest).x, x);
				sumY.at(nearest) += t.offsetY(sites.at(nearest).y, y);
				++count.at(nearest);
			}
		}
		for (int s = 0; s < n; ++s)
		{
			::MapGeneration::generationCheckpoint();
			if (count.at(s))
			{
				// Round half away from zero, in integers.
				const auto mean = [&](std::int64_t sum)
				{
					return int(sum >= 0 ? (2 * sum + count.at(s)) / (2 * count.at(s))
										: -((-2 * sum + count.at(s)) / (2 * count.at(s))));
				};
				sites.at(s).x = t.x(sites.at(s).x + mean(sumX.at(s)));
				sites.at(s).y = t.y(sites.at(s).y + mean(sumY.at(s)));
			}
		}
	}
	return sites;
}

std::vector<std::vector<int>> siteNeighbours(const Torus &t, const std::vector<int> &labels,
											 int sites)
{
	std::vector<std::vector<int>> graph(size_t(std::max(0, sites)));
	for (int y = 0; y < t.h; ++y)
	{
		::MapGeneration::generationCheckpoint();
		for (int x = 0; x < t.w; ++x)
		{
			::MapGeneration::generationCheckpoint();
			const int a = labels.at(size_t(y) * t.w + x);
			for (int b : {labels.at(t.at(x + 1, y)), labels.at(t.at(x, y + 1))})
			{
				::MapGeneration::generationCheckpoint();
				if (a != b && a >= 0 && b >= 0 && a < sites && b < sites)
				{
					graph.at(a).push_back(b);
					graph.at(b).push_back(a);
				}
			}
		}
	}
	for (auto &list : graph)
	{
		::MapGeneration::generationCheckpoint();
		std::sort(list.begin(), list.end());
		list.erase(std::unique(list.begin(), list.end()), list.end());
	}
	return graph;
}
} // namespace MapGeneration

namespace MapGeneration
{
std::vector<int> farthestSites(const Torus &t, const std::vector<unsigned char> &candidates,
							   const std::vector<unsigned char> &walkable, int count,
							   GenerationContext &context, const std::string &stream, int trials,
							   const std::function<bool(int)> *prefer, int *rejected)
{
	PERF_SCOPE_TIME(Sites);
	std::vector<int> pool;
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (candidates.at(i))
			pool.push_back(i);
	}
	std::vector<int> best;
	int bestSpacing = -1, bestRejected = 0;
	if (pool.empty() || count <= 0)
		return best;
	// Every trial draws its first site whether or not a trial is kept, so the draws a request
	// consumes depend on `trials` alone and a validator replaying the design lands on the same
	// sites.
	for (int trial = 0; trial < std::max(1, trials); ++trial)
	{
		::MapGeneration::generationCheckpoint();
		std::vector<int> chosen{pool.at(context.bounded(stream, std::uint32_t(pool.size())))};
		int spacing = INT_MAX, fellBack = 0;
		while (int(chosen.size()) < count)
		{
			::MapGeneration::generationCheckpoint();
			const std::vector<int> steps = stepsFrom(t, tileMask(t, chosen), walkable);
			// Candidates farthest first, the lower index first among equals.
			std::vector<std::pair<int, int>> order;
			for (int i : pool)
			{
				::MapGeneration::generationCheckpoint();
				if (steps.at(i) > 0)
					order.push_back({-steps.at(i), i});
			}
			if (order.empty())
				break;
			std::sort(order.begin(), order.end());
			int next = -1;
			if (prefer)
				for (const auto &[negative, i] : order)
				{
					::MapGeneration::generationCheckpoint();
					if ((*prefer)(i))
					{
						next = i;
						break;
					}
				}
			if (next < 0)
			{
				next = order.front().second;
				fellBack += prefer != nullptr;
			}
			spacing = std::min(spacing, steps.at(next));
			chosen.push_back(next);
		}
		// A trial that placed more sites beats one that placed fewer; among full trials the one
		// whose closest pair is farthest apart wins, the earlier trial on a tie.
		if (chosen.size() > best.size() || (chosen.size() == best.size() && spacing > bestSpacing))
		{
			best = chosen;
			bestSpacing = spacing;
			bestRejected = fellBack;
		}
	}
	if (rejected)
		*rejected = bestRejected;
	return best;
}

std::vector<int> recentreSites(const Torus &t, const std::vector<int> &labels,
							   const std::vector<unsigned char> &candidates,
							   const std::vector<int> &sites)
{
	std::vector<int> moved = sites;
	const int n = int(sites.size());
	std::vector<std::int64_t> sumX(n, 0), sumY(n, 0), tiles(n, 0);
	for (int i = 0; i < t.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int k = labels.at(i);
		if (k < 0 || k >= n)
			continue;
		const int sx = t.remainderX(sites.at(k)), sy = sites.at(k) / t.w;
		sumX.at(k) += t.offsetX(sx, t.remainderX(i));
		sumY.at(k) += t.offsetY(sy, i / t.w);
		++tiles.at(k);
	}
	for (int k = 0; k < n; ++k)
	{
		::MapGeneration::generationCheckpoint();
		if (tiles.at(k) == 0)
			continue;
		// The middle in sixteenths of a tile, rounded to nearest, so the arithmetic is integer.
		const std::int64_t mx =
			(t.remainderX(sites.at(k))) * 16 + (sumX.at(k) * 16 + tiles.at(k) / 2) / tiles.at(k);
		const std::int64_t my =
			(sites.at(k) / t.w) * 16 + (sumY.at(k) * 16 + tiles.at(k) / 2) / tiles.at(k);
		std::int64_t nearest = -1;
		for (int i = 0; i < t.size(); ++i)
		{
			::MapGeneration::generationCheckpoint();
			if (!candidates.at(i) || labels.at(i) != k)
				continue;
			std::int64_t dx = std::abs((t.remainderX(i)) * 16 - mx), dy = std::abs((i / t.w) * 16 - my);
			dx = std::min(dx, t.w * 16 - dx);
			dy = std::min(dy, t.h * 16 - dy);
			const std::int64_t d = dx * dx + dy * dy;
			if (nearest < 0 || d < nearest)
			{
				nearest = d;
				moved.at(k) = i;
			}
		}
	}
	return moved;
}
int closestWalk(const Torus &t, const std::vector<int> &sites,
				const std::vector<unsigned char> &walkable)
{
	if (sites.size() < 2)
		return INT_MAX;
	// The first site's flood is whole: any site it doesn't reach makes the answer 0. Otherwise every
	// site shares its piece of ground (the walk is symmetric), so each later flood need only reach as
	// far as the closest pair so far; a site it doesn't reach within that is no closer.
	int spacing = INT_MAX;
	for (size_t a = 0; a + 1 < sites.size(); ++a)
	{
		::MapGeneration::generationCheckpoint();
		const Flood flood =
			floodFrom(t, tileMask(t, {sites.at(a)}), walkable, a == 0 ? INT_MAX : spacing);
		for (size_t b = a + 1; b < sites.size(); ++b)
		{
			::MapGeneration::generationCheckpoint();
			const int steps = flood.steps.at(size_t(sites.at(b)));
			if (a == 0 && steps < 0)
				return 0;
			if (steps >= 0)
				spacing = std::min(spacing, steps);
		}
	}
	return spacing;
}
} // namespace MapGeneration
