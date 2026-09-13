// SPDX-License-Identifier: GPL-3.0-or-later
#include "Morphology.h"
#include <algorithm>
#include <deque>
#include <queue>
namespace MapGeneration
{
namespace
{
// Whether any of the 2r + 1 entries centred on each position of a cyclic line is set, by a running
// count, so the cost doesn't grow with the radius.
void slideAny(const std::vector<unsigned char> &line, int radius, std::vector<unsigned char> &out)
{
	const int n = int(line.size());
	out.assign(line.size(), 0);
	if (2 * radius + 1 >= n)
	{
		const bool any = std::any_of(line.begin(), line.end(), [](unsigned char v) { return v; });
		std::fill(out.begin(), out.end(), any);
		return;
	}
	int count = 0;
	for (int d = -radius; d <= radius; ++d)
		count += line[((d % n) + n) % n] != 0;
	for (int i = 0; i < n; ++i)
	{
		out[i] = count > 0;
		count -= line[((i - radius) % n + n) % n] != 0;
		count += line[(i + radius + 1) % n] != 0;
	}
}

// One-dimensional squared distance transform of a cyclic line (Felzenszwalb and Huttenlocher's lower
// envelope), where f holds 0 on sources and a huge value elsewhere. The line is laid out three times
// so every position sees its nearest source the short way round.
void distanceLine(const std::vector<std::int64_t> &f, std::vector<std::int64_t> &out)
{
	constexpr std::int64_t kHuge = std::int64_t(1) << 50;
	const int n = int(f.size()), m = 3 * n;
	std::vector<int> v(m);
	std::vector<double> z(m + 1);
	const auto value = [&](int q) { return f[q % n]; };
	int k = -1;
	for (int q = 0; q < m; ++q)
	{
		if (value(q) >= kHuge)
			continue;
		if (k < 0)
		{
			k = 0;
			v[0] = q;
			z[0] = -1e30;
			z[1] = 1e30;
			continue;
		}
		double s;
		for (;;)
		{
			const int p = v[k];
			s = double((value(q) + std::int64_t(q) * q) - (value(p) + std::int64_t(p) * p)) /
				(2.0 * (q - p));
			if (s > z[k])
				break;
			--k; // z[0] is minus infinity, so this stops at the first parabola
		}
		++k;
		v[k] = q;
		z[k] = s;
		z[k + 1] = 1e30;
	}
	out.assign(n, kHuge);
	if (k < 0)
		return;
	int j = 0;
	for (int q = n; q < 2 * n; ++q)
	{
		while (z[j + 1] < q)
			++j;
		const std::int64_t d = q - v[j];
		out[q - n] = d * d + value(v[j]);
	}
}
// The minimum over the 2r + 1 entries centred on each position of a cyclic line: a monotonic deque
// of candidates, each entry pushed and popped once.
void slideMin(const std::vector<int> &line, int radius, std::vector<int> &out)
{
	const int n = int(line.size());
	out.assign(line.size(), 0);
	if (2 * radius + 1 >= n)
	{
		std::fill(out.begin(), out.end(), *std::min_element(line.begin(), line.end()));
		return;
	}
	std::deque<int> candidates; // positions (unwrapped), values nondecreasing front to back
	const auto push = [&](int position)
	{
		const int v = line[((position % n) + n) % n];
		while (!candidates.empty() && line[((candidates.back() % n) + n) % n] >= v)
			candidates.pop_back();
		candidates.push_back(position);
	};
	for (int d = -radius; d <= radius; ++d)
		push(d);
	for (int i = 0; i < n; ++i)
	{
		out[i] = line[((candidates.front() % n) + n) % n];
		if (candidates.front() == i - radius)
			candidates.pop_front();
		push(i + radius + 1);
	}
}
} // namespace

std::vector<unsigned char> dilate(const Torus &t, const std::vector<unsigned char> &mask,
								  int radius)
{
	if (radius <= 0)
		return mask;
	std::vector<unsigned char> rows(mask.size()), result(mask.size());
	std::vector<unsigned char> line, out;
	line.resize(t.w);
	for (int y = 0; y < t.h; ++y)
	{
		for (int x = 0; x < t.w; ++x)
			line[x] = mask[size_t(y) * t.w + x];
		slideAny(line, radius, out);
		for (int x = 0; x < t.w; ++x)
			rows[size_t(y) * t.w + x] = out[x];
	}
	line.resize(t.h);
	for (int x = 0; x < t.w; ++x)
	{
		for (int y = 0; y < t.h; ++y)
			line[y] = rows[size_t(y) * t.w + x];
		slideAny(line, radius, out);
		for (int y = 0; y < t.h; ++y)
			result[size_t(y) * t.w + x] = out[y];
	}
	return result;
}

std::vector<unsigned char> erode(const Torus &t, const std::vector<unsigned char> &mask, int radius)
{
	std::vector<unsigned char> inverse(mask.size());
	for (size_t i = 0; i < mask.size(); ++i)
		inverse[i] = !mask[i];
	std::vector<unsigned char> grown = dilate(t, inverse, radius);
	for (auto &v : grown)
		v = !v;
	return grown;
}

std::vector<unsigned char> openMask(const Torus &t, const std::vector<unsigned char> &mask,
									int radius)
{
	return dilate(t, erode(t, mask, radius), radius);
}

std::vector<unsigned char> closeMask(const Torus &t, const std::vector<unsigned char> &mask,
									 int radius)
{
	return erode(t, dilate(t, mask, radius), radius);
}

std::vector<std::int64_t> distanceSquaredTo(const Torus &t, const std::vector<unsigned char> &mask)
{
	constexpr std::int64_t kHuge = std::int64_t(1) << 50;
	const size_t n = mask.size();
	if (std::none_of(mask.begin(), mask.end(), [](unsigned char v) { return v; }))
		return std::vector<std::int64_t>(n, -1);
	std::vector<std::int64_t> field(n), line, out;
	for (size_t i = 0; i < n; ++i)
		field[i] = mask[i] ? 0 : kHuge;
	line.resize(t.w);
	for (int y = 0; y < t.h; ++y)
	{
		for (int x = 0; x < t.w; ++x)
			line[x] = field[size_t(y) * t.w + x];
		distanceLine(line, out);
		for (int x = 0; x < t.w; ++x)
			field[size_t(y) * t.w + x] = out[x];
	}
	line.resize(t.h);
	for (int x = 0; x < t.w; ++x)
	{
		for (int y = 0; y < t.h; ++y)
			line[y] = field[size_t(y) * t.w + x];
		distanceLine(line, out);
		for (int y = 0; y < t.h; ++y)
			field[size_t(y) * t.w + x] = out[y];
	}
	return field;
}

std::vector<unsigned char> dilateRound(const Torus &t, const std::vector<unsigned char> &mask,
									   double radius)
{
	const std::vector<std::int64_t> d2 = distanceSquaredTo(t, mask);
	std::vector<unsigned char> result(mask.size(), 0);
	for (size_t i = 0; i < mask.size(); ++i)
		result[i] = d2[i] >= 0 && double(d2[i]) <= radius * radius;
	return result;
}

std::vector<int> clearance(const Torus &t, const std::vector<unsigned char> &mask)
{
	std::vector<unsigned char> outside(mask.size());
	for (size_t i = 0; i < mask.size(); ++i)
		outside[i] = !mask[i];
	std::vector<int> steps = stepsFrom(t, outside);
	const int far = std::max(t.w, t.h) / 2;
	for (size_t i = 0; i < mask.size(); ++i)
		steps[i] = !mask[i] ? 0 : steps[i] < 0 ? far : steps[i];
	return steps;
}

std::vector<unsigned char> dropSmallRegions(const Torus &t, const std::vector<unsigned char> &mask,
											int minimumTiles, GridNeighbors neighbours)
{
	const std::vector<int> region = connectedRegions(mask, t.w, t.h, true, neighbours);
	int regions = 0;
	for (int r : region)
		regions = std::max(regions, r + 1);
	std::vector<int> size(size_t(regions), 0);
	for (int r : region)
		if (r >= 0)
			++size[size_t(r)];
	std::vector<unsigned char> kept(mask.size(), 0);
	for (size_t i = 0; i < mask.size(); ++i)
		kept[i] = region[i] >= 0 && size[size_t(region[i])] >= minimumTiles;
	return kept;
}

int widestWalkClearance(const Torus &t, const std::vector<unsigned char> &mask,
						const std::vector<int> &sources, const std::vector<unsigned char> &goal)
{
	const std::vector<int> room = clearance(t, mask);
	// Best bottleneck so far per tile; a max-heap on it (widest-path Dijkstra).
	std::vector<int> best(mask.size(), 0);
	std::priority_queue<std::pair<int, int>> heap;
	for (int i : sources)
		if (mask[i] && room[i] > best[i])
		{
			best[i] = room[i];
			heap.push({best[i], i});
		}
	while (!heap.empty())
	{
		const auto [width, i] = heap.top();
		heap.pop();
		if (width < best[i])
			continue;
		if (goal[i])
			return width;
		const int x = i % t.w, y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int m = t.at(x + dx, y + dy);
				const int through = std::min(width, room[m]);
				if (mask[m] && through > best[m])
				{
					best[m] = through;
					heap.push({through, m});
				}
			}
	}
	return 0;
}

