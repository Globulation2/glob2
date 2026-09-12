// SPDX-License-Identifier: GPL-3.0-or-later
#include "WatershedGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Resources.h"
#include "Settlements.h"
#include "Unit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// A branching river system draining into a sea along one side of the map. Tributaries rise at
// springs in the uplands and join a trunk that splits into a delta at the coast. Every channel is
// wide enough to keep a core of open water, so on foot a river can only be crossed at a ford: a
// strip of sand laid across it. Wheat and wood only regrow near water, so farmland follows the
// rivers in long ribbons, while ground far from any river dries out to sand.
//
// The coast, rivers and fords are planned in a canonical frame with the sea along the bottom,
// then turned so the sea lies along one side of the map. Planning draws only from its
// own named streams and never reads the map, so validateWorld can plan the same layout again and
// check every ford and channel against the finished world.
//
// Terrain is stamped on the undermap with a symmetric version of Map::controlSand's shoreline
// rule, then controlSand itself is run and must change nothing: the shores are already what it
// would make of them, without its row-order raster pass silting narrow channels unevenly.
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr const char *kLayoutStream = "watershed-layout";
constexpr const char *kRiverStream = "watershed-rivers";
constexpr const char *kStartStream = "watershed-starts";

// Channel radius, in undermap vertices, before the shores are sanded. Sanding takes up to about
// 1.5 vertices off a diagonal channel, and a tile is water only when all four corners are, so this
// is the narrowest channel whose water tiles still form a 4-connected core in every direction: a
// line no unit can step across.
constexpr double kMinimumChannelRadius = 3.2;
// Distance between consecutive points of a river's centreline.
constexpr double kSpacing = 0.5;
// A ford is sand this far either side of its centre line along the river - three rows of pure
// sand tiles, five rows walkable - reaching this far past the channel's radius across it.
constexpr double kFordHalfWidth = 2.0;
constexpr double kFordReach = 2.5;
// A tributary joins a point downstream of its spring by at least this much per tile of sideways
// travel, so rivers run towards the sea rather than along it.
constexpr double kSlope = 0.3;
// Tiles of map per colony that validateRequest allows.
constexpr int kTilesPerColony = 1024;
// Distance along a river between fords, for each setting of the fords control.
constexpr int kFordSpacing[5] = {80, 60, 44, 34, 26};
// Colonies start between these many steps from open water: on the floodplain, not its shore.
constexpr int kColonyWaterNear = 5, kColonyWaterFar = 11;
constexpr double kColonySpacing = 14;

double sq(double v)
{
	return v * v;
}

struct Vec
{
	double x, y;
};
Vec operator+(Vec a, Vec b)
{
	return {a.x + b.x, a.y + b.y};
}
Vec operator-(Vec a, Vec b)
{
	return {a.x - b.x, a.y - b.y};
}
Vec operator*(Vec a, double s)
{
	return {a.x * s, a.y * s};
}
double dot(Vec a, Vec b)
{
	return a.x * b.x + a.y * b.y;
}
double length(Vec a)
{
	return std::sqrt(dot(a, a));
}
Vec normalized(Vec a)
{
	const double l = length(a);
	return l > 1e-9 ? a * (1.0 / l) : Vec{0, 1};
}
double wrapped(double value, double period)
{
	return value - period * std::floor(value / period);
}
double centred(double delta, double period)
{
	return delta - period * std::round(delta / period);
}
int wrapIndex(int value, int period)
{
	return ((value % period) + period) % period;
}

double unit(GenerationContext &context, const char *stream)
{
	return context.bounded(stream, 1u << 20) / double(1u << 20);
}
double between(GenerationContext &context, const char *stream, double lo, double hi)
{
	return lo + (hi - lo) * unit(context, stream);
}

double channelRadius(int riverWidth)
{
	return kMinimumChannelRadius + 0.4 * (riverWidth - 1);
}

// A coastline: a sum of sinusoids whose frequencies are whole cycles of the coast's length, so it
// joins itself seamlessly across the torus seam.
struct Ripple
{
	double base = 0, period = 1;
	std::array<double, 4> amplitude{}, phase{}, frequency{};
	// A smooth bulge, such as a delta building land out into the sea.
	double lobeCentre = 0, lobeHeight = 0, lobeWidth = 1;
	double at(double u) const
	{
		double value = base;
		for (size_t k = 0; k < amplitude.size(); ++k)
			value += amplitude[k] * std::sin(2 * kPi * frequency[k] * u / period + phase[k]);
		if (lobeHeight != 0)
			value += lobeHeight * std::exp(-sq(centred(u - lobeCentre, period) / lobeWidth));
		return value;
	}
};

Ripple makeRipple(GenerationContext &context, double base, double amplitude, int period)
{
	static const double harmonics[4] = {1, 3, 5, 9}, weights[4] = {0.35, 0.3, 0.2, 0.15};
	Ripple ripple;
	ripple.base = base;
	ripple.period = period;
	const int scale = std::max(1, period / 128);
	for (size_t k = 0; k < 4; ++k)
	{
		ripple.frequency[k] = harmonics[k] * scale;
		ripple.amplitude[k] = amplitude * weights[k] * between(context, kLayoutStream, 0.5, 1.0);
		ripple.phase[k] = between(context, kLayoutStream, 0, 2 * kPi);
	}
	return ripple;
}

// Smooth value noise on a lattice that tiles the map, so it wraps like the map does.
class PeriodicNoise
{
  public:
	PeriodicNoise(int width, int height, double cell, std::mt19937 &random)
		: width(width), height(height), columns(std::max(2, int(std::lround(width / cell)))),
		  rows(std::max(2, int(std::lround(height / cell)))), lattice(size_t(columns) * rows)
	{
		for (double &value : lattice)
			value = random() / 4294967296.0;
	}
	double at(double x, double y) const
	{
		const double fx = x * columns / width, fy = y * rows / height;
		const double x0 = std::floor(fx), y0 = std::floor(fy);
		double tx = fx - x0, ty = fy - y0;
		tx = tx * tx * (3 - 2 * tx);
		ty = ty * ty * (3 - 2 * ty);
		const int ix = wrapIndex(int(x0), columns), iy = wrapIndex(int(y0), rows);
		const int jx = (ix + 1) % columns, jy = (iy + 1) % rows;
		const double top = lattice[iy * columns + ix] * (1 - tx) + lattice[iy * columns + jx] * tx;
		const double bottom =
			lattice[jy * columns + ix] * (1 - tx) + lattice[jy * columns + jx] * tx;
		return top * (1 - ty) + bottom * ty;
	}

  private:
	int width, height, columns, rows;
	std::vector<double> lattice;
};

// The canonical frame: u runs along the coast, v across it with the sea at large v. The map is
// the frame turned so the sea lies along one of its sides: orientations 0 and 1 put it south or
// north, 2 and 3 east or west.
struct Frame
{
	int width = 0, height = 0; // the map
	int along = 0, across = 0; // the frame
	int orientation = 0;
	Vec toMap(Vec c) const
	{
		switch (orientation)
		{
		case 0:
			return {c.x, c.y};
		case 1:
			return {c.x, across - c.y};
		case 2:
			return {c.y, c.x};
		default:
			return {across - c.y, c.x};
		}
	}
	Vec toCanonical(Vec m) const
	{
		const double x = m.x, y = m.y;
		Vec c{x, y};
		switch (orientation)
		{
		case 1:
			c = {x, across - y};
			break;
		case 2:
			c = {y, x};
			break;
		case 3:
			c = {y, across - x};
			break;
		default:
			break;
		}
		return {wrapped(c.x, along), wrapped(c.y, across)};
	}
};

// One stretch of river, from its upstream end (a spring, or the delta's apex for a distributary)
// to where it ends: joining its parent at joinIndex, at the apex for the trunk, or in the sea.
struct Reach
{
	std::vector<Vec> points;
	std::vector<double> radius;
	// Distance across the frame to the nearer coast; small or negative where a reach meets the sea.
	std::vector<double> coastGap;
	int parent = -1, joinIndex = -1;
	bool distributary = false;
	double length() const { return (points.size() - 1) * kSpacing; }
};

struct Ford
{
	Vec center, along, across;
	double span;
};

struct Layout
{
	Frame frame;
	Ripple north, south;
	int trunk = 0;
	// Everything below is in map coordinates.
	std::vector<Reach> reaches;
	std::vector<std::vector<int>> joins;
	// Lines from every spring straight upstream to the sea: they split the land into banks.
	std::vector<std::vector<Vec>> divides;
	std::vector<Vec> mouths, junctions;
	std::vector<Ford> fords;
};

// River centreline points in map coordinates, bucketed so a ford can check what is near it.
class ChannelIndex
{
  public:
	explicit ChannelIndex(const Layout &layout)
		: layout(layout), w(layout.frame.width), h(layout.frame.height),
		  columns(std::max(1, w / 16)), rows(std::max(1, h / 16)), cells(size_t(columns) * rows)
	{
		for (int r = 0; r < int(layout.reaches.size()); ++r)
			for (int i = 0; i < int(layout.reaches[r].points.size()); ++i)
				cells[cellOf(layout.reaches[r].points[i])].push_back({r, i});
	}

	// Whether p lies at least `margin` outside every channel, ignoring reach r within `skip` tiles
	// of point i along it.
	bool clear(Vec p, double margin, int r, int i, double skip) const
	{
		const int cx = column(p), cy = row(p);
		int xs[3], ys[3], nx = 0, ny = 0;
		for (int d = -1; d <= 1; ++d)
		{
			const int x = wrapIndex(cx + d, columns), y = wrapIndex(cy + d, rows);
			if (std::find(xs, xs + nx, x) == xs + nx)
				xs[nx++] = x;
			if (std::find(ys, ys + ny, y) == ys + ny)
				ys[ny++] = y;
		}
		for (int a = 0; a < ny; ++a)
			for (int b = 0; b < nx; ++b)
				for (const auto &entry : cells[size_t(ys[a]) * columns + xs[b]])
				{
					if (entry.first == r && std::abs(entry.second - i) * kSpacing <= skip)
						continue;
					const Reach &reach = layout.reaches[entry.first];
					double radius = reach.radius[entry.second];
					if (entry.second == 0 && !reach.distributary)
						radius += 1.3; // the spring's pond
					const Vec q = reach.points[entry.second];
					if (sq(centred(p.x - q.x, w)) + sq(centred(p.y - q.y, h)) < sq(radius + margin))
						return false;
				}
		return true;
	}

