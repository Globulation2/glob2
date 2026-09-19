// SPDX-License-Identifier: GPL-3.0-or-later
#include "Morphology.h"
#include <algorithm>
#include <deque>
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
	for (int d = -radius; d < 0; ++d)
		count += line[n + d] != 0;
	for (int d = 0; d <= radius; ++d)
		count += line[d] != 0;
	// 2 * radius + 1 < n was just established above, so i - radius and i + radius + 1 leave
	// [0, n) only in these two disjoint end ranges of the line; splitting the loop this way
	// replaces the pair of remainders every iteration used with plain indexing in between.
	int i = 0;
	for (; i < radius; ++i)
	{
		out[i] = count > 0;
		count -= line[i - radius + n] != 0;
		count += line[i + radius + 1] != 0;
	}
	for (; i < n - radius - 1; ++i)
	{
		out[i] = count > 0;
		count -= line[i - radius] != 0;
		count += line[i + radius + 1] != 0;
	}
	for (; i < n; ++i)
	{
		out[i] = count > 0;
		count -= line[i - radius] != 0;
		count += line[i + radius + 1 - n] != 0;
	}
}

// One-dimensional squared distance transform of a cyclic line (Felzenszwalb and Huttenlocher's lower
// envelope), where f holds 0 on sources and a huge value elsewhere. The line is laid out three times
// so every position sees its nearest source the short way round.
void distanceLine(const std::vector<std::int64_t> &f, std::vector<std::int64_t> &out)
{
	constexpr std::int64_t kHuge = std::int64_t(1) << 50;
	const int n = int(f.size()), m = 3 * n;
	// f laid out three times over turns every f[q % n] below into a plain index: q and the v[j]
	// values recorded from it both stay within [0, m), so triple[q] is exactly f[q % n], without
	// a division at every one of the O(m) lookups the two loops below make.
	std::vector<std::int64_t> triple(m);
	for (int rep = 0; rep < 3; ++rep)
		std::copy(f.begin(), f.end(), triple.begin() + rep * n);
	std::vector<int> v(m);
	std::vector<double> z(m + 1);
	const auto value = [&](int q) { return triple[q]; };
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
	const int farthest = std::max(t.w, t.h) / 2;
	for (size_t i = 0; i < mask.size(); ++i)
		steps[i] = !mask[i] ? 0 : steps[i] < 0 ? farthest : steps[i];
	return steps;
}

int roomiestTile(const Torus &t, const std::vector<unsigned char> &region,
				 const std::vector<int> &room, int nearX, int nearY)
{
	int best = -1;
	for (int i = 0; i < t.size(); ++i)
	{
		if (!region[i])
			continue;
		if (best < 0 || room[i] > room[best] ||
			(room[i] == room[best] && t.dist2(i % t.w, i / t.w, nearX, nearY) <
										  t.dist2(best % t.w, best / t.w, nearX, nearY)))
			best = i;
	}
	return best;
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
						const std::vector<int> &room, const std::vector<int> &sources,
						const std::vector<unsigned char> &goal)
{
	// Best bottleneck so far per tile, and a bucket queue on it rather than a comparison heap.
	//
	// This is widest-path Dijkstra, so it wants the largest tentative width next - but the widths are
	// clearances, which are small bounded integers (at most half the shorter side), not arbitrary
	// keys. A bucket per width and a walk downwards through them gives the same order for O(1) a push
	// and a pop, where a binary heap pays a logarithm and chases pointers over a growing array for
	// every one of them. Even Ground's shape pass scores thousands of arrangements and this was the
	// single hottest thing in it once the clearance field stopped being rebuilt per colony: two
	// fifths of the search's time was inside __pop_heap alone.
	//
	// Processing buckets from the widest down is what makes it correct: a tile popped from bucket w
	// when every wider bucket is empty can never be reached more widely later, because relaxation
	// only ever produces min(w, room), which is at most w. So the first pop of a tile is final, and a
	// tile whose entry is stale (a wider one was queued later) is skipped by the best[] check exactly
	// as it would be with a heap.
	std::vector<int> best(mask.size(), 0);
	int widest = 0;
	for (const int i : sources)
		if (mask[i] && room[i] > best[i])
			widest = std::max(widest, room[i]);
	if (widest <= 0)
		return 0;
	std::vector<std::vector<int>> bucket(size_t(widest) + 1);
	for (const int i : sources)
		if (mask[i] && room[i] > best[i])
		{
			best[i] = room[i];
			bucket[size_t(room[i])].push_back(i);
		}
	for (int width = widest; width > 0; --width)
	{
		// Indexed, not iterated: relaxing at this width can append to this very bucket, and a
		// reference into it would dangle the moment that push reallocated.
		for (size_t head = 0; head < bucket[size_t(width)].size(); ++head)
		{
			const int i = bucket[size_t(width)][head];
			if (best[i] != width)
				continue; // stale: this tile was queued again wider, and has been settled already
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
						bucket[size_t(through)].push_back(m);
					}
				}
		}
	}
	return 0;
}

int widestWalkClearance(const Torus &t, const std::vector<unsigned char> &mask,
						const std::vector<int> &sources, const std::vector<unsigned char> &goal)
{
	return widestWalkClearance(t, mask, clearance(t, mask), sources, goal);
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

namespace MapGeneration
{
std::vector<unsigned char> bridgeDiagonals(const Torus &t, const std::vector<unsigned char> &mask)
{
	std::vector<unsigned char> result = mask;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			if (!mask[t.at(x, y)])
				continue;
			// Only the two diagonals ahead in row order, so each contact is looked at once.
			for (int dx : {-1, 1})
			{
				const int diagonal = t.at(x + dx, y + 1), beside = t.at(x + dx, y),
						  below = t.at(x, y + 1);
				if (mask[diagonal] && !mask[beside] && !mask[below])
					result[std::min(beside, below)] = 1;
			}
		}
	return result;
}
} // namespace MapGeneration

namespace MapGeneration
{
std::vector<int> windowCount(const Torus &t, const std::vector<unsigned char> &mask, int radius)
{
	// Rows first: each tile's count over the 2 * radius + 1 tiles of its row, through the wrap.
	std::vector<int> rows(mask.size(), 0), result(mask.size(), 0);
	for (int y = 0; y < t.h; ++y)
	{
		int sum = 0;
		for (int dx = -radius; dx <= radius; ++dx)
			sum += mask[t.at(dx, y)];
		for (int x = 0; x < t.w; ++x)
		{
			rows[size_t(y) * t.w + x] = sum;
			sum += mask[t.at(x + radius + 1, y)] - mask[t.at(x - radius, y)];
		}
	}
	for (int x = 0; x < t.w; ++x)
	{
		int sum = 0;
		for (int dy = -radius; dy <= radius; ++dy)
			sum += rows[t.at(x, dy)];
		for (int y = 0; y < t.h; ++y)
		{
			result[size_t(y) * t.w + x] = sum;
			sum += rows[t.at(x, y + radius + 1)] - rows[t.at(x, y - radius)];
		}
	}
	return result;
}
} // namespace MapGeneration
