// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainMaterials.h"
#include <algorithm>
#include <cmath>

namespace TerrainVisual
{
namespace
{
int wrap(int x, int size)
{
	x %= size;
	return x < 0 ? x + size : x;
}
float smoothstep(float t) { return t * t * (3 - 2 * t); }
// Squareness remaps a cell coordinate toward a step at the cell's midline:
// a blend of t and smoothstep applied three times. Both ends keep their value
// and share one slope, so fields stay continuous with matching slopes across
// cell edges while contours hug the midlines and corners tighten.
float squared(float t, float k, float &slope)
{
	float s = t, derivative = 1;
	for (int i = 0; i < 3; ++i)
	{
		derivative *= 6 * s * (1 - s);
		s = smoothstep(s);
	}
	slope = 1 + k * (derivative - 1);
	return t + k * (s - t);
}
// Catmull-Rom basis for the vertices at -1, 0, 1 and 2 and its derivative per
// vertex spacing, at t in [0, 1] between vertices 0 and 1.
void catmullRom(float t, float weight[4], float slope[4])
{
	const float t2 = t * t, t3 = t2 * t;
	weight[0] = .5f * (-t3 + 2 * t2 - t);
	weight[1] = .5f * (3 * t3 - 5 * t2 + 2);
	weight[2] = .5f * (-3 * t3 + 4 * t2 + t);
	weight[3] = .5f * (t3 - t2);
	slope[0] = .5f * (-3 * t2 + 4 * t - 1);
	slope[1] = .5f * (9 * t2 - 10 * t);
	slope[2] = .5f * (-9 * t2 + 8 * t + 1);
	slope[3] = .5f * (3 * t2 - 2 * t);
}
// A unit difference between two fields across a straight contour moves it by
// this many pixels: the Catmull-Rom step between two vertex rows has slope 1.25
// per 32 pixels, and the difference of two complementary fields doubles it.
constexpr float PixelsPerFieldUnit = 32.f / 2.5f;
// The strongest multiplicative noise keeps every field positive where its
// vertices reach, so meanders never cut a material off its own vertex.
constexpr float MaximumWiggle = .75f;
// Score below which a field is too far from its contour for seams and surf.
constexpr float MinimumBandScore = .05f;
// Boost of the summed fractal grit before it is clipped to its bound.
constexpr float GritContrast = 3.5f;
// Logical pixels between pebble cells (dividing every map size, so pebbles wrap).
constexpr float PebbleCell = 4.f;
// Pebbles push a border out by this multiple of a profile's speckle reach.
constexpr float PebbleReach = 2.f;
// Combined meander, detail and grit of one field, in field units: as much as
// keeps every field positive where its vertices reach (MaximumWiggle). Halo
// fields keep seams and surf continuous at any displacement within it.
constexpr float MaximumDisplacement = MaximumWiggle;
// Squareness of a vertex on a 45-degree staircase. A slight negative value
// pulls the spline towards its vertices just enough that the contour through a
// staircase of vertices runs straight, within a pixel, instead of stepping.
constexpr float DiagonalSquareness = -.2f;
// Logical pixels between the lattice points of the fine detail octave.
constexpr int DetailPeriod = 16;
// Whether the vertex's 3x3 neighbourhood holds exactly two materials split by
// a 45-degree line: the vertices on either side of one of the lines
// dx - dy = t + 1/2 or dx + dy = t + 1/2 are uniform. Both a staircase's edge
// vertices and the vertices one step off it match, so the whole staircase
// straightens, while a right-angled corner vertex does not.
template <typename Vertex>
bool onDiagonal(const Vertex &vertex, int cx, int cy)
{
	for (const int sign : {1, -1})
		for (int t = -2; t <= 1; ++t)
		{
			int side[2] = {-1, -1};
			bool split = true;
			for (int dy = -1; dy <= 1 && split; ++dy)
				for (int dx = -1; dx <= 1 && split; ++dx)
				{
					int &material = side[dx - sign * dy > t];
					const int other = vertex(cx + dx, cy + dy);
					split = material < 0 || material == other;
					material = other;
				}
			if (split && side[0] != side[1])
				return true;
		}
	return false;
}
} // namespace

float SmoothBorders::NoiseLayer::at(int px, int py, float &dx, float &dy) const
{
	dx = dy = 0;
	if (!shift)
		return 0;
	const int x = px + offsetX, y = py + offsetY;
	const int ix = std::min(x >> shift, Stride - 2), iy = std::min(y >> shift, Stride - 2);
	const float span = float(1 << shift);
	const float tu = std::clamp(float(x - (ix << shift)) / span, 0.f, 1.f);
	const float tv = std::clamp(float(y - (iy << shift)) / span, 0.f, 1.f);
	const float u = linear ? tu : smoothstep(tu), v = linear ? tv : smoothstep(tv);
	const float du = (linear ? 1 : 6 * tu * (1 - tu)) / span, dv = (linear ? 1 : 6 * tv * (1 - tv)) / span;
	const float a = vertices[iy * Stride + ix], b = vertices[iy * Stride + ix + 1];
	const float c = vertices[(iy + 1) * Stride + ix], d = vertices[(iy + 1) * Stride + ix + 1];
	const float top = a + (b - a) * u, bottom = c + (d - c) * u;
	const float value = top + (bottom - top) * v;
	if (ridged)
		return weight * (1 - 2 * std::abs(value)); // Its slope is never used.
	dx = weight * ((b - a) + ((d - c) - (b - a)) * v) * du;
	dy = weight * (bottom - top) * dv;
	return weight * value;
}

float SmoothBorders::pebble(unsigned field, int px, int py) const
{
	// Half the cells of a 4px world grid hold a pebble of 1.25 to 2.75 px
	// radius at a hashed offset; edges feather over three quarters of a pixel.
	const float x = originX + float(px) / 256, y = originY + float(py) / 256;
	const int cx = int(std::floor(x / PebbleCell)), cy = int(std::floor(y / PebbleCell));
	const int columns = width * 32 / int(PebbleCell), rows = height * 32 / int(PebbleCell);
	float share = 0;
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
		{
			const auto h = hash(wrap(cx + dx, columns), wrap(cy + dy, rows), pebbleSalt[field] ^ seed);
			if (h & 1u)
				continue;
			const float ox = (float(cx + dx) + .2f + .6f * float(h >> 1 & 255) / 255) * PebbleCell;
			const float oy = (float(cy + dy) + .2f + .6f * float(h >> 9 & 255) / 255) * PebbleCell;
			const float radius = 1.25f + 1.5f * float(h >> 17 & 255) / 255;
			const float d = std::sqrt((x - ox) * (x - ox) + (y - oy) * (y - oy));
			share = std::max(share, std::clamp((radius - d) / .75f, 0.f, 1.f));
		}
	return share;
}

void SmoothBorders::prepare(const Catalog &c, const Recipe &r)
{
	if (!r.hasNeighborhood || c.version < 4)
		return;
	const auto &corners = r.corners;
	if (!r.haloCount && std::all_of(corners.begin(), corners.end(), [&](auto id) { return id == corners[0]; }))
		return;
	// Version 4 draws every contact smoothly; profiles only set parameters.
	const bool everyContact = c.version >= 4;
	originX = float(r.x * 32);
	originY = float(r.y * 32);
	seed = mapSeedSalt(r.seed);
	width = r.width;
	height = r.height;
	// Shares and wiggle are decided per vertex from its eight neighbours, all of
	// which lie in the 4x4 neighbourhood, so adjacent tiles agree on every
	// shared corner.
	const auto vertex = [&](int x, int y) { return r.neighborhood[(y + 1) * 4 + x + 1]; };
	bool all = true;
	for (unsigned k = 0; k < 4; ++k)
	{
		const int cx = int(k & 1), cy = int(k >> 1);
		const auto own = vertex(cx, cy);
		bool smooth = true;
		int wiggle = 0, square = 0, detail = 0, jagged = 0, speckle = 0;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const auto other = vertex(cx + dx, cy + dy);
				if (other == own)
					continue;
				const auto &profile = c.profiles[c.profileFor(own, other)];
				if (!everyContact && profile.shape != BoundaryShape::Smooth)
					smooth = false;
				else
				{
					wiggle = std::max(wiggle, profile.wiggle);
					square = std::max(square, profile.squareness);
					detail = std::max(detail, profile.detail);
					jagged = std::max(jagged, profile.jagged);
					speckle = std::max(speckle, profile.speckle);
				}
			}
		cornerShare[k] = smooth ? 4096 : 0;
		cornerSpeckle[k] = float(speckle) / 256;
		cornerWiggle[k] = std::min(MaximumWiggle, float(wiggle) / 256 / PixelsPerFieldUnit);
		// Detail shares the bound that keeps every field positive at its vertices.
		cornerDetail[k] = std::min(MaximumWiggle - cornerWiggle[k], float(detail) / 256 / PixelsPerFieldUnit);
		cornerJagged[k] = std::min(MaximumWiggle - cornerWiggle[k] - cornerDetail[k],
								   float(jagged) / 256 / PixelsPerFieldUnit);
		// A contour lies about half a cell from the next uniform cell, which
		// draws no seam or surf bands, and bands reach eight pixels: together the
		// noises of one field stay within MaximumDisplacement, scaled down alike.
		if (const float total = cornerWiggle[k] + cornerDetail[k] + cornerJagged[k];
			total > MaximumDisplacement)
		{
			const float scale = MaximumDisplacement / total;
			cornerWiggle[k] *= scale;
			cornerDetail[k] *= scale;
			cornerJagged[k] *= scale;
		}
		// Square profiles keep right angles but run straight along diagonals;
		// round profiles are left as they are.
		cornerSquareness[k] = square && smooth && onDiagonal(vertex, cx, cy) ? DiagonalSquareness
																			 : float(square) / 256;
		anyShare |= smooth;
		all &= smooth;
	}
	fullShare = all;
	if (!anyShare)
		return;
	// Only the tile's own corner materials may contribute to it; halo vertices
	// of other materials shape the slopes through their zero fields. A contour
	// between halo materials lies over half a cell beyond the tile edge, outside
	// the reach of every seam and surf band.
	owner.fill(-1);
	std::array<MaterialId, 4 + Recipe::MaxHalo> members{};
	unsigned memberCount = 0;
	for (const auto id : corners) members[memberCount++] = id;
	for (unsigned i = 0; i < r.haloCount; ++i) members[memberCount++] = r.halo[i];
	for (unsigned m = 0; m < memberCount; ++m)
	{
		const auto id = members[m];
		if (m == 4)
			weighted = count; // Halo fields follow the corner fields.
		unsigned index = 0;
		while (index < count && fields[index].material != id)
			++index;
		if (index < count)
			continue;
		pebbleSalt[count] = c.materials[id].salt ^ 0x3c6ef372u;
		auto &field = fields[count++];
		field.material = id;
		const auto &edge = c.profiles[c.materials[id].profile];
		field.feather = float(edge.feather) / 256;
		// Per-material value noise in world space: the same function in every
		// tile, so a pair's contour meanders by the difference of its two fields.
		const std::uint32_t seed = mapSeedSalt(r.seed);
		// Layers: the two meander octaves, fine detail, then fractal grit.
		struct Plan { NoiseLayer *layer; int period; float weight; bool ridged; };
		// Grit keeps most of its energy down to the finest octaves, as natural
		// edges do: crunchy at every scale. The finest two are ridged.
		const float gritTotal = 1 + .7f + .8f + .7f;
		const Plan plans[] = {
			{&field.noise[0], edge.wigglePeriod, 2.f / 3, false},
			{&field.noise[1], edge.wigglePeriod >> 1, 1.f / 3, false},
			{&field.detail, DetailPeriod, 1.f, false},
			{&field.grit[0], 32, 1 / gritTotal, false},
			{&field.grit[1], 16, .7f / gritTotal, false},
			{&field.grit[2], 8, .8f / gritTotal, true},
			{&field.grit[3], 4, .7f / gritTotal, true},
		};
		for (unsigned octave = 0; octave < std::size(plans); ++octave)
		{
			auto &layer = *plans[octave].layer;
			layer.weight = plans[octave].weight;
			layer.ridged = plans[octave].ridged;
			const int period = plans[octave].period;
			const int spacing = std::min({period, r.width * 32, r.height * 32});
			if (spacing < 4)
				continue;
			layer.shift = 8;
			for (int s = spacing; s > 1; s >>= 1)
				++layer.shift;
			const int phase = 0;
			layer.offsetX = ((r.x * 32 + phase) % spacing) * 256;
			layer.offsetY = ((r.y * 32 + phase) % spacing) * 256;
			const int columns = std::min(NoiseLayer::Stride - 1, (layer.offsetX / 256 + 32) / spacing + 1);
			const int rows = std::min(NoiseLayer::Stride - 1, (layer.offsetY / 256 + 32) / spacing + 1);
			for (int y = 0; y <= rows; ++y)
				for (int x = 0; x <= columns; ++x)
				{
					const int gx = wrap((r.x * 32 + phase) / spacing + x, r.width * 32 / spacing);
					const int gy = wrap((r.y * 32 + phase) / spacing + y, r.height * 32 / spacing);
					const auto h = hash(gx, gy, c.materials[id].salt ^ seed ^ (0x7c3b9a15u + octave * 0x9e3779b9u));
					layer.vertices[y * NoiseLayer::Stride + x] = float(h & 65535) / 32767.5f - 1;
				}
		}
	}
	if (memberCount <= 4)
		weighted = count;
	for (unsigned f = 0; f < count; ++f)
		for (unsigned g = 0; g < count; ++g)
			if (f != g && c.bandsBetween(fields[f].material, fields[g].material))
				bands[f] |= std::uint8_t(1u << g);
	for (unsigned i = 0; i < owner.size(); ++i)
		for (unsigned f = 0; f < count; ++f)
			if (fields[f].material == r.neighborhood[i])
				owner[i] = int(f);
	// A diagonal pair of one material meets the other diagonal at a saddle. The
	// dominant material joins its lobes through a neck there, so roads and
	// rivers continue diagonally; equal materials take a seeded choice. The bias
	// vanishes on every tile edge, so neighbouring tiles are unaffected.
	if (weighted == 2 && corners[0] == corners[3] && corners[1] == corners[2])
	{
		const auto a = corners[0], b = corners[1];
		bool joinA = c.dominates(a, b);
		if (!joinA && !c.dominates(b, a))
			joinA = hash(wrap(r.x, r.width), wrap(r.y, r.height), mapSeedSalt(r.seed) ^ 0x2d1b5a3fu) & 1;
		const auto &profile = c.profiles[c.profileFor(a, b)];
		fields[joinA ? 0 : 1].bias = float(profile.bridge) / 256 / PixelsPerFieldUnit;
	}
}