  private:
	int column(Vec p) const { return std::min(columns - 1, int(wrapped(p.x, w) / 16)); }
	int row(Vec p) const { return std::min(rows - 1, int(wrapped(p.y, h) / 16)); }
	size_t cellOf(Vec p) const { return size_t(row(p)) * columns + column(p); }
	const Layout &layout;
	int w, h, columns, rows;
	std::vector<std::vector<std::pair<int, int>>> cells;
};

std::vector<Vec> resample(const std::vector<Vec> &line)
{
	std::vector<Vec> out{line.front()};
	double carried = 0;
	for (size_t i = 1; i < line.size(); ++i)
	{
		Vec a = line[i - 1];
		const Vec b = line[i];
		double segment = length(b - a);
		while (segment > 0 && carried + segment >= kSpacing)
		{
			a = a + (b - a) * ((kSpacing - carried) / segment);
			out.push_back(a);
			segment = length(b - a);
			carried = 0;
		}
		carried += segment;
	}
	if (out.size() > 1 && length(line.back() - out.back()) < kSpacing / 2)
		out.back() = line.back();
	else
		out.push_back(line.back());
	return out;
}

// Grows the river network in the canonical frame. Springs join the nearest point downstream of
// them on the network so far, nearest first, which grows a branching tree the way a spanning tree
// does. A path that would run out into the sea is refused; one that runs into another river joins
// that river instead.
class RiverPlanner
{
  public:
	struct Attachment
	{
		double distance2;
		int reach, index;
	};

	RiverPlanner(const Frame &frame, const Ripple &north, const Ripple &south, double clearance,
				 double northMargin, double southMargin)
		: along(frame.along), across(frame.across), north(north), south(south),
		  clearance(clearance), northMargin(northMargin), southMargin(southMargin)
	{
		columns = std::max(1, along / 24);
		rows = std::max(1, across / 24);
		cellW = double(along) / columns;
		cellH = double(across) / rows;
		cells.resize(size_t(columns) * rows);
	}

	std::vector<Reach> reaches;
	// Per reach: where each tributary joins it, and from which side.
	std::vector<std::vector<std::pair<int, int>>> joins;
	int trunk = -1;
	// 0 plans every river without its meander.
	double meanderScale = 1.0;

	int sideOf(int r, int i, Vec source) const
	{
		const Vec t = tangent(r, i), p = reaches[r].points[i];
		const Vec d{centred(source.x - p.x, along), source.y - p.y};
		return t.x * d.y - t.y * d.x >= 0 ? 1 : -1;
	}

	double distance2(Vec a, Vec b) const { return sq(centred(a.x - b.x, along)) + sq(a.y - b.y); }

	int add(Reach reach)
	{
		reaches.push_back(std::move(reach));
		joins.emplace_back();
		const int id = int(reaches.size()) - 1;
		const auto &points = reaches[id].points;
		for (size_t i = 0; i < points.size(); i += 2)
			cells[cellOf(points[i])].push_back({id, int(i)});
		return id;
	}

	// Squared distance to the nearest indexed river point within about one cell, or the largest
	// double when there is none that close; `only` limits the search to one reach.
	double nearest2(Vec p, int ignore = -1, int only = -1, int *nearestReach = nullptr) const
	{
		const int cx = column(p), cy = row(p);
		int xs[3], count = 0;
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int x = wrapIndex(cx + dx, columns);
			if (std::find(xs, xs + count, x) == xs + count)
				xs[count++] = x;
		}
		double best = std::numeric_limits<double>::max();
		for (int dy = -1; dy <= 1; ++dy)
		{
			const int y = cy + dy;
			if (y < 0 || y >= rows)
				continue;
			for (int k = 0; k < count; ++k)
				for (const auto &entry : cells[size_t(y) * columns + xs[k]])
					if (entry.first != ignore && (only < 0 || entry.first == only))
					{
						const double d = distance2(p, reaches[entry.first].points[entry.second]);
						if (d < best)
						{
							best = d;
							if (nearestReach)
								*nearestReach = entry.first;
						}
					}
		}
		return best;
	}

	// The point of reach r nearest to p where a spring at `source` may join it, if one is close.
	int nearestAttachable(int r, Vec p, Vec source) const
	{
		int best = -1;
		double bestDistance = sq(2 * clearance);
		for (int i = 0; i < int(reaches[r].points.size()); i += 2)
		{
			if (!attachable(r, i, source))
				continue;
			const Vec q = reaches[r].points[i];
			const double du = centred(q.x - source.x, along), dv = q.y - source.y;
			if (dv < std::max(4.0, kSlope * std::abs(du)))
				continue;
			const double d = distance2(p, q);
			if (d < bestDistance)
			{
				bestDistance = d;
				best = i;
			}
		}
		return best;
	}

	bool inland(Vec p, double northGap, double southGap) const
	{
		return p.y >= north.at(p.x) + northGap && p.y <= south.at(p.x) - southGap;
	}

	Vec tangent(int r, int i) const
	{
		const auto &points = reaches[r].points;
		const int a = std::max(0, i - 2), b = std::min(int(points.size()) - 1, i + 2);
		return normalized(
			Vec{centred(points[b].x - points[a].x, along), points[b].y - points[a].y});
	}

	// A cubic curve that leaves `from` towards `to` and arrives leaning into the direction the
	// river it joins is flowing, with a meander that fades to nothing at both ends.
	std::vector<Vec> path(Vec from, Vec to, Vec endDirection, double meander,
						  GenerationContext &context) const
	{
		const Vec delta{centred(to.x - from.x, along), to.y - from.y};
		const Vec end = from + delta;
		const double span = std::max(1.0, length(delta));
		const Vec blend = normalized(endDirection * 0.4 + normalized(delta) * 0.6);
		const Vec c1 = from + delta * 0.33, c2 = end - blend * (0.33 * span);
		const int steps = std::max(16, int(span * 3));
		std::vector<Vec> raw;
		raw.reserve(steps + 1);
		for (int k = 0; k <= steps; ++k)
		{
			const double t = double(k) / steps, s = 1 - t;
			raw.push_back(from * (s * s * s) + c1 * (3 * s * s * t) + c2 * (3 * s * t * t) +
						  end * (t * t * t));
		}
		const std::vector<Vec> base = resample(raw);
		const double amplitude = std::min(6.5, 0.1 * span) * meander * meanderScale;
		const double waves = std::max(1.0, span / 34.0);
		const double phase1 = between(context, kRiverStream, 0, 2 * kPi);
		const double phase2 = between(context, kRiverStream, 0, 2 * kPi);
		const double last = double(base.size() - 1);
		std::vector<Vec> bent(base.size());
		for (size_t i = 0; i < base.size(); ++i)
		{
			const double t = last > 0 ? i / last : 0;
			const Vec direction =
				normalized(base[std::min(base.size() - 1, i + 1)] - base[i ? i - 1 : 0]);
			const double offset = amplitude * std::sin(kPi * t) *
								  (0.75 * std::sin(2 * kPi * waves * t + phase1) +
								   0.25 * std::sin(2 * kPi * 2.3 * waves * t + phase2));
			bent[i] = base[i] + Vec{-direction.y, direction.x} * offset;
		}
		return resample(bent);
	}

	// Where along the network a tributary may join: not on the delta, not at a spring or a
	// junction, and not beside another tributary's mouth.
	bool attachable(int r, int i, Vec source) const
	{
		const Reach &reach = reaches[r];
		if (reach.distributary)
			return false;
		const double s = i * kSpacing;
		if (s < 8 || reach.length() - s < 8)
			return false;
		// Junctions on the same bank well apart leave straight stretches between them to ford, and
		// send later springs to join tributaries instead of all lining up along the trunk; one from
		// the opposite bank only needs room for its own mouth.
		const int side = sideOf(r, i, source);
		for (const auto &join : joins[r])
			if (std::abs(i - join.first) * kSpacing < (join.second == side ? 20 : 8))
				return false;
		return true;
	}

	std::vector<Attachment> attachments(Vec source, int firstReach = 0) const
	{
		std::vector<Attachment> out;
		for (int r = firstReach; r < int(reaches.size()); ++r)
			for (int i = 0; i < int(reaches[r].points.size()); i += 2)
			{
				if (!attachable(r, i, source))
					continue;
				const Vec p = reaches[r].points[i];
				const double du = centred(p.x - source.x, along), dv = p.y - source.y;
				if (dv < std::max(4.0, kSlope * std::abs(du)))
					continue;
				out.push_back({du * du + dv * dv, r, i});
			}
		std::sort(out.begin(), out.end(),
				  [](const Attachment &a, const Attachment &b)
				  {
					  if (a.distance2 != b.distance2)
						  return a.distance2 < b.distance2;
					  return a.reach != b.reach ? a.reach < b.reach : a.index < b.index;
				  });
		return out;
	}

	struct Verdict
	{
		bool ok = false;
		int blocking = -1; // the other river the path ran into, when that is why it was refused
		Vec where{0, 0};
	};

	Verdict review(const std::vector<Vec> &points, int parent) const
	{
		Verdict verdict;
		const size_t n = points.size();
		if ((n - 1) * kSpacing < 12)
			return verdict;
		const double joinZone = clearance + 4;
		for (size_t i = 0; i < n; i += 2)
		{
			const double toEnd = (n - 1 - i) * kSpacing;
			const Vec p = points[i];
			if (toEnd > 4 && !inland(p, northMargin, southMargin))
				return verdict;
			if (toEnd <= 6)
				continue;
			// Clear of every other river; and while it swings in to join its parent, arriving at an
			// angle rather than running alongside it.
			// Not near: it is a legacy macro in Windows' windef.h and expands to nothing there.
			int nearestRiver = -1;
			if (nearest2(p, parent, -1, &nearestRiver) <
				sq(toEnd > joinZone ? clearance : 0.75 * clearance))
			{
				verdict.blocking = nearestRiver;
				verdict.where = p;
				return verdict;
			}
			if (nearest2(p, -1, parent) < sq(std::min(clearance, 0.35 * toEnd)))
				return verdict;
		}
		verdict.ok = true;
		return verdict;
	}

	void attachSources(const std::vector<Vec> &sources, GenerationContext &context)
	{
		const double none = std::numeric_limits<double>::max();
		std::vector<double> cached(sources.size(), none);
		std::vector<unsigned char> alive(sources.size(), 1);
		for (size_t k = 0; k < sources.size(); ++k)
		{
			const auto list = attachments(sources[k]);
			if (!list.empty())
				cached[k] = list.front().distance2;
		}
		while (true)
		{
			int pick = -1;
			for (size_t k = 0; k < sources.size(); ++k)
				if (alive[k] && cached[k] < none && (pick < 0 || cached[k] < cached[pick]))
					pick = int(k);
			if (pick < 0)
				return;
			const auto list = attachments(sources[pick]);
			if (list.empty())
			{
				alive[pick] = 0;
				continue;
			}
			// A cached distance can only be optimistic (junctions since added rule points out), so a
			// spring whose real best is further than cached waits its proper turn.
			if (list.front().distance2 > cached[pick] + 1e-9)
			{
				cached[pick] = list.front().distance2;
				continue;
			}
			alive[pick] = 0;
			std::vector<Vec> tried;
			int added = -1;
			for (const Attachment &a : list)
			{
				if (tried.size() >= 12 || added >= 0)
					break;
				const Vec p = reaches[a.reach].points[a.index];
				bool repeat = false;
				for (Vec t : tried)
					repeat = repeat || distance2(t, p) < 36;
				if (repeat)
					continue;
				tried.push_back(p);
				// A full meander first, then a gentler one. A river joins whatever river it runs into:
				// a path refused for passing too near another river is tried again as its tributary.
				int target = a.reach, index = a.index;
				for (int hop = 0; hop < 3 && added < 0; ++hop)
				{
					Verdict last;
					for (double meander : {1.0, 0.35})
					{
						Reach reach;
						reach.points = path(sources[pick], reaches[target].points[index],
											tangent(target, index), meander, context);
						last = review(reach.points, target);
						if (!last.ok)
							continue;
						reach.parent = target;
						reach.joinIndex = index;
						const int side = sideOf(target, index, sources[pick]);
						added = add(std::move(reach));
						joins[target].push_back({index, side});
						break;
					}
					if (added >= 0 || last.blocking < 0)
						break;
					// Nothing joins the delta itself: a path that runs into it joins the trunk above it.
					const int blocking =
						reaches[last.blocking].distributary ? trunk : last.blocking;
					const int next =
						blocking >= 0 ? nearestAttachable(blocking, last.where, sources[pick]) : -1;
					if (next < 0)
						break;
					target = blocking;
					index = next;
				}
			}
			if (added < 0)
				continue;
			for (size_t k = 0; k < sources.size(); ++k)
			{
				if (!alive[k])
					continue;
				if (nearest2(sources[k]) < sq(clearance))
				{
					alive[k] = 0; // it would only add a stub beside the river that just passed it
					continue;
				}
				const auto fresh = attachments(sources[k], added);
				if (!fresh.empty())
					cached[k] = std::min(cached[k], fresh.front().distance2);
			}
		}
	}

  private:
	int column(Vec p) const { return std::min(columns - 1, int(wrapped(p.x, along) / cellW)); }
	int row(Vec p) const { return std::clamp(int(std::floor(p.y / cellH)), 0, rows - 1); }
	size_t cellOf(Vec p) const { return size_t(row(p)) * columns + column(p); }

	int along, across;
	const Ripple &north, &south;
	double clearance, northMargin, southMargin;
	int columns, rows;
	double cellW, cellH;
	std::vector<std::vector<std::pair<int, int>>> cells;
};

