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
// Teeth along contours: logical pixels between tips (dividing every map size,
// so they wrap), and the share of a profile's grit drawn as teeth rather than
// pyramid facets.
constexpr float TeethPeriod = 16.f;
constexpr float TeethShare = .6f;
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
// Logical pixels between the feature points of the coarser grit lattice.
constexpr int JaggedPeriod = 16;
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
	dx = ((b - a) + ((d - c) - (b - a)) * v) * du;
	dy = (bottom - top) * dv;
	return top + (bottom - top) * v;
}

float SmoothBorders::ConeLayer::at(int px, int py) const
{
	if (!spacing)
		return 0;
	const int cx = std::clamp(px / spacing + 1, 1, cells - 2), cy = std::clamp(py / spacing + 1, 1, cells - 2);
	float nearest = 1e30f;
	for (int y = cy - 1; y <= cy + 1; ++y)
		for (int x = cx - 1; x <= cx + 1; ++x)
		{
			const auto &p = points[y * MaxCells + x];
			const auto &axis = axes[y * MaxCells + x];
			const float dx = float(px) - p[0], dy = float(py) - p[1];
			// Distance in the pyramid's own turned square metric.
			const float u = dx * axis[0] + dy * axis[1], v = dy * axis[0] - dx * axis[1];
			nearest = std::min(nearest, std::max(std::abs(u), std::abs(v)));
		}
	// 1 at the tip, falling straight to -1 at 0.6 spacing.
	return 1 - 2 * std::min(1.f, nearest / (.6f * float(spacing)));
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
		int wiggle = 0, square = 0, detail = 0, jagged = 0;
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
				}
			}
		cornerShare[k] = smooth ? 4096 : 0;
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
		auto &field = fields[count++];
		field.material = id;
		const auto &edge = c.profiles[c.materials[id].profile];
		field.feather = float(edge.feather) / 256;
		// Per-material value noise in world space: the same function in every
		// tile, so a pair's contour meanders by the difference of its two fields.
		const std::uint32_t seed = mapSeedSalt(r.seed);
		// Grit: one hashed feature point per cell of two lattices, in world space.
		for (unsigned grit = 0; grit < field.jagged.size(); ++grit)
		{
			auto &cones = field.jagged[grit];
			const int spacing = JaggedPeriod >> grit;
			if (r.width * 32 % spacing || r.height * 32 % spacing)
				continue;
			cones.spacing = spacing * 256;
			cones.cells = 32 / spacing + 2;
			for (int y = 0; y < cones.cells; ++y)
				for (int x = 0; x < cones.cells; ++x)
				{
					const int gx = wrap(r.x * 32 / spacing + x - 1, r.width * 32 / spacing);
					const int gy = wrap(r.y * 32 / spacing + y - 1, r.height * 32 / spacing);
					const auto h = hash(gx, gy, c.materials[id].salt ^ seed ^ (0x51ed270bu + grit * 0x9e3779b9u));
					const float fx = .1f + .8f * float(h & 1023) / 1023, fy = .1f + .8f * float(h >> 10 & 1023) / 1023;
					cones.points[y * ConeLayer::MaxCells + x] = {(float(x - 1) + fx) * cones.spacing,
																  (float(y - 1) + fy) * cones.spacing};
					const float angle = float(h >> 20 & 1023) / 1024 * 1.5707963f; // A square repeats every quarter turn.
					cones.axes[y * ConeLayer::MaxCells + x] = {std::cos(angle), std::sin(angle)};
				}
		}
		// Layers: the two meander octaves, then fine detail.
		for (unsigned octave = 0; octave <= field.noise.size(); ++octave)
		{
			const bool fine = octave == field.noise.size();
			auto &layer = fine ? field.detail : field.noise[octave];
			const int period = fine ? DetailPeriod : edge.wigglePeriod >> octave;
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
			// The finer meander octave carries a third of it; detail is its own layer.
			const float weight = fine ? 1.f : octave ? 1.f / 3 : 2.f / 3;
			for (int y = 0; y <= rows; ++y)
				for (int x = 0; x <= columns; ++x)
				{
					const int gx = wrap((r.x * 32 + phase) / spacing + x, r.width * 32 / spacing);
					const int gy = wrap((r.y * 32 + phase) / spacing + y, r.height * 32 / spacing);
					const auto h = hash(gx, gy, c.materials[id].salt ^ seed ^ (0x7c3b9a15u + octave * 0x9e3779b9u));
					layer.vertices[y * NoiseLayer::Stride + x] = weight * (float(h & 65535) / 32767.5f - 1);
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
		for (const auto &cones : fields[f].jagged)
			noise += jagged * (1 - TeethShare) * .5f * cones.at(px, py);
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
	// Teeth along contours: the pair's border moves by triangle waves along
	// world x and y, blended by the squared components of the contour's
	// direction, so straight edges of any heading break into straight-sided
	// teeth with sharp tips both ways. Waves of plain world coordinates, weighted
	// by slowly turning directions, stay stable; projecting the position onto
	// the direction would not. The shift is antisymmetric in the pair, so
	// weights still sum to one and seams and surf follow the teeth.
	const float teeth = jagged * TeethShare * PixelsPerFieldUnit;
	const float worldX = originX + float(px) / 256, worldY = originY + float(py) / 256;
	const auto distance = [&](unsigned f, unsigned g)
	{
		const float gx = slopeX[f] - slopeX[g], gy = slopeY[f] - slopeY[g];
		const float length = std::max(std::sqrt(gx * gx + gy * gy), 1e-4f);
		float d = (score[f] - score[g]) / length;
		if (teeth > 0)
		{
			const bool lower = fields[f].material < fields[g].material;
			const auto low = lower ? fields[f].material : fields[g].material;
			const auto high = lower ? fields[g].material : fields[f].material;
			const auto h = hash(low, high, seed ^ 0x6a09e667u);
			const auto tri = [](float u) { return 1 - 4 * std::abs(u - std::floor(u) - .5f); };
			// The contour runs across the slope: x-waves for steep contours.
			const float across = gx * gx / (length * length);
			const float wave = (1 - across) * tri(worldX / TeethPeriod + float(h & 1023) / 1024) +
							   across * tri(worldY / TeethPeriod + float(h >> 10 & 1023) / 1024);
			// Only where both fields have risen: a material whose field is still
			// zero has no contour nearby to put teeth on.
			const float risen = std::clamp((std::min(score[f], score[g]) - .02f) / .13f, 0.f, 1.f);
			d += (lower ? 1.f : -1.f) * teeth * wave * risen * risen * (3 - 2 * risen);
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