int SmoothBorders::share(int px, int py) const
{
	if (fullShare)
		return 4096;
	if (!anyShare)
		return 0;
	const float u = smoothstep(std::clamp(float(px) / 8192, 0.f, 1.f));
	const float v = smoothstep(std::clamp(float(py) / 8192, 0.f, 1.f));
	const float top = cornerShare[0] + (cornerShare[1] - cornerShare[0]) * u;
	const float bottom = cornerShare[2] + (cornerShare[3] - cornerShare[2]) * u;
	return int(top + (bottom - top) * v + .5f);
}

float SmoothBorders::bilinear(const std::array<float, 4> &corner, float u, float v)
{
	const float top = corner[0] + (corner[1] - corner[0]) * u;
	const float bottom = corner[2] + (corner[3] - corner[2]) * u;
	return top + (bottom - top) * v;
}

Coverage SmoothBorders::at(int px, int py) const
{
	const float u = std::clamp(float(px) / 8192, 0.f, 1.f);
	const float v = std::clamp(float(py) / 8192, 0.f, 1.f);
	float wx[4], dwx[4], wy[4], dwy[4];
	// Squareness varies continuously between corners, so neighbouring cells
	// remap their shared edge identically.
	const float square = bilinear(cornerSquareness, u, v);
	float slopeU = 1, slopeV = 1;
	catmullRom(squared(u, square, slopeU), wx, dwx);
	catmullRom(squared(v, square, slopeV), wy, dwy);
	for (int i = 0; i < 4; ++i)
	{
		dwx[i] *= slopeU;
		dwy[i] *= slopeV;
	}
	std::array<float, 6> value{}, slopeX{}, slopeY{};
	for (int j = 0; j < 4; ++j)
		for (int i = 0; i < 4; ++i)
			if (const int f = owner[j * 4 + i]; f >= 0)
			{
				value[f] += wx[i] * wy[j];
				slopeX[f] += dwx[i] * wy[j];
				slopeY[f] += wx[i] * dwy[j];
			}
	const float wiggle = bilinear(cornerWiggle, u, v);
	const float detail = bilinear(cornerDetail, u, v);
	const float jagged = bilinear(cornerJagged, u, v);
	const float saddle = 16 * u * (1 - u) * v * (1 - v);
	std::array<float, 6> score{};
	for (unsigned f = 0; f < count; ++f)
	{
		// Noise per Q8 pixel; only the broad meander's slope enters the distance.
		float noise = 0, noiseX = 0, noiseY = 0, gx, gy;
		for (const auto &layer : fields[f].noise)
		{
			noise += wiggle * layer.at(px, py, gx, gy);
			noiseX += wiggle * gx;
			noiseY += wiggle * gy;
		}
		noise += detail * fields[f].detail.at(px, py, gx, gy);
		// Summed octaves rarely approach their bound; boosted and clipped to it,
		// typical edges break up several pixels while the bound still holds.
		float grit = 0;
		for (const auto &layer : fields[f].grit)
			grit += layer.at(px, py, gx, gy);
		noise += jagged * std::clamp(GritContrast * grit, -1.f, 1.f);
		const float scale = 1 + noise;
		score[f] = value[f] * scale + fields[f].bias * saddle;
		// Per pixel. Fine detail and grit move the contour through the score, but
		// their steep slopes would make a first-order distance claim every
		// contour within a few pixels: extrapolating a 4px zigzag over the eight
		// pixels of a band. Distances use the smooth slope, so bands follow the
		// teeth near a contour and stay put away from one.
		slopeX[f] = slopeX[f] * scale / 32 + value[f] * noiseX * 256;
		slopeY[f] = slopeY[f] * scale / 32 + value[f] * noiseY * 256;
	}
	// Signed pixel distance from field f to field g, from a first-order
	// expansion of their score difference.
	// Pebbles: each material's grains push its border out by the profile's
	// reach, so specks of either material lie detached across the other's
	// edge. Antisymmetric in the pair, so weights still sum to one and seams and
	// surf ring the grains; only where both fields have risen.
	const float reach = PebbleReach * bilinear(cornerSpeckle, u, v);
	std::array<float, 6> grains{};
	if (reach > 0)
		for (unsigned f = 0; f < count; ++f)
			grains[f] = pebble(f, px, py);
	const auto distance = [&](unsigned f, unsigned g)
	{
		const float gx = slopeX[f] - slopeX[g], gy = slopeY[f] - slopeY[g];
		float d = (score[f] - score[g]) / std::max(std::sqrt(gx * gx + gy * gy), 1e-4f);
		if (reach > 0 && grains[f] != grains[g])
		{
			const float risen = std::clamp((std::min(score[f], score[g]) - .02f) / .13f, 0.f, 1.f);
			d += reach * (grains[f] - grains[g]) * risen * risen * (3 - 2 * risen);
		}
		return d;
	};
	// Each material's distance to its strongest competitor ramps its weight
	// over the two materials' mean feather. Two materials therefore sum to one
	// exactly, and three meet with equal shares at their junction.
	// Coverage comes from the corner fields alone, exactly as without a halo.
	Coverage out;
	std::array<float, 4> raw{};
	float total = 0;
	unsigned top = 0;
	for (unsigned f = 1; f < weighted; ++f)
		if (score[f] > score[top])
			top = f;
	for (unsigned f = 0; f < weighted; ++f)
	{
		unsigned rival = f == 0 ? 1 : 0;
		for (unsigned g = 0; g < weighted; ++g)
			if (g != f && score[g] > score[rival])
				rival = g;
		const float ramp = (fields[f].feather + fields[rival].feather) / 2;
		// One corner material owns the whole cell, whatever lies beyond it.
		raw[f] = weighted == 1 ? 1.f : std::clamp(.5f + distance(f, rival) / ramp, 0.f, 1.f);
		total += raw[f];
	}
	unsigned sum = 0;
	for (unsigned f = 0; f < weighted; ++f)
	{
		out.material[f] = fields[f].material;
		out.weight[f] = unsigned(raw[f] / total * 65536);
		sum += out.weight[f];
	}
	out.weight[top] += 65536 - sum;
	// Seams and surf take the nearest contour of a material they draw a band
	// against, halo materials beyond the tile edge included. Contours without a
	// band neither hide one behind them nor depend on which tile measures.
	float nearest = 256;
	out.neighbor = fields[top == 0 ? 1 : 0].material;
	for (unsigned g = 0; g < count; ++g)
		// A field that has not risen here has no contour within band reach, and a
		// first-order distance to it would be meaningless.
		if (g != top && (bands[top] >> g & 1u) && score[g] > MinimumBandScore)
			if (const float d = std::max(0.f, distance(top, g)); d < nearest)
			{
				nearest = d;
				out.neighbor = fields[g].material;
			}
	out.margin = unsigned(std::min(65535.f, nearest * 256));
	return out;
}
} // namespace TerrainVisual
