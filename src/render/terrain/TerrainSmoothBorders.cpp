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
} // namespace

float SmoothBorders::NoiseLayer::at(int px, int py) const
{
	if (!shift)
		return 0;
	const int x = px + offsetX, y = py + offsetY;
	const int ix = std::min(x >> shift, 3), iy = std::min(y >> shift, 3);
	const float u = smoothstep(float(x - (ix << shift)) / float(1 << shift));
	const float v = smoothstep(float(y - (iy << shift)) / float(1 << shift));
	const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
	return lerp(lerp(vertices[iy * 5 + ix], vertices[iy * 5 + ix + 1], u),
				lerp(vertices[(iy + 1) * 5 + ix], vertices[(iy + 1) * 5 + ix + 1], u), v);
}

void SmoothBorders::prepare(const Catalog &c, const Recipe &r)
{
	if (!r.hasNeighborhood || c.version < 4)
		return;
	const auto &corners = r.corners;
	if (std::all_of(corners.begin(), corners.end(), [&](auto id) { return id == corners[0]; }))
		return;
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
		int wiggle = 0, square = 0;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const auto other = vertex(cx + dx, cy + dy);
				if (other == own)
					continue;
				const auto &profile = c.profiles[c.profileFor(own, other)];
				if (profile.shape != BoundaryShape::Smooth)
					smooth = false;
				else
				{
					wiggle = std::max(wiggle, profile.wiggle);
					square = std::max(square, profile.squareness);
				}
			}
		cornerShare[k] = smooth ? 4096 : 0;
		cornerWiggle[k] = std::min(MaximumWiggle, float(wiggle) / 256 / PixelsPerFieldUnit);
		cornerSquareness[k] = float(square) / 256;
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
	for (const auto id : corners)
	{
		unsigned index = 0;
		while (index < count && fields[index].material != id)
			++index;
		if (index < count)
			continue;
		auto &field = fields[count++];
		field.material = id;
		const auto &edge = c.profiles[c.materials[id].profile];
		field.feather = float(edge.feather) / 256;
		// Per-material value noise in world space: the same function in every
		// tile, so a pair's contour meanders by the difference of its two fields.
		const std::uint32_t seed = mapSeedSalt(r.seed);
		for (unsigned octave = 0; octave < field.noise.size(); ++octave)
		{
			auto &layer = field.noise[octave];
			const int spacing = std::min({edge.wigglePeriod >> octave, r.width * 32, r.height * 32});
			if (spacing < 8)
				continue;
			layer.shift = 8;
			for (int s = spacing; s > 1; s >>= 1)
				++layer.shift;
			layer.offsetX = (r.x * 32 % spacing) * 256;
			layer.offsetY = (r.y * 32 % spacing) * 256;
			const int columns = std::min(4, (layer.offsetX / 256 + 32) / spacing + 1);
			const int rows = std::min(4, (layer.offsetY / 256 + 32) / spacing + 1);
			// The finer octave carries a third of the meander.
			const float weight = octave ? 1.f / 3 : 2.f / 3;
			for (int y = 0; y <= rows; ++y)
				for (int x = 0; x <= columns; ++x)
				{
					const int gx = wrap(r.x * 32 / spacing + x, r.width * 32 / spacing);
					const int gy = wrap(r.y * 32 / spacing + y, r.height * 32 / spacing);
					const auto h = hash(gx, gy, c.materials[id].salt ^ seed ^ (0x7c3b9a15u + octave * 0x9e3779b9u));
					layer.vertices[y * 5 + x] = weight * (float(h & 65535) / 32767.5f - 1);
				}
		}
	}
	for (unsigned i = 0; i < owner.size(); ++i)
		for (unsigned f = 0; f < count; ++f)
			if (fields[f].material == r.neighborhood[i])
				owner[i] = int(f);
	// A diagonal pair of one material meets the other diagonal at a saddle. The
	// dominant material joins its lobes through a neck there, so roads and
	// rivers continue diagonally; equal materials take a seeded choice. The bias
	// vanishes on every tile edge, so neighbouring tiles are unaffected.
	if (count == 2 && corners[0] == corners[3] && corners[1] == corners[2])
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
	std::array<float, 4> value{}, slopeX{}, slopeY{};
	for (int j = 0; j < 4; ++j)
		for (int i = 0; i < 4; ++i)
			if (const int f = owner[j * 4 + i]; f >= 0)
			{
				value[f] += wx[i] * wy[j];
				slopeX[f] += dwx[i] * wy[j];
				slopeY[f] += wx[i] * dwy[j];
			}
	const float wiggle = bilinear(cornerWiggle, u, v);
	const float saddle = 16 * u * (1 - u) * v * (1 - v);
	std::array<float, 4> score{};
	for (unsigned f = 0; f < count; ++f)
	{
		float noise = 0;
		for (const auto &layer : fields[f].noise)
			noise += layer.at(px, py);
		const float scale = 1 + wiggle * noise;
		score[f] = value[f] * scale + fields[f].bias * saddle;
		// Per pixel; the slow noise contributes little slope of its own.
		slopeX[f] *= scale / 32;
		slopeY[f] *= scale / 32;
	}
	// Signed pixel distance from field f to field g, from a first-order
	// expansion of their score difference.
	const auto distance = [&](unsigned f, unsigned g)
	{
		const float gx = slopeX[f] - slopeX[g], gy = slopeY[f] - slopeY[g];
		return (score[f] - score[g]) / std::max(std::sqrt(gx * gx + gy * gy), 1e-4f);
	};
	// Each material's distance to its strongest competitor ramps its weight
	// over the two materials' mean feather. Two materials therefore sum to one
	// exactly, and three meet with equal shares at their junction.
	Coverage out;
	std::array<float, 4> raw{};
	float total = 0;
	unsigned top = 0;
	for (unsigned f = 1; f < count; ++f)
		if (score[f] > score[top])
			top = f;
	for (unsigned f = 0; f < count; ++f)
	{
		unsigned rival = f == 0 ? 1 : 0;
		for (unsigned g = 0; g < count; ++g)
			if (g != f && score[g] > score[rival])
				rival = g;
		const float ramp = (fields[f].feather + fields[rival].feather) / 2;
		raw[f] = std::clamp(.5f + distance(f, rival) / ramp, 0.f, 1.f);
		total += raw[f];
	}
	unsigned sum = 0;
	for (unsigned f = 0; f < count; ++f)
	{
		out.material[f] = fields[f].material;
		out.weight[f] = unsigned(raw[f] / total * 65536);
		sum += out.weight[f];
	}
	out.weight[top] += 65536 - sum;
	// Seams and surf take the nearest contour of another material.
	float nearest = 256;
	out.neighbor = fields[top == 0 ? 1 : 0].material;
	for (unsigned g = 0; g < count; ++g)
		if (g != top && fields[g].material != fields[top].material)
			if (const float d = std::max(0.f, distance(top, g)); d < nearest)
			{
				nearest = d;
				out.neighbor = fields[g].material;
			}
	out.margin = unsigned(std::min(65535.f, nearest * 256));
	return out;
}
} // namespace TerrainVisual