// Fords go where a river can be crossed cleanly: the middle of every straight, uncluttered run of
// river, longest runs first, about one per `spacing` tiles of each reach.
void chooseFords(Layout &layout, const WatershedOptions &o)
{
	const double spacing = kFordSpacing[std::clamp(o.fords, 1, 5) - 1];
	const Frame &frame = layout.frame;
	auto distance2 = [&](Vec a, Vec b)
	{ return sq(centred(a.x - b.x, frame.width)) + sq(centred(a.y - b.y, frame.height)); };
	const int count = int(layout.reaches.size());
	const ChannelIndex channels(layout);
	std::vector<int> order;
	for (int r = layout.trunk; r < count; ++r)
		order.push_back(r);
	for (int r = 0; r < layout.trunk; ++r)
		order.push_back(r);
	for (int r : order)
	{
		const Reach &reach = layout.reaches[r];
		const int n = int(reach.points.size());
		const double len = reach.length();
		if (len < std::max(20.0, spacing / 2))
			continue;
		auto valid = [&](int i)
		{
			if (i < 16 || i >= n - 16)
				return false;
			const double radius = reach.radius[i], span = radius + kFordReach;
			if (reach.coastGap[i] < span + 6)
				return false;
			for (int j : layout.joins[r])
				if (std::abs(i - j) * kSpacing < radius + 8)
					return false;
			// Only on a straight stretch, eight tiles either way, where the channel runs on in line
			// beyond both sides of the ford and is no wider across than its radius.
			const Vec before = reach.points[i] - reach.points[i - 16];
			const Vec after = reach.points[i + 16] - reach.points[i];
			if (dot(normalized(before), normalized(after)) < std::cos(25 * kPi / 180))
				return false;
			// The crossing, and dry ground a little past both of its ends, touch no other channel:
			// a ford crosses one river, not a junction, a spring's pond or a neighbouring bend.
			const Vec along = normalized(reach.points[i + 2] - reach.points[i - 2]);
			const Vec across{-along.y, along.x};
			for (double s = -(span + 1); s <= span + 1 + 1e-9; s += 1.0)
				for (double a : {-kFordHalfWidth, 0.0, kFordHalfWidth})
					if (!channels.clear(reach.points[i] + across * s + along * a,
										std::abs(s) <= span ? 1.5 : 0.5, r, i, span + 6))
						return false;
			return true;
		};
		std::vector<std::pair<int, int>> runs; // first and last fordable point, a tile apart
		for (int i = 12; i < n - 12; i += 2)
		{
			if (!valid(i))
				continue;
			if (!runs.empty() && runs.back().second == i - 2)
				runs.back().second = i;
			else
				runs.push_back({i, i});
		}
		struct Spot
		{
			int index;
			int run;
		};
		std::vector<Spot> spots;
		for (const auto &run : runs)
		{
			const int steps = (run.second - run.first) / 2;
			const int extra = int(steps / spacing);
			for (int k = 0; k <= extra; ++k)
				spots.push_back({run.first + 2 * int(steps * (k + 0.5) / (extra + 1)), steps});
		}
		std::stable_sort(spots.begin(), spots.end(),
						 [](const Spot &a, const Spot &b) { return a.run > b.run; });
		const int limit = std::max(1, int(std::lround(len / spacing)));
		std::vector<int> chosen;
		for (const Spot &spot : spots)
		{
			if (int(chosen.size()) >= limit)
				break;
			const double span = reach.radius[spot.index] + kFordReach;
			bool apart = true;
			for (int c : chosen)
				apart = apart && std::abs(c - spot.index) * kSpacing >= 0.75 * spacing;
			for (const Ford &f : layout.fords)
				apart = apart &&
						distance2(reach.points[spot.index], f.center) >= sq(f.span + span + 10);
			if (!apart)
				continue;
			chosen.push_back(spot.index);
			const Vec along =
				normalized(reach.points[spot.index + 2] - reach.points[spot.index - 2]);
			layout.fords.push_back({reach.points[spot.index], along, Vec{-along.y, along.x}, span});
		}
	}
}

