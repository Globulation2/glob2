// SPDX-License-Identifier: GPL-3.0-or-later
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
		if (site.tile < 0 || site.tile >= t.size() || !std::isfinite(site.weight) ||
			site.weight <= 0)
			return {};
		maximumWeight = std::max(maximumWeight, site.weight);
	}
	std::vector<int> picked{sites[first].tile};
	while (int(picked.size()) < count)
	{
		double best = -1;
		int chosen = -1;
		for (const auto &site : sites)
		{
			int distance = INT_MAX;
			for (int p : picked)
				distance =
					std::min(distance, t.dist2(site.tile % t.w, site.tile / t.w, p % t.w, p / t.w));
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
			result.push_back(i);
	}
	else
	{
		for (int d = -radius; d <= radius; ++d)
			result.push_back(((c + d) % count + count) % count);
	}
	return result;
}
} // namespace

NearestSites nearestTwoSites(const Torus &t, const std::vector<Site> &sites, int x, int y)
{
	NearestSites result;
	for (int k = 0; k < int(sites.size()); ++k)
	{
		const int d = t.dist2(sites[k].x, sites[k].y, x, y);
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
		columns[c] = bucketWindow(c, gx, 1);
	for (int r = 0; r < gy; ++r)
		rows[r] = bucketWindow(r, gy, 1);
	std::vector<std::vector<int>> buckets(size_t(gx) * gy);
	std::vector<Site> sites;
	for (std::int64_t attempt = 0; attempt < expected * dartsPerSite; ++attempt)
	{
		const int x = int(context.bounded(stream, t.w));
		const int y = int(context.bounded(stream, t.h));
		const int bx = x * gx / t.w, by = y * gy / t.h;
		bool clear = true;
		for (int row : rows[by])
		{
			for (int column : columns[bx])
			{
				for (int s : buckets[size_t(row) * gx + column])
					if (t.dist2(x, y, sites[s].x, sites[s].y) < minimum * minimum)
					{
						clear = false;
						break;
					}
				if (!clear)
					break;
			}
			if (!clear)
				break;
		}
		if (!clear)
			continue;
		buckets[size_t(by) * gx + bx].push_back(int(sites.size()));
		sites.push_back({x, y});
	}
	return sites;
}

namespace
{
// The eight headings: east, then round by about 22 degrees to north-east of west. Small whole
// numbers, so every landform along one lands the same way on every platform.
constexpr int kHeadingSteps[kGrainHeadings][2] = {{1, 0},  {2, 1},  {1, 1},  {1, 2},
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
			const std::int64_t d = t.dist2(sites[s].x, sites[s].y, sites[from].x, sites[from].y);
			nearest[s] = nearest[s] < 0 ? d : std::min(nearest[s], d);
		}
	};
	std::vector<unsigned char> taken(n, 0);
	for (int seed : seeds)
	{
		measureFrom(seed);
		taken[seed] = 1; // a seed is never chosen, whatever `eligible` says of it
	}
	std::vector<int> chosen;
	for (int k = 0; k < count; ++k)
	{
		int pick = -1;
		for (size_t s = 0; s < n; ++s)
			if (eligible[s] && !taken[s] && (pick < 0 || nearest[s] > nearest[pick]))
				pick = int(s);
		if (pick < 0)
			break;
		taken[pick] = 1;
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
		columns[c] = bucketWindow(c, gx, window);
	for (int r = 0; r < gy; ++r)
		rows[r] = bucketWindow(r, gy, window);
	std::vector<std::vector<int>> buckets(size_t(gx) * gy);
	std::vector<Site> sites;
	const std::int64_t clear = grain.metric2(minimum), clearFixed = grain.metric2(fixedMinimum);
	const auto place = [&](int x, int y)
	{
		buckets[size_t(y * gy / t.h) * gx + x * gx / t.w].push_back(int(sites.size()));
		sites.push_back({x, y});
	};
	for (const Site &site : fixed)
		place(t.x(site.x), t.y(site.y));
	const size_t fixedCount = sites.size();
	for (std::int64_t attempt = 0; attempt < expected * dartsPerSite; ++attempt)
	{
		const int x = int(context.bounded(stream, t.w));
		const int y = int(context.bounded(stream, t.h));
		const int bx = x * gx / t.w, by = y * gy / t.h;
		bool free = true;
		for (int row : rows[by])
		{
			for (int column : columns[bx])
			{
				for (int s : buckets[size_t(row) * gx + column])
				{
					const std::int64_t d = grain.distance2(t, sites[s].x, sites[s].y, x, y);
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
		for (size_t b = a + 1; b < sites.size(); ++b)
		{
			const double d = grain.distance(t, sites[a].x, sites[a].y, sites[b].x, sites[b].y);
			nearest[a] = std::min(nearest[a], d);
			nearest[b] = std::min(nearest[b], d);
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
	const double c = std::cos(grain.heading()), sn = std::sin(grain.heading());
	const double extentX = reach * std::hypot(c * stretch, sn),
				 extentY = reach * std::hypot(sn * stretch, c);
	const int gx = std::max(1, t.w / spacing), gy = std::max(1, t.h / spacing);
	const int windowX = int(std::ceil(extentX * gx / t.w)) + 1,
			  windowY = int(std::ceil(extentY * gy / t.h)) + 1;
	std::vector<std::vector<int>> buckets(size_t(gx) * gy), columns(gx), rows(gy);
	for (size_t s = 0; s < sites.size(); ++s)
		buckets[size_t(sites[s].y * gy / t.h) * gx + sites[s].x * gx / t.w].push_back(int(s));
	for (int col = 0; col < gx; ++col)
		columns[col] = bucketWindow(col, gx, windowX);
	for (int r = 0; r < gy; ++r)
		rows[r] = bucketWindow(r, gy, windowY);
	const std::int64_t within = grain.metric2(reach);
	const int halfW = t.w / 2, halfH = t.h / 2;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			std::int64_t best = INT64_MAX;
			int nearest = -1;
			// The inner loop of a whole-map labelling, run a few dozen times per tile: the wrap
			// is a comparison rather than Torus::offsetX's two remainders, and the far images
			// are tried only when Grain::distance2's bound says one could be nearer.
			const auto consider = [&](int s)
			{
				int dx = x - sites[s].x, dy = y - sites[s].y;
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
												grain.stretchPercent * grain.stretchPercent / 10000 +
											1;
				const int otherX = dx > 0 ? dx - t.w : dx + t.w, otherY = dy > 0 ? dy - t.h : dy + t.h;
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
			for (int row : rows[y * gy / t.h])
				for (int column : columns[x * gx / t.w])
					for (int s : buckets[size_t(row) * gx + column])
						consider(s);
			// Nothing within reach in the window: nothing within reach anywhere, so the nearest
			// site, wherever it is, needs the whole list.
			if (nearest < 0 || best > within)
				for (size_t s = 0; s < sites.size(); ++s)
					consider(int(s));
			label[size_t(y) * t.w + x] = nearest;
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
		const std::vector<int> label = nearestSiteLabels(t, sites, spacing, grain, reach);
		std::vector<std::int64_t> sumX(n, 0), sumY(n, 0), count(n, 0);
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				const int s = label[size_t(y) * t.w + x];
				// Offsets from the site, so a cell across the wrap averages the short way round.
				sumX[s] += t.offsetX(sites[s].x, x);
				sumY[s] += t.offsetY(sites[s].y, y);
				++count[s];
			}
		for (int s = fixedCount; s < n; ++s)
			if (count[s])
			{
				// Round half away from zero, in integers, as relaxPoints does.
				const auto mean = [&](std::int64_t sum)
				{
					return int(sum >= 0 ? (2 * sum + count[s]) / (2 * count[s])
										: -((-2 * sum + count[s]) / (2 * count[s])));
				};
				sites[s].x = t.x(sites[s].x + mean(sumX[s]));
				sites[s].y = t.y(sites[s].y + mean(sumY[s]));
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
	std::vector<std::vector<double>> distance(n, std::vector<double>(n, 0)),
		room(n, std::vector<double>(n, 0));
	for (size_t a = 0; a < n; ++a)
		for (size_t b = a + 1; b < n; ++b)
		{
			// The pair's map distance is measured on the same image of the pair as its distance
			// under the grain, which may not be the nearest image in map tiles.
			const int dx = t.offsetX(sites[a].x, sites[b].x), dy = t.offsetY(sites[a].y, sites[b].y);
			const int otherX = dx > 0 ? dx - t.w : dx + t.w, otherY = dy > 0 ? dy - t.h : dy + t.h;
			double under = 0, across = 0;
			for (const int ox : {dx, otherX})
				for (const int oy : {dy, otherY})
					if (const double d = grain.distance(ox, oy); across == 0 || d < under)
					{
						under = d;
						across = std::hypot(ox, oy);
					}
			distance[a][b] = distance[b][a] = under;
			room[a][b] = room[b][a] = across > 0 ? gap * under / across : gap;
		}
	// Every free site starts at half its nearest neighbour's distance less that pair's gap: two
	// neighbours each taking that keep the gap between them whatever else is round them.
	for (size_t s = 0; s < n; ++s)
	{
		if (fixed[s] >= 0)
		{
			radius[s] = fixed[s];
			continue;
		}
		double nearest = std::min(t.w, t.h);
		for (size_t o = 0; o < n; ++o)
			if (o != s)
				nearest = std::min(nearest, (distance[s][o] - room[s][o]) / 2);
		radius[s] = std::clamp(nearest, 0.0, maximum);
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
			if (fixed[s] >= 0 || dropped[s])
				continue;
			double slack = maximum;
			for (size_t o = 0; o < n; ++o)
				if (o != s && !dropped[o])
					slack = std::min(slack, distance[s][o] - radius[o] - room[s][o]);
			radius[s] = std::max(radius[s], std::min(slack, maximum));
		}
	};
	std::vector<unsigned char> dropped(n, 0);
	for (int round = 0; round < 3; ++round)
		grow(dropped);
	for (size_t s = 0; s < n; ++s)
		if (fixed[s] < 0 && radius[s] < minimum)
			dropped[s] = 1;
	grow(dropped);
	for (size_t s = 0; s < n; ++s)
		if (dropped[s])
			radius[s] = -1;
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
	std::vector<std::vector<int>> buckets(size_t(gx) * gy), columns(gx), rows(gy);
	for (size_t s = 0; s < sites.size(); ++s)
		buckets[size_t(sites[s].y * gy / t.h) * gx + sites[s].x * gx / t.w].push_back(int(s));
	for (int c = 0; c < gx; ++c)
		columns[c] = bucketWindow(c, gx, 2);
	for (int r = 0; r < gy; ++r)
		rows[r] = bucketWindow(r, gy, 2);
	std::vector<int> label(size_t(t.w) * t.h, 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const size_t i = size_t(y) * t.w + x;
			int px = x * 16 + 8 + int((warpX[i] - 32768) * amplitude / 32768);
			int py = y * 16 + 8 + int((warpY[i] - 32768) * amplitude / 32768);
			px = ((px % W) + W) % W;
			py = ((py % H) + H) % H;
			const int bx = int(std::int64_t(px) * gx / W), by = int(std::int64_t(py) * gy / H);
			std::int64_t best = INT64_MAX;
			int nearest = 0;
			for (int row : rows[by])
				for (int column : columns[bx])
					for (int s : buckets[size_t(row) * gx + column])
					{
						int dx = std::abs(px - (sites[s].x * 16 + 8)),
							dy = std::abs(py - (sites[s].y * 16 + 8));
						dx = std::min(dx, W - dx);
						dy = std::min(dy, H - dy);
						const std::int64_t d = std::int64_t(dx) * dx + std::int64_t(dy) * dy;
						if (d < best || (d == best && s < nearest))
						{
							best = d;
							nearest = s;
						}
					}
			label[i] = nearest;
		}
	return label;
}

std::vector<Site> relaxPoints(const Torus &t, std::vector<Site> sites, int iterations)
{
	PERF_SCOPE_TIME(Relax);
	const int n = int(sites.size());
	for (int round = 0; round < iterations && n > 0; ++round)
	{
		std::vector<std::int64_t> sumX(n, 0), sumY(n, 0), count(n, 0);
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
			{
				int nearest = 0, best = INT32_MAX;
				for (int s = 0; s < n; ++s)
				{
					const int d = t.dist2(x, y, sites[s].x, sites[s].y);
					if (d < best)
					{
						best = d;
						nearest = s;
					}
				}
				// Offsets from the site, so a cell across the wrap averages the short way round.
				sumX[nearest] += t.offsetX(sites[nearest].x, x);
				sumY[nearest] += t.offsetY(sites[nearest].y, y);
				++count[nearest];
			}
		for (int s = 0; s < n; ++s)
			if (count[s])
			{
				// Round half away from zero, in integers.
				const auto mean = [&](std::int64_t sum)
				{
					return int(sum >= 0 ? (2 * sum + count[s]) / (2 * count[s])
										: -((-2 * sum + count[s]) / (2 * count[s])));
				};
				sites[s].x = t.x(sites[s].x + mean(sumX[s]));
				sites[s].y = t.y(sites[s].y + mean(sumY[s]));
			}
	}
	return sites;
}

std::vector<std::vector<int>> siteNeighbours(const Torus &t, const std::vector<int> &labels,
											 int sites)
{
	std::vector<std::vector<int>> graph(size_t(std::max(0, sites)));
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int a = labels[size_t(y) * t.w + x];
			for (int b : {labels[t.at(x + 1, y)], labels[t.at(x, y + 1)]})
				if (a != b && a >= 0 && b >= 0 && a < sites && b < sites)
				{
					graph[a].push_back(b);
					graph[b].push_back(a);
				}
		}
	for (auto &list : graph)
	{
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
		if (candidates[i])
			pool.push_back(i);
	std::vector<int> best;
	int bestSpacing = -1, bestRejected = 0;
	if (pool.empty() || count <= 0)
		return best;
	// Every trial draws its first site whether or not a trial is kept, so the draws a request
	// consumes depend on `trials` alone and a validator replaying the design lands on the same
	// sites.
	for (int trial = 0; trial < std::max(1, trials); ++trial)
	{
		std::vector<int> chosen{pool[context.bounded(stream, std::uint32_t(pool.size()))]};
		int spacing = INT_MAX, fellBack = 0;
		while (int(chosen.size()) < count)
		{
			const std::vector<int> steps = stepsFrom(t, tileMask(t, chosen), walkable);
			// Candidates farthest first, the lower index first among equals.
			std::vector<std::pair<int, int>> order;
			for (int i : pool)
				if (steps[i] > 0)
					order.push_back({-steps[i], i});
			if (order.empty())
				break;
			std::sort(order.begin(), order.end());
			int next = -1;
			if (prefer)
				for (const auto &[negative, i] : order)
					if ((*prefer)(i))
					{
						next = i;
						break;
					}
			if (next < 0)
			{
				next = order.front().second;
				fellBack += prefer != nullptr;
			}
			spacing = std::min(spacing, steps[next]);
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
		const int k = labels[i];
		if (k < 0 || k >= n)
			continue;
		const int sx = sites[k] % t.w, sy = sites[k] / t.w;
		sumX[k] += t.offsetX(sx, i % t.w);
		sumY[k] += t.offsetY(sy, i / t.w);
		++tiles[k];
	}
	for (int k = 0; k < n; ++k)
	{
		if (tiles[k] == 0)
			continue;
		// The middle in sixteenths of a tile, rounded to nearest, so the arithmetic is integer.
		const std::int64_t mx = (sites[k] % t.w) * 16 + (sumX[k] * 16 + tiles[k] / 2) / tiles[k];
		const std::int64_t my = (sites[k] / t.w) * 16 + (sumY[k] * 16 + tiles[k] / 2) / tiles[k];
		std::int64_t nearest = -1;
		for (int i = 0; i < t.size(); ++i)
		{
			if (!candidates[i] || labels[i] != k)
				continue;
			std::int64_t dx = std::abs((i % t.w) * 16 - mx), dy = std::abs((i / t.w) * 16 - my);
			dx = std::min(dx, t.w * 16 - dx);
			dy = std::min(dy, t.h * 16 - dy);
			const std::int64_t d = dx * dx + dy * dy;
			if (nearest < 0 || d < nearest)
			{
				nearest = d;
				moved[k] = i;
			}
		}
	}
	return moved;
}
} // namespace MapGeneration