int narrowestPassage(const Torus &t, const std::vector<unsigned char> &mask,
					 const std::vector<int> &sources, const std::vector<unsigned char> &goal)
{
	const int c = widestWalkClearance(t, mask, sources, goal);
	return c > 0 ? 2 * c - 1 : 0;
}

std::vector<unsigned char> slivers(const Torus &t, const std::vector<unsigned char> &mask,
								   int minimumClearance)
{
	const std::vector<unsigned char> kept = openMask(t, mask, std::max(0, minimumClearance - 1));
	std::vector<unsigned char> thin(mask.size(), 0);
	for (size_t i = 0; i < mask.size(); ++i)
		thin[i] = mask[i] && !kept[i];
	return thin;
}

std::vector<int> windowMinimum(const Torus &t, const std::vector<int> &field, int radius)
{
	std::vector<int> rows(field.size()), result(field.size()), line, out;
	line.resize(t.w);
	for (int y = 0; y < t.h; ++y)
	{
		for (int x = 0; x < t.w; ++x)
			line[x] = field[size_t(y) * t.w + x];
		slideMin(line, radius, out);
		for (int x = 0; x < t.w; ++x)
			rows[size_t(y) * t.w + x] = out[x];
	}
	line.resize(t.h);
	for (int x = 0; x < t.w; ++x)
	{
		for (int y = 0; y < t.h; ++y)
			line[y] = rows[size_t(y) * t.w + x];
		slideMin(line, radius, out);
		for (int y = 0; y < t.h; ++y)
			result[size_t(y) * t.w + x] = out[y];
	}
	return result;
}
} // namespace MapGeneration