// The whole plan: a pure function of the request's size, options and seed.
Layout planLayout(int width, int height, const WatershedOptions &o, GenerationContext &context)
{
	Layout layout;
	Frame &frame = layout.frame;
	frame.width = width;
	frame.height = height;
	// Any of the four sides on a square map. On a rectangular one the rivers run the long way, with
	// the sea across one of its short ends: one network cannot gather a coast twice as long as the
	// land is deep, and would leave most of the map a desert.
	frame.orientation = int(context.bounded(kLayoutStream, width == height ? 4 : 2));
	if (width > height)
		frame.orientation += 2;
	frame.along = frame.orientation < 2 ? width : height;
	frame.across = frame.orientation < 2 ? height : width;
	const double along = frame.along, across = frame.across;

	// One band of sea along the map's edge: deep off the delta, and a narrow strait along the
	// uplands on the far side of the torus seam.
	const double sea = std::clamp(std::round(8 + 0.14 * across), 16.0, 64.0);
	layout.north = makeRipple(context, 0.35 * sea, 0.08 * sea + 1.0, frame.along);
	layout.south = makeRipple(context, across - 0.65 * sea, 0.2 * sea, frame.along);

	const double minimumRadius = channelRadius(o.riverWidth);
	const double gain = 0.35 + 0.1 * o.riverWidth;
	const double estimate = minimumRadius + 0.5;
	const double clearance = 2 * estimate + 5;
	const double northMargin = estimate + 8, southMargin = estimate + 6;
	RiverPlanner planner(frame, layout.north, layout.south, clearance, northMargin, southMargin);
	planner.meanderScale = o.meanders ? 1.0 : 0.0;

	// The delta: one to three distributaries fanning out from an apex to the coast. Without a
	// delta there is no lobe and only the middle mouth is kept; the others are still planned, so
	// every later draw is the one it would have been.
	const double deltaLength = std::clamp(0.12 * across + 6, 12.0, 44.0);
	const double spread = std::clamp(0.16 * along, 18.0, 44.0);
	const int mouthLimit = std::max(1, int(0.7 * along / spread) + 1);
	const int mouthCount = std::min(mouthLimit, 1 + int(context.bounded(kLayoutStream, 3)));
	const double u0 = between(context, kLayoutStream, 0, along);
	layout.south.lobeCentre = u0;
	layout.south.lobeHeight = o.delta ? 0.3 * sea : 0.0;
	layout.south.lobeWidth = 1.1 * spread + 6;
	const Vec apex{u0, layout.south.at(u0) - deltaLength};
	static const double shares[3][3] = {{1, 0, 0}, {0.5, 0.5, 0}, {0.32, 0.36, 0.32}};
	std::vector<Vec> mouths;
	for (int m = 0; m < mouthCount; ++m)
	{
		const double u = u0 + (m - (mouthCount - 1) / 2.0) * spread *
								  between(context, kLayoutStream, 0.85, 1.15);
		Reach reach;
		reach.points = planner.path(apex, {u, layout.south.at(u) + std::min(8.0, 0.25 * sea)},
									{0, 1}, 0.5, context);
		reach.distributary = true;
		if (!o.delta && m != mouthCount / 2)
			continue;
		mouths.push_back({u, layout.south.at(u)});
		planner.add(std::move(reach));
	}

	// The trunk rises well up in the uplands; every other spring joins the network it starts.
	const double headU = u0 + between(context, kLayoutStream, -0.15, 0.15) * along;
	const double top = layout.north.at(headU) + northMargin;
	const Vec head{headU,
				   top + std::max(0.0, apex.y - top) * between(context, kLayoutStream, 0.1, 0.3)};
	{
		Reach reach;
		reach.points = planner.path(head, apex, {0, 1}, 1.0, context);
		layout.trunk = planner.add(std::move(reach));
		planner.trunk = layout.trunk;
	}
	const int trunk = layout.trunk;

	const double landArea = along * (across - sea);
	const int wanted = int(std::lround(landArea * o.riverDensity * 1.5e-4));
	const double sourceSpacing = 0.7 * std::sqrt(landArea / (wanted + 1.0));
	// A spring that finds no way onto the network is dropped, so springs are sampled again wherever
	// there is still room until enough have joined it.
	for (int round = 0; round < 6; ++round)
	{
		const int missing = wanted - (int(planner.reaches.size()) - trunk - 1);
		if (missing <= 0)
			break;
		std::vector<Vec> sources;
		for (int attempt = 0; attempt < 60 * missing && int(sources.size()) < missing; ++attempt)
		{
			const double u = between(context, kRiverStream, 0, along);
			// Upstream of the delta's apex: nearer the sea than that, there is no river to run down to.
			const double lo = layout.north.at(u) + northMargin;
			const double hi =
				std::min(layout.south.at(u) - (deltaLength + southMargin), apex.y - 6);
			const double v = between(context, kRiverStream, 0, 1);
			if (hi <= lo)
				continue;
			const Vec s{u, lo + (hi - lo) * v};
			bool clear = planner.nearest2(s) >= sq(clearance);
			for (size_t k = 0; clear && k < sources.size(); ++k)
				clear = planner.distance2(s, sources[k]) >= sq(sourceSpacing);
			for (int r = trunk + 1; clear && r < int(planner.reaches.size()); ++r)
				clear =
					planner.distance2(s, planner.reaches[r].points.front()) >= sq(sourceSpacing);
			if (clear)
				sources.push_back(s);
		}
		planner.attachSources(sources, context);
	}

	// Width grows with the number of springs upstream.
	const int count = int(planner.reaches.size());
	std::vector<double> outflow(count, 1.0);
	std::vector<std::vector<int>> children(count);
	for (int r = count - 1; r > trunk; --r)
	{
		outflow[planner.reaches[r].parent] += outflow[r];
		children[planner.reaches[r].parent].push_back(r);
	}
	auto radiusFor = [&](double flow)
	{ return std::min(minimumRadius + 3.5, minimumRadius + gain * (std::sqrt(flow) - 1)); };
	for (int r = trunk; r < count; ++r)
	{
		Reach &reach = planner.reaches[r];
		std::vector<std::pair<int, double>> inflows;
		for (int c : children[r])
			inflows.push_back({planner.reaches[c].joinIndex, outflow[c]});
		std::sort(inflows.begin(), inflows.end());
		double flow = 1;
		size_t next = 0;
		reach.radius.resize(reach.points.size());
		for (size_t i = 0; i < reach.points.size(); ++i)
		{
			while (next < inflows.size() && inflows[next].first <= int(i))
				flow += inflows[next++].second;
			reach.radius[i] = radiusFor(flow);
		}
	}
	const int distributaries = int(mouths.size());
	for (int m = 0; m < distributaries; ++m)
	{
		Reach &reach = planner.reaches[m];
		const double radius =
			radiusFor(std::max(1.0, outflow[trunk] * shares[distributaries - 1][m]));
		const double last = double(reach.points.size() - 1);
		reach.radius.resize(reach.points.size());
		for (size_t i = 0; i < reach.points.size(); ++i)
			reach.radius[i] = radius + 0.8 * (i / last);
	}

	// Into map coordinates.
	for (Reach &reach : planner.reaches)
	{
		reach.coastGap.resize(reach.points.size());
		for (size_t i = 0; i < reach.points.size(); ++i)
		{
			const Vec c = reach.points[i];
			reach.coastGap[i] = std::min(c.y - layout.north.at(c.x), layout.south.at(c.x) - c.y);
		}
	}
	for (int r = trunk + 1; r < count; ++r)
	{
		const Reach &reach = planner.reaches[r];
		layout.junctions.push_back(
			frame.toMap(planner.reaches[reach.parent].points[reach.joinIndex]));
	}
	layout.junctions.push_back(frame.toMap(apex));
	for (int r = trunk; r < count; ++r)
	{
		std::vector<Vec> line;
		Vec p = planner.reaches[r].points.front();
		for (int k = 0; k < frame.across * 2; ++k)
		{
			line.push_back(frame.toMap(p));
			if (p.y < layout.north.at(p.x) - 1)
				break;
			p.y -= kSpacing;
		}
		layout.divides.push_back(std::move(line));
	}
	for (Vec m : mouths)
		layout.mouths.push_back(frame.toMap(m));
	for (Reach &reach : planner.reaches)
		for (Vec &p : reach.points)
			p = frame.toMap(p);
	layout.reaches = std::move(planner.reaches);
	layout.joins.resize(planner.joins.size());
	for (size_t r = 0; r < planner.joins.size(); ++r)
		for (const auto &join : planner.joins[r])
			layout.joins[r].push_back(join.first);
	chooseFords(layout, o);
	return layout;
}

// Distance in fifths of a tile (5 across, 7 diagonally) from every vertex to the nearest water.
std::vector<int> waterDistance(const std::vector<unsigned char> &terrain, int w, int h)
{
	const int unreached = std::numeric_limits<int>::max();
	std::vector<int> distance(terrain.size(), unreached);
	std::array<std::vector<int>, 8> buckets;
	size_t pending = 0;
	for (size_t i = 0; i < terrain.size(); ++i)
		if (terrain[i] == WATER)
		{
			distance[i] = 0;
			buckets[0].push_back(int(i));
			++pending;
		}
	for (int current = 0; pending > 0; ++current)
	{
		std::vector<int> items;
		items.swap(buckets[current % 8]);
		pending -= items.size();
		for (int p : items)
		{
			if (distance[p] != current)
				continue;
			const int x = p % w, y = p / w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					const int q = wrapIndex(y + dy, h) * w + wrapIndex(x + dx, w);
					const int next = current + (dx && dy ? 7 : 5);
					if (next < distance[q])
					{
						distance[q] = next;
						buckets[next % 8].push_back(q);
						++pending;
					}
				}
		}
	}
	return distance;
}

void stampFord(std::vector<unsigned char> &terrain, int w, int h, const Ford &f)
{
	const int reach = int(std::ceil(f.span + kFordHalfWidth)) + 1;
	const int cx = int(std::floor(f.center.x)), cy = int(std::floor(f.center.y));
	for (int dy = -reach; dy <= reach; ++dy)
		for (int dx = -reach; dx <= reach; ++dx)
		{
			const Vec d{cx + dx - f.center.x, cy + dy - f.center.y};
			if (std::abs(dot(d, f.along)) > kFordHalfWidth || std::abs(dot(d, f.across)) > f.span)
				continue;
			unsigned char &t = terrain[size_t(wrapIndex(cy + dy, h)) * w + wrapIndex(cx + dx, w)];
			if (t == WATER)
				t = SAND;
		}
}

