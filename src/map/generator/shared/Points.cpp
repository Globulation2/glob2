// SPDX-License-Identifier: GPL-3.0-or-later
#include "Points.h"
#include "GenerationContext.h"
#include "LatticeNoise.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace MapGeneration
{
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

std::vector<int> nearestSiteLabels(const Torus &t, const std::vector<Site> &sites, int spacing,
								   GenerationContext &context, const std::string &stream,
								   int warpPeriodPercent, int warpPercent)
{
	const int period = std::max(1, spacing * warpPeriodPercent / 100);
	const std::vector<int> warpX = fractalNoise(t.w, t.h, period, 3, context.stream(stream));
	const std::vector<int> warpY = fractalNoise(t.w, t.h, period, 3, context.stream(stream));
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