std::vector<unsigned char> stampTerrain(const Layout &layout, const WatershedOptions &o,
										GenerationContext &context)
{
	const Frame &frame = layout.frame;
	const int w = frame.width, h = frame.height;
	std::vector<unsigned char> terrain(size_t(w) * h, GRASS);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const Vec c = frame.toCanonical({double(x), double(y)});
			if (c.y < layout.north.at(c.x) || c.y > layout.south.at(c.x))
				terrain[size_t(y) * w + x] = WATER;
		}
	auto disc = [&](Vec centre, double radius)
	{
		const int x0 = int(std::floor(centre.x - radius)), x1 = int(std::ceil(centre.x + radius));
		const int y0 = int(std::floor(centre.y - radius)), y1 = int(std::ceil(centre.y + radius));
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
				if (sq(x - centre.x) + sq(y - centre.y) <= radius * radius)
					terrain[size_t(wrapIndex(y, h)) * w + wrapIndex(x, w)] = WATER;
	};
	for (const Reach &reach : layout.reaches)
	{
		for (size_t i = 0; i < reach.points.size(); ++i)
			disc(reach.points[i], reach.radius[i]);
		if (!reach.distributary)
			disc(reach.points.front(), reach.radius.front() + 1.3); // the spring's pond
	}

	// Uplands far from any water dry out to sand. The floor keeps a floodplain of grass wide enough
	// to farm and build on beside every river, whatever the setting.
	if (o.dryness > 0)
	{
		const std::vector<int> distance = waterDistance(terrain, w, h);
		std::mt19937 &random = context.stream("watershed-terrain");
		const PeriodicNoise coarse(w, h, 28, random), fine(w, h, 9, random);
		const double reach = 24.0 - 1.3 * o.dryness;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const size_t i = size_t(y) * w + x;
				if (terrain[i] != GRASS)
					continue;
				const double n = 0.7 * coarse.at(x, y) + 0.3 * fine.at(x, y);
				if (distance[i] > 5 * std::max(11.0, reach + 12.0 * (n - 0.5)))
					terrain[i] = SAND;
			}
	}

	// Map::controlSand's rule, applied to every vertex at once instead of in raster order: water
	// touching grass and grass touching water both become sand, so every shore gets the same sand
	// on both sides and the result is already a fixed point of controlSand.
	const std::vector<unsigned char> before = terrain;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			if (before[i] == SAND)
				continue;
			const unsigned char other = before[i] == WATER ? GRASS : WATER;
			for (int dy = -1; dy <= 1 && terrain[i] != SAND; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					if (before[size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w)] == other)
					{
						terrain[i] = SAND;
						break;
					}
		}
	for (const Ford &f : layout.fords)
		stampFord(terrain, w, h, f);
	return terrain;
}

bool vertexTileWater(const std::vector<unsigned char> &t, int w, int h, int x, int y)
{
	const int x1 = (x + 1) % w, y1 = (y + 1) % h;
	return t[size_t(y) * w + x] == WATER && t[size_t(y) * w + x1] == WATER &&
		   t[size_t(y1) * w + x] == WATER && t[size_t(y1) * w + x1] == WATER;
}

// Every ford interrupts a real channel - open water runs on in line just upstream and downstream
// of it - is open from bank to bank along three parallel lines, and reaches dry land at both
// ends. `water(x, y)` answers for a tile.
template <typename Water> std::string checkFords(const Layout &layout, Water water)
{
	const int w = layout.frame.width, h = layout.frame.height;
	auto wet = [&](Vec p)
	{ return water(wrapIndex(int(std::floor(p.x)), w), wrapIndex(int(std::floor(p.y)), h)); };
	auto where = [&](const Ford &f)
	{
		return " at (" + std::to_string(wrapIndex(int(f.center.x), w)) + ", " +
			   std::to_string(wrapIndex(int(f.center.y), h)) + ")";
	};
	// Open water within a tile of a point: a thin or diagonal channel's water core need not cover
	// any one exact tile once its shores are sanded.
	auto wetNear = [&](Vec p)
	{
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (wet(p + Vec{double(dx), double(dy)}))
					return true;
		return false;
	};
	for (const Ford &f : layout.fords)
	{
		for (int side : {-1, 1})
			if (!wetNear(f.center + f.along * (side * (kFordHalfWidth + 2.5))))
				return "A ford" + where(f) + " does not cross a channel.";
		for (int a = -1; a <= 1; ++a)
			for (double s = -f.span; s <= f.span + 1e-9; s += kSpacing)
				if (wet(f.center + f.along * a + f.across * s))
					return "A ford" + where(f) + " is cut by open water.";
		for (int side : {-1, 1})
			if (wet(f.center + f.across * (side * (f.span + 1.0))))
				return "A ford" + where(f) + " does not reach its bank.";
	}
	return "";
}

// Every channel keeps a 4-connected core of open water from its spring to where it ends, except
// where a ford crosses it - so the fords are the only way over on foot.
template <typename Water> std::string checkChannels(const Layout &layout, Water water)
{
	const int w = layout.frame.width, h = layout.frame.height;
	for (const Reach &reach : layout.reaches)
	{
		bool previous = false;
		int px = 0, py = 0;
		for (size_t i = 0; i < reach.points.size(); ++i)
		{
			const Vec p = reach.points[i];
			bool skip = reach.coastGap[i] < 2.0;
			for (size_t k = 0; !skip && k < layout.fords.size(); ++k)
			{
				const Ford &f = layout.fords[k];
				const Vec d{centred(p.x - f.center.x, w), centred(p.y - f.center.y, h)};
				skip = std::abs(dot(d, f.along)) <= kFordHalfWidth + 2 &&
					   std::abs(dot(d, f.across)) <= f.span + 1;
			}
			if (skip)
			{
				previous = false;
				continue;
			}
			const int x = wrapIndex(int(std::floor(p.x)), w),
					  y = wrapIndex(int(std::floor(p.y)), h);
			if (!water(x, y))
				return "A river channel silts up at (" + std::to_string(x) + ", " +
					   std::to_string(y) + ").";
			if (previous && x != px && y != py && !water(x, py) && !water(px, y))
				return "A river channel can be stepped across at (" + std::to_string(x) + ", " +
					   std::to_string(y) + ").";
			previous = true;
			px = x;
			py = y;
		}
	}
	return "";
}

// Labels 8-connected components of open tiles; closed tiles get -1.
std::vector<int> components(const std::vector<unsigned char> &open, int w, int h, int &count)
{
	std::vector<int> label(open.size(), -1);
	std::vector<int> queue;
	queue.reserve(open.size());
	count = 0;
	for (size_t start = 0; start < open.size(); ++start)
	{
		if (!open[start] || label[start] >= 0)
			continue;
		queue.clear();
		queue.push_back(int(start));
		label[start] = count;
		for (size_t head = 0; head < queue.size(); ++head)
		{
			const int x = queue[head] % w, y = queue[head] / w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const size_t q = size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w);
					if (open[q] && label[q] < 0)
					{
						label[q] = count;
						queue.push_back(int(q));
					}
				}
		}
		++count;
	}
	return label;
}

// The finished terrain seen tile by tile.
struct Tiles
{
	int w = 0, h = 0;
	std::vector<unsigned char> water, grass;
	std::vector<int> waterSteps; // 8-neighbour steps to the nearest water tile
	std::vector<int> grassDepth; // 8-neighbour steps to the nearest tile that isn't pure grass
	std::vector<int> component;  // walkable component, -1 on water
	int mainComponent = -1;
};

// Steps from every tile to the nearest tile where `seed` holds.
std::vector<int> stepsFrom(const std::vector<unsigned char> &seed, int w, int h)
{
	std::vector<int> steps(seed.size(), -1), queue;
	queue.reserve(seed.size());
	for (size_t i = 0; i < seed.size(); ++i)
		if (seed[i])
		{
			steps[i] = 0;
			queue.push_back(int(i));
		}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int x = queue[head] % w, y = queue[head] / w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const size_t q = size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w);
				if (steps[q] < 0)
				{
					steps[q] = steps[queue[head]] + 1;
					queue.push_back(int(q));
				}
			}
	}
	return steps;
}

Tiles tileView(const std::vector<unsigned char> &terrain, int w, int h)
{
	Tiles t;
	t.w = w;
	t.h = h;
	const size_t n = size_t(w) * h;
	t.water.assign(n, 0);
	t.grass.assign(n, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			const int x1 = (x + 1) % w, y1 = (y + 1) % h;
			const unsigned char a = terrain[i], b = terrain[size_t(y) * w + x1];
			const unsigned char c = terrain[size_t(y1) * w + x], d = terrain[size_t(y1) * w + x1];
			t.water[i] = a == WATER && b == WATER && c == WATER && d == WATER;
			t.grass[i] = a == GRASS && b == GRASS && c == GRASS && d == GRASS;
		}
	t.waterSteps = stepsFrom(t.water, w, h);
	std::vector<unsigned char> land(n), notGrass(n);
	for (size_t i = 0; i < n; ++i)
	{
		land[i] = !t.water[i];
		notGrass[i] = !t.grass[i];
	}
	t.grassDepth = stepsFrom(notGrass, w, h);
	int count = 0;
	t.component = components(land, w, h, count);
	std::vector<int> sizes(count, 0);
	for (int c : t.component)
		if (c >= 0)
			++sizes[c];
	if (count)
		t.mainComponent = int(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
	return t;
}

// The banks: land split by the rivers, by every ford as if it were still water, and by a line
// from each spring upstream to the sea, so the ground either side of a river counts as two banks
// even though a colony could walk around its spring.
std::vector<int> bankRegions(const Layout &layout, const Tiles &t)
{
	const int w = t.w, h = t.h;
	std::vector<unsigned char> open(t.water.size());
	for (size_t i = 0; i < open.size(); ++i)
		open[i] = !t.water[i];
	auto close = [&](int x, int y) { open[size_t(wrapIndex(y, h)) * w + wrapIndex(x, w)] = 0; };
	for (const Ford &f : layout.fords)
	{
		const int reach = int(std::ceil(f.span + kFordHalfWidth)) + 2;
		const int cx = int(std::floor(f.center.x)), cy = int(std::floor(f.center.y));
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
			{
				const Vec d{cx + dx + 0.5 - f.center.x, cy + dy + 0.5 - f.center.y};
				if (std::abs(dot(d, f.along)) <= kFordHalfWidth + 1 &&
					std::abs(dot(d, f.across)) <= f.span + 1)
					close(cx + dx, cy + dy);
			}
	}
	for (const auto &line : layout.divides)
		for (Vec p : line)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					close(int(std::floor(p.x)) + dx, int(std::floor(p.y)) + dy);
	int count = 0;
	return components(open, w, h, count);
}

// Swarm anchors on the floodplain of the main landmass, clear of the fords, spread as far apart
// as possible and, where the map allows, each on a bank of its own.
std::vector<MapGeneratorPoint> chooseSites(const Layout &layout, const Tiles &t,
										   const std::vector<int> &regions,
										   const Fertility::Field &fertility, int teams,
										   GenerationContext &context, std::string &detail)
{
	const int w = t.w, h = t.h;
	struct Site
	{
		int x, y, region;
		double fertility;
	};
	std::vector<Site> candidates;
	for (int y = 0; y < h; y += 2)
		for (int x = 0; x < w; x += 2)
		{
			const size_t c = size_t((y + 2) % h) * w + (x + 2) % w;
			if (t.component[c] != t.mainComponent || regions[c] < 0 ||
				t.waterSteps[c] < kColonyWaterNear || t.waterSteps[c] > kColonyWaterFar)
				continue;
			bool open = true;
			for (int dy = -2; dy <= 5 && open; ++dy)
				for (int dx = -2; dx <= 5 && open; ++dx)
					open = t.grass[size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w)] != 0;
			const Vec centre{x + 2.0, y + 2.0};
			for (size_t k = 0; open && k < layout.fords.size(); ++k)
			{
				const Ford &f = layout.fords[k];
				open =
					sq(centred(centre.x - f.center.x, w)) + sq(centred(centre.y - f.center.y, h)) >=
					sq(f.span + 12);
			}
			if (!open)
				continue;
			// How fertile the ground a young colony works is, around the swarm.
			double sum = 0;
			int samples = 0;
			for (int dy = -10; dy <= 13; dy += 3)
				for (int dx = -10; dx <= 13; dx += 3, ++samples)
					sum += fertility.at(wrapIndex(x + dx, w), wrapIndex(y + dy, h));
			candidates.push_back({x, y, regions[c], sum / samples});
		}
	if (int(candidates.size()) < teams)
	{
		detail = "only " + std::to_string(candidates.size()) + " floodplain sites for " +
				 std::to_string(teams) + " colonies";
		return {};
	}
	// Every colony starts on the more fertile half of the floodplain, so none is handed the dry
	// fringe while another gets the river bank; spacing is then chosen within that half.
	std::stable_sort(candidates.begin(), candidates.end(),
					 [](const Site &a, const Site &b) { return a.fertility > b.fertility; });
	const std::vector<Site> all = candidates;
	candidates.resize(std::max<size_t>(size_t(teams) * 6, candidates.size() / 2));
	if (candidates.size() > all.size())
		candidates = all;
	auto distance2 = [&](const Site &a, const Site &b)
	{ return sq(centred(double(a.x - b.x), w)) + sq(centred(double(a.y - b.y), h)); };
	std::vector<int> best;
	double bestScore = -1;
	std::vector<double> nearest(candidates.size());
	for (int trial = 0; trial < 32; ++trial)
	{
		// Half the trials in the fertile half; if no spacing fits there, the rest use every site.
		if (trial == 16)
		{
			if (!best.empty())
				break;
			candidates = all;
			nearest.assign(candidates.size(), 0);
		}
		std::vector<int> chosen{int(context.bounded(kStartStream, candidates.size()))};
		std::fill(nearest.begin(), nearest.end(), std::numeric_limits<double>::max());
		auto claim = [&](int k)
		{
			for (size_t c = 0; c < candidates.size(); ++c)
				nearest[c] = std::min(nearest[c], distance2(candidates[c], candidates[k]));
		};
		claim(chosen.front());
		while (int(chosen.size()) < teams)
		{
			int pick = -1;
			double pickScore = -1;
			for (size_t c = 0; c < candidates.size(); ++c)
			{
				if (nearest[c] < sq(kColonySpacing))
					continue;
				bool shared = false;
				for (int k : chosen)
					shared = shared || candidates[k].region == candidates[c].region;
				const double score = nearest[c] * (shared ? 0.3 : 1.0);
				if (score > pickScore)
				{
					pickScore = score;
					pick = int(c);
				}
			}
			if (pick < 0)
				break;
			chosen.push_back(pick);
			claim(pick);
		}
		if (int(chosen.size()) < teams)
			continue;
		double closest = std::numeric_limits<double>::max();
		int shared = 0;
		for (size_t a = 0; a < chosen.size(); ++a)
			for (size_t b = a + 1; b < chosen.size(); ++b)
			{
				closest =
					std::min(closest, distance2(candidates[chosen[a]], candidates[chosen[b]]));
				shared += candidates[chosen[a]].region == candidates[chosen[b]].region;
			}
		const double score = (teams > 1 ? closest : 1.0) * std::pow(0.5, shared);
		if (score > bestScore)
		{
			bestScore = score;
			best = chosen;
		}
	}
	if (best.empty())
	{
		detail = "the floodplain sites are too close together for " + std::to_string(teams) +
				 " colonies";
		return {};
	}
	std::vector<MapGeneratorPoint> sites;
	for (int k : best)
		sites.emplace_back(candidates[k].x, candidates[k].y);
	return sites;
}

// Walking steps from the given tiles over land that is free of resources and buildings.
std::vector<int> walk(const Map &map, const std::vector<int> &from)
{
	const int w = map.getW(), h = map.getH();
	std::vector<int> distance(size_t(w) * h, -1);
	std::vector<int> queue;
	queue.reserve(distance.size());
	for (int p : from)
		if (distance[p] < 0)
		{
			distance[p] = 0;
			queue.push_back(p);
		}
	for (size_t head = 0; head < queue.size(); ++head)
	{
		const int x = queue[head] % w, y = queue[head] / w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
				const size_t q = size_t(ny) * w + nx;
				if (distance[q] < 0 && !map.isWater(nx, ny) && !map.isResource(nx, ny) &&
					map.getBuilding(nx, ny) == NOGBID)
				{
					distance[q] = distance[queue[head]] + 1;
					queue.push_back(int(q));
				}
			}
	}
	return distance;
}

std::vector<std::vector<int>> workersByTeam(const Map &map, int teams)
{
	std::vector<std::vector<int>> workers(teams);
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
		{
			const Uint16 gid = map.getGroundUnit(x, y);
			if (gid == NOGUID)
				continue;
			const int team = Unit::GIDtoTeam(gid);
			if (team >= 0 && team < teams)
				workers[team].push_back(y * map.getW() + x);
		}
	return workers;
}

// Clumps keep a gap of open ground between them, so no two can join into a wall.
class Reservations
{
  public:
	Reservations(int w, int h) : w(w), h(h), taken(size_t(w) * h, 0) {}
	bool free(int x, int y, int radius) const
	{
		const int reach = radius + 2;
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
				if (dx * dx + dy * dy <= reach * reach &&
					taken[size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w)])
					return false;
		return true;
	}
	void reserve(int x, int y, int radius)
	{
		const int reach = radius + 1;
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
				if (dx * dx + dy * dy <= reach * reach)
					taken[size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w)] = 1;
	}

  private:
	int w, h;
	std::vector<unsigned char> taken;
};

// Every colony gets the same kit on its own floodplain: wheat and wood where the ground is most
// fertile, on different sides of the swarm, and a little stone on the drier side.
void placeStarterKits(Game &game, GenerationContext &context, int teams, const Tiles &t,
					  const Fertility::Field &fertility, Reservations &reservations)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	const auto workers = workersByTeam(map, teams);
	for (int team = 0; team < teams; ++team)
	{
		const std::vector<int> distance = walk(map, workers[team]);
		const int bx = context.bootX[team], by = context.bootY[team];
		const Vec centre{bx + 2.0, by + 2.0};
		struct Spot
		{
			int x, y, steps;
			Vec offset;
			std::uint32_t fertility;
		};
		std::vector<Spot> spots;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const int steps = distance[size_t(y) * w + x];
				if (steps < 6 || steps > 22 || !map.isGrass(x, y) ||
					!map.isResourceAllowed(x, y, CORN))
					continue;
				const Vec offset{centred(x - centre.x, w), centred(y - centre.y, h)};
				if (std::max(std::abs(offset.x), std::abs(offset.y)) < 7.5)
					continue;
				spots.push_back({x, y, steps, offset, fertility.at(x, y)});
			}
		// On the preferred side at full size if possible; then on any side; then a smaller clump a
		// little further out that may sit beside another, for a colony on a narrow strip of
		// floodplain. Any route a kit closes is opened again by connectColonies.
		auto pick = [&](int nearSteps, int farSteps, bool fertile, const std::vector<Vec> &avoid,
						double minimumAngle, int radius) -> std::pair<const Spot *, int>
		{
			for (int pass = 0; pass < 3; ++pass)
			{
				const int size = pass < 2 ? radius : 1, reach = pass < 2 ? farSteps : farSteps + 8;
				const Spot *chosen = nullptr;
				for (const Spot &s : spots)
				{
					if (s.steps < nearSteps || s.steps > reach ||
						t.grassDepth[size_t(s.y) * w + s.x] <= size ||
						(pass < 2 && !reservations.free(s.x, s.y, size)))
						continue;
					bool apart = true;
					for (Vec a : avoid)
						apart = apart && (pass > 0 || dot(normalized(a), normalized(s.offset)) <=
														  std::cos(minimumAngle * kPi / 180));
					if (!apart)
						continue;
					if (!chosen || (fertile ? s.fertility > chosen->fertility
											: s.fertility < chosen->fertility))
						chosen = &s;
				}
				if (chosen)
					return {chosen, size};
			}
			return {nullptr, 0};
		};
		std::vector<Vec> used;
		for (const auto &kit : {std::pair<int, int>{CORN, 2}, std::pair<int, int>{WOOD, 2},
								std::pair<int, int>{STONE, 1}})
		{
			const bool stone = kit.first == STONE;
			const auto [spot, size] =
				pick(stone ? 10 : 6, stone ? 14 : 10, !stone, used, stone ? 60 : 100, kit.second);
			if (!spot)
				continue;
			placeResourceClump(map, context, MapGeneratorPoint(spot->x, spot->y), kit.first, size);
			reservations.reserve(spot->x, spot->y, size);
			used.push_back(spot->offset);
		}
	}
}

// Farmland in ribbons along the rivers: clumps seeded on the most fertile free ground, 2:1 wheat
// to wood, with the crop chosen by a coarse field so stretches of bank favour one or the other.
void placeFarmland(Game &game, GenerationContext &context, const Tiles &t,
				   const Fertility::Field &fertility, const std::vector<unsigned char> &keepClear,
				   Reservations &reservations, const WatershedOptions &o)
{
	Map &map = game.map;
	const int w = t.w, h = t.h;
	std::mt19937 &random = context.stream("watershed-farmland");
	const PeriodicNoise patch(w, h, 6, random), crop(w, h, 20, random);
	struct Candidate
	{
		double score;
		int index;
	};
	std::vector<Candidate> order;
	const std::uint32_t floor = Fertility::kScale / 20;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			const std::uint32_t f = fertility.at(x, y);
			if (t.grassDepth[i] < 3 || keepClear[i] || f < floor ||
				!map.isResourceAllowed(x, y, CORN) || map.isResource(x, y))
				continue;
			order.push_back({f * (0.5 + patch.at(x + 0.5, y + 0.5)), int(i)});
		}
	std::sort(order.begin(), order.end(), [](const Candidate &a, const Candidate &b)
			  { return a.score != b.score ? a.score > b.score : a.index < b.index; });
	int budget = int(order.size()) / 5;
	// Other wheat or wood amounts than the default give each crop a budget of its own: its share
	// of the candidates the crop field gives it, scaled.
	const bool shared = o.wheat == 100 && o.wood == 100;
	int cornBudget = 0, woodBudget = 0;
	if (!shared && !order.empty())
	{
		std::int64_t cornCandidates = 0;
		for (const Candidate &c : order)
			cornCandidates += crop.at(c.index % w + 0.5, c.index / w + 0.5) < 0.62;
		const int cornShare = int(budget * cornCandidates / std::int64_t(order.size()));
		cornBudget = int(scaledCount(cornShare, o.wheat));
		woodBudget = int(scaledCount(budget - cornShare, o.wood));
	}
	for (const Candidate &c : order)
	{
		if (shared ? budget <= 0 : cornBudget <= 0 && woodBudget <= 0)
			break;
		const int x = c.index % w, y = c.index / w;
		const int radius = context.bounded("resources", 5) < 2 && t.grassDepth[c.index] > 3 ? 3 : 2;
		if (!reservations.free(x, y, radius))
			continue;
		const int type = crop.at(x + 0.5, y + 0.5) < 0.62 ? CORN : WOOD;
		int &remaining = shared ? budget : type == CORN ? cornBudget : woodBudget;
		if (remaining <= 0)
			continue;
		remaining -= placeResourceClump(map, context, MapGeneratorPoint(x, y), type, radius);
		reservations.reserve(x, y, radius);
	}
}

// Stone outcrops at the edge of the dry uplands, well away from the rivers.
void placeStone(Game &game, GenerationContext &context, const Tiles &t,
				const std::vector<unsigned char> &keepClear, Reservations &reservations,
				int stonePercent)
{
	Map &map = game.map;
	const int w = t.w, h = t.h;
	std::vector<int> candidates;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			if (!t.grass[i] || keepClear[i] || t.waterSteps[i] < 9)
				continue;
			bool edge = false;
			for (int dy = -2; dy <= 2 && !edge; ++dy)
				for (int dx = -2; dx <= 2 && !edge; ++dx)
					edge = !t.grass[size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w)];
			if (edge || t.waterSteps[i] >= 14)
				candidates.push_back(int(i));
		}
	const int outcrops = int(scaledCount(std::max(2, w * h / 3000), stonePercent));
	std::vector<MapGeneratorPoint> placed;
	for (int attempt = 0;
		 attempt < outcrops * 20 && int(placed.size()) < outcrops && !candidates.empty(); ++attempt)
	{
		const int i = candidates[context.bounded("resources", candidates.size())];
		const int x = i % w, y = i / w, radius = 1 + int(context.bounded("resources", 2));
		bool apart = reservations.free(x, y, radius) && map.isResourceAllowed(x, y, STONE);
		for (const auto &p : placed)
			apart = apart &&
					sq(centred(double(x - p.x), w)) + sq(centred(double(y - p.y), h)) >= sq(20);
		if (!apart)
			continue;
		placeResourceClump(map, context, MapGeneratorPoint(x, y), STONE, radius);
		reservations.reserve(x, y, radius);
		placed.emplace_back(x, y);
	}
}

// Fruit is rare: a small grove in the fertile wedge where two rivers meet, one kind at a time.
void placeFruit(Game &game, GenerationContext &context, const Layout &layout, const Tiles &t,
				const Fertility::Field &fertility, const std::vector<unsigned char> &keepClear,
				Reservations &reservations, int fruitPercent)
{
	Map &map = game.map;
	const int w = t.w, h = t.h;
	std::vector<Vec> junctions = layout.junctions;
	for (size_t i = junctions.size(); i > 1; --i)
		std::swap(junctions[i - 1], junctions[context.bounded("resources", i)]);
	const int groves = int(scaledCount(std::max(3, w * h / 6000), fruitPercent));
	int type = int(context.bounded("resources", 3)), placed = 0;
	for (size_t j = 0; j < junctions.size() && placed < groves; ++j)
	{
		const int jx = int(std::floor(junctions[j].x)), jy = int(std::floor(junctions[j].y));
		int bestX = 0, bestY = 0;
		std::uint32_t bestFertility = 0;
		for (int dy = -12; dy <= 12; ++dy)
			for (int dx = -12; dx <= 12; ++dx)
			{
				const int d2 = dx * dx + dy * dy;
				const int x = wrapIndex(jx + dx, w), y = wrapIndex(jy + dy, h);
				const size_t i = size_t(y) * w + x;
				if (d2 < 25 || d2 > 144 || !t.grass[i] || keepClear[i] ||
					fertility.at(x, y) <= bestFertility || !map.isResourceAllowed(x, y, CHERRY) ||
					!reservations.free(x, y, 1))
					continue;
				bestFertility = fertility.at(x, y);
				bestX = x;
				bestY = y;
			}
		if (!bestFertility)
			continue;
		placeResourceClump(map, context, MapGeneratorPoint(bestX, bestY), CHERRY + type, 1);
		reservations.reserve(bestX, bestY, 1);
		type = (type + 1) % 3;
		++placed;
	}
}

// Algae off every mouth and in the sea's shallows, where there is sand near enough for it to
// grow back.
void placeAlgae(Game &game, GenerationContext &context, const Layout &layout, const Tiles &t,
				int algaePercent)
{
	Map &map = game.map;
	const int w = t.w, h = t.h;
	for (Vec mouth : layout.mouths)
	{
		std::vector<int> water;
		const int mx = int(std::floor(mouth.x)), my = int(std::floor(mouth.y));
		for (int dy = -10; dy <= 10; ++dy)
			for (int dx = -10; dx <= 10; ++dx)
			{
				const int x = wrapIndex(mx + dx, w), y = wrapIndex(my + dy, h);
				const size_t i = size_t(y) * w + x;
				if (dx * dx + dy * dy <= 100 && t.water[i] && t.waterSteps[i] == 0)
					water.push_back(int(i));
			}
		for (int clump = 0; clump < scaledCount(3, algaePercent) && !water.empty(); ++clump)
		{
			const int i = water[context.bounded("resources", water.size())];
			placeResourceClump(map, context, MapGeneratorPoint(i % w, i / w), ALGA, 2);
		}
	}
	std::vector<int> shallows;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const size_t i = size_t(y) * w + x;
			if (!t.water[i])
				continue;
			bool shore = false;
			for (int dy = -3; dy <= 3 && !shore; ++dy)
				for (int dx = -3; dx <= 3 && !shore; ++dx)
					shore = !t.water[size_t(wrapIndex(y + dy, h)) * w + wrapIndex(x + dx, w)];
			if (!shore)
				continue;
			const Vec c = layout.frame.toCanonical({x + 0.5, y + 0.5});
			if (c.y < layout.north.at(c.x) - 1 || c.y > layout.south.at(c.x) + 1)
				shallows.push_back(int(i));
		}
	for (int clump = 0; clump < scaledCount(int(shallows.size()) / 60, algaePercent); ++clump)
	{
		const int i = shallows[context.bounded("resources", shallows.size())];
		placeResourceClump(map, context, MapGeneratorPoint(i % w, i / w), ALGA, 1);
	}
}

// Clears the fewest resource tiles that let every colony walk to colony 0: a 0-1 search where
// stepping onto a resource costs one and open ground nothing.
bool connectColonies(Game &game, int teams, std::string &detail)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	const auto workers = workersByTeam(map, teams);
	for (int team = 1; team < teams; ++team)
	{
		std::vector<int> reach = walk(map, workers[0]);
		bool connected = false;
		for (int p : workers[team])
			connected = connected || reach[p] >= 0;
		if (connected)
			continue;
		std::vector<int> cost(size_t(w) * h, std::numeric_limits<int>::max()),
			from(size_t(w) * h, -1);
		std::vector<unsigned char> target(size_t(w) * h, 0);
		for (int p : workers[team])
			target[p] = 1;
		std::deque<int> queue;
		for (int p : workers[0])
		{
			cost[p] = 0;
			queue.push_back(p);
		}
		int reached = -1;
		while (!queue.empty() && reached < 0)
		{
			const int p = queue.front();
			queue.pop_front();
			if (target[p])
			{
				reached = p;
				break;
			}
			const int x = p % w, y = p / w;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int nx = map.normalizeX(x + dx), ny = map.normalizeY(y + dy);
					const int q = ny * w + nx;
					if (map.isWater(nx, ny) || map.getBuilding(nx, ny) != NOGBID)
						continue;
					const int step = map.isResource(nx, ny) ? 1 : 0;
					if (cost[p] + step < cost[q])
					{
						cost[q] = cost[p] + step;
						from[q] = p;
						if (step)
							queue.push_back(q);
						else
							queue.push_front(q);
					}
				}
		}
		if (reached < 0)
		{
			detail = "colony " + std::to_string(team) + " has no land route to colony 0";
			return false;
		}
		for (int p = reached; p >= 0; p = from[p])
			if (map.isResource(p % w, p / w))
				map.setNoResource(p % w, p / w, 1);
	}
	return true;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "watershed layout";
	const WatershedOptions o(context.request);
	Map &map = game.map;
	const int w = map.getW(), h = map.getH(), teams = context.request.nbTeams;
	const Layout layout = planLayout(w, h, o, context);

	context.stage = "watershed terrain";
	const std::vector<unsigned char> terrain = stampTerrain(layout, o, context);
	const auto stampedWater = [&](int x, int y) { return vertexTileWater(terrain, w, h, x, y); };
	context.detail = checkFords(layout, stampedWater);
	if (context.detail.empty())
		context.detail = checkChannels(layout, stampedWater);
	if (!context.detail.empty())
		return false;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			map.setUMTerrain(x, y, TerrainType(terrain[size_t(y) * w + x]));
	map.controlSand();
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (map.getUMTerrain(x, y) != terrain[size_t(y) * w + x])
			{
				context.detail = "Map::controlSand changed the stamped shoreline";
				return false;
			}
	map.rebuildTerrain();

	context.stage = "watershed colonies";
	const Tiles tiles = tileView(terrain, w, h);
	const std::vector<int> regions = bankRegions(layout, tiles);
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<MapGeneratorPoint> sites =
		chooseSites(layout, tiles, regions, fertility, teams, context, context.detail);
	if (int(sites.size()) != teams)
		return false;
	for (int team = 0; team < teams; ++team)
		game.addTeam();
	for (int team = 0; team < teams; ++team)
	{
		std::vector<unsigned char> home(size_t(w) * h, 0);
		for (int dy = -6; dy <= 9; ++dy)
			for (int dx = -6; dx <= 9; ++dx)
			{
				const int x = wrapIndex(sites[team].x + dx, w),
						  y = wrapIndex(sites[team].y + dy, h);
				home[size_t(y) * w + x] = !tiles.water[size_t(y) * w + x];
			}
		if (!placeSettlement(game, context, team, home, sites[team], "starts"))
			return false;
	}

	context.stage = "watershed resources";
	std::vector<unsigned char> keepClear(size_t(w) * h, 0);
	for (const Ford &f : layout.fords)
	{
		const int reach = int(std::ceil(f.span + 5));
		const int cx = int(std::floor(f.center.x)), cy = int(std::floor(f.center.y));
		for (int dy = -reach; dy <= reach; ++dy)
			for (int dx = -reach; dx <= reach; ++dx)
				if (sq(cx + dx + 0.5 - f.center.x) + sq(cy + dy + 0.5 - f.center.y) <=
					sq(f.span + 5))
					keepClear[size_t(wrapIndex(cy + dy, h)) * w + wrapIndex(cx + dx, w)] = 1;
	}
	for (int team = 0; team < teams; ++team)
		for (int dy = -3; dy <= 6; ++dy)
			for (int dx = -3; dx <= 6; ++dx)
				keepClear[size_t(wrapIndex(context.bootY[team] + dy, h)) * w +
						  wrapIndex(context.bootX[team] + dx, w)] = 1;
	Reservations reservations(w, h);
	placeStarterKits(game, context, teams, tiles, fertility, reservations);
	placeFruit(game, context, layout, tiles, fertility, keepClear, reservations, o.fruit);
	placeStone(game, context, tiles, keepClear, reservations, o.stone);
	placeFarmland(game, context, tiles, fertility, keepClear, reservations, o);
	placeAlgae(game, context, layout, tiles, o.algae);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (keepClear[size_t(y) * w + x] && map.isResource(x, y) && !map.isWater(x, y))
				map.setNoResource(x, y, 1);
	context.stage = "watershed connectivity";
	if (!connectColonies(game, teams, context.detail))
		return false;
	// Opening a route can clear part of a starter kit, so the kits are topped up afterwards - held
	// to the kit's own distance rather than the shared defaults, so a colony whose kit could not be
	// placed still gets wheat and wood as near as its rivals do - and the routes checked once more
	// in case a top-up closed one.
	guaranteeStartingResources(game, context, 16, 16);
	return connectColonies(game, teams, context.detail);
}

std::string validate(const GenerationRequest &r)
{
	if (r.nbTeams > (1 << (r.wDec + r.hDec)) / kTilesPerColony)
		return "The watershed has too little floodplain for this many colonies; use a bigger map "
			   "or "
			   "fewer colonies.";
	return "";
}

// Checked on the finished world rather than trusted. Walking treats water, buildings and every
// resource tile as walls (units move, so they don't count): every colony must reach colony 0 and
// stand beside wheat and wood that way. The layout is planned again from the request to find the
// fords and channels: every ford must interrupt a real channel, be open from bank to bank and have
// walkable land at both ends, and every channel must keep its core of open water everywhere else.
std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const Map &map = game.map;
	const int w = map.getW(), h = map.getH(), teams = context.request.nbTeams;
	const auto workers = workersByTeam(map, teams);
	for (int team = 0; team < teams; ++team)
		if (workers[team].empty())
			return "Colony " + std::to_string(team) + " has no workers.";
	std::vector<int> fromFirst;
	for (int team = 0; team < teams; ++team)
	{
		const std::vector<int> reach = walk(map, workers[team]);
		if (team == 0)
			fromFirst = reach;
		bool met = false;
		for (int p : workers[team])
			met = met || fromFirst[p] >= 0;
		if (!met)
			return "Colony " + std::to_string(team) + " cannot walk to colony 0 without swimming.";
		bool wheat = false, wood = false;
		for (int y = 0; y < h && !(wheat && wood); ++y)
			for (int x = 0; x < w; ++x)
			{
				const int type = map.getResource(x, y).type;
				if (type != CORN && type != WOOD)
					continue;
				bool beside = false;
				for (int dy = -1; dy <= 1 && !beside; ++dy)
					for (int dx = -1; dx <= 1 && !beside; ++dx)
						beside =
							reach[size_t(map.normalizeY(y + dy)) * w + map.normalizeX(x + dx)] >= 0;
				(type == CORN ? wheat : wood) = (type == CORN ? wheat : wood) || beside;
			}
		if (!wheat || !wood)
			return "Colony " + std::to_string(team) + " cannot walk to " +
				   (wheat ? "wood." : "wheat.");
	}

	const WatershedOptions o(context.request);
	GenerationContext replay(context.request);
	const Layout layout = planLayout(w, h, o, replay);
	const auto water = [&](int x, int y) { return map.isWater(x, y); };
	std::string error = checkFords(layout, water);
	if (!error.empty())
		return error;
	for (const Ford &f : layout.fords)
		for (int side : {-1, 1})
		{
			const Vec end = f.center + f.across * (side * (f.span + 1.0));
			bool walkable = false;
			for (int dy = -1; dy <= 1 && !walkable; ++dy)
				for (int dx = -1; dx <= 1 && !walkable; ++dx)
				{
					const int x = map.normalizeX(int(std::floor(end.x)) + dx);
					const int y = map.normalizeY(int(std::floor(end.y)) + dy);
					walkable = !map.isWater(x, y) && !map.isResource(x, y) &&
							   map.getBuilding(x, y) == NOGBID;
				}
			if (!walkable)
				return "A ford at (" + std::to_string(map.normalizeX(int(f.center.x))) + ", " +
					   std::to_string(map.normalizeY(int(f.center.y))) +
					   ") has no walkable land on one bank.";
		}
	return checkChannels(layout, water);
}
} // namespace

WatershedOptions::WatershedOptions(const GenerationRequest &r)
	: riverDensity(r.option("river-density")), riverWidth(r.option("river-width")),
	  dryness(r.option("dryness")), fords(r.option("fords")), delta(r.option("river-delta") != 0),
	  meanders(r.option("meanders") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition watershedDefinition()
{
	return {"watershed",
			13,
			"Watershed",
			1,
			false,
			{// Springs per area of land; each one that finds its way to the network is a tributary.
			 {"river-density", "River density", 1, 10, 1, 5, ControlGroup::Terrain},
			 // The narrowest channel, at a spring; rivers widen downstream as tributaries join.
			 {"river-width", "River width", 1, 5, 1, 2, ControlGroup::Terrain},
			 // How near to the rivers the uplands dry out to sand; 0 leaves them grass, and no
			 // setting dries ground within 11 tiles of water, so every river keeps a floodplain.
			 {"dryness", "Dryness", 0, 10, 1, 5, ControlGroup::Terrain},
			 // How often a ford crosses a river: about one per 80 tiles of river at 1, one per 26
			 // at 5, and at least one on every reach long enough to hold one.
			 {"fords", "Fords", 1, 5, 1, 3, ControlGroup::Layout},
			 // Off, the trunk reaches the sea through a single mouth, with no delta lobe.
			 GeneratorControl::toggle("river-delta", "River delta", true, ControlGroup::Terrain),
			 // Off, rivers run without their meanders.
			 GeneratorControl::toggle("meanders", "Meandering rivers", true, ControlGroup::Terrain),
			 // The farmland along the rivers (wheat and wood each), stone outcrops, fruit groves at
			 // the confluences and algae off the mouths and shallows. Every colony's starter kit
			 // stays as it is.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validate,
			validateWorld};
}
