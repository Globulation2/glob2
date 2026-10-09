// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainMaterials.h"
#include "map/TerrainPresentation.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <set>
#include <cmath>

namespace TerrainVisual
{
namespace
{
void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(std::string("Terrain tileset: ") + message);
}
int integerInRange(const nlohmann::json &value, int minimum, int maximum)
{
	require(value.is_number_integer() && value >= minimum && value <= maximum,
			"integer field outside supported range");
	return value.get<int>();
}
std::string dataPath(const nlohmann::json &value)
{
	const auto path = value.get<std::string>();
	// Keep authoring paths canonical and platform independent. Check complete
	// segments: a filename such as "ice..cracked" is not parent traversal.
	require(path.starts_with("data/") && path.find_first_of("\\:") == std::string::npos &&
				path.find('\0') == std::string::npos,
			"invalid data-relative path");
	std::size_t start = 0;
	while (start <= path.size())
	{
		const auto end = path.find('/', start);
		const auto segment = path.substr(start, end == std::string::npos ? end : end - start);
		require(!segment.empty() && segment != "." && segment != "..",
				"asset paths must use canonical segments");
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return path;
}
std::array<unsigned char, 3> rgbColor(const nlohmann::json &value)
{
	require(value.is_array() && value.size() == 3, "RGB color requires three channels");
	std::array<unsigned char, 3> result{};
	for (int i = 0; i < 3; ++i)
		result[i] = integerInRange(value[i], 0, 255);
	return result;
}
std::uint32_t keyHash(const std::string &s)
{
	std::uint32_t h = 2166136261u;
	for (unsigned char c : s)
	{
		h ^= c;
		h *= 16777619u;
	}
	return h;
}
int wrap(int x, int size)
{
	x %= size;
	return x < 0 ? x + size : x;
}
} // namespace
std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t salt)
{
	std::uint32_t h = x * 73856093u ^ y * 19349663u ^ salt;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return h;
}
MaterialId Catalog::find(const std::string &key) const
{
	for (unsigned i = 0; i < materials.size(); ++i)
		if (materials[i].key == key)
			return MaterialId(i);
	throw std::runtime_error("Terrain tileset: unknown material " + key);
}
Catalog Catalog::parse(const nlohmann::json &j)
{
	Catalog c;
	c.compiledPack = j.value("compiled_pack", std::string{});
	c.serialized = j.dump();
	if (!c.compiledPack.empty())
	{
		dataPath(c.compiledPack);
		require(c.compiledPack.ends_with("/atlas.json") &&
					std::count(c.compiledPack.begin(), c.compiledPack.end(), '/') >= 3,
				"compiled pack must name atlas.json in its own data subdirectory");
	}
	const int version = integerInRange(j.at("version"), 1, 3);
	c.version = version;
	if (j.contains("boundary_warp_q8"))
	{
		require(version >= 2, "boundary warp requires catalog version 2");
		const auto &warp = j.at("boundary_warp_q8");
		require(warp.is_array() && warp.size() == 3, "boundary warp requires three amplitudes");
		constexpr int limits[] = {1024, 384, 128};
		for (int i = 0; i < 3; ++i)
			c.boundaryWarp[i] = integerInRange(warp[i], 0, limits[i]);
	}
	require(j.at("profiles").is_array() && !j.at("profiles").empty(), "missing boundary profiles");
	std::set<std::string> keys;
	for (const auto &p : j.at("profiles"))
	{
		Profile v{p.at("key").get<std::string>(), integerInRange(p.at("roughness_q8"), 0, 512)};
		v.legacyEdges = version == 1;
		const auto shape = p.value("shape", std::string{"patch"});
		require(shape == "patch" || shape == "contextual", "unknown boundary shape");
		require(version >= 3 || shape == "patch", "contextual shape requires catalog version 3");
		v.contextual = shape == "contextual";
		require(version >= 2 || !p.contains("feather_q8"),
				"boundary feather requires catalog version 2");
		v.feather = integerInRange(p.value("feather_q8", nlohmann::json(256)), 128, 512);
		for (const char *field : {"amplitude_q8", "speckle_q8", "bridge_q8"})
			require(version >= 3 || !p.contains(field),
					"contour amplitude, speckle and bridge require catalog version 3");
		v.amplitude = integerInRange(p.value("amplitude_q8", nlohmann::json(512)), 0, 1024);
		v.speckle = integerInRange(p.value("speckle_q8", nlohmann::json(0)), 0, 1024);
		v.bridge = integerInRange(p.value("bridge_q8", nlohmann::json(0)), 0, 1024);
		v.shearScale = version >= 3 ? 256 : 128;
		require(!v.key.empty() && keys.insert(v.key).second, "duplicate boundary profile");
		const auto &curves = p.at("contours_q12");
		require(curves.is_array() && (version >= 3 ? curves.size() >= 4 && curves.size() <= 64
												   : curves.size() == 4),
				"profile requires four contours, or four to sixty-four in version 3");
		const int limit = version == 1 ? 256 : version == 2 ? 512 : 1024;
		for (const auto &curve : curves)
		{
			const auto count = curve.size();
			require(curve.is_array() &&
						(count == 5 || (version >= 2 && (count == 9 || count == 17 || count == 33))),
					"contour requires 5, 9, 17 or 33 points");
			std::vector<int> points;
			for (const auto &point : curve)
				points.push_back(integerInRange(point, -limit, limit));
			require(points.front() == 0 && points.back() == 0,
					"contours must share zero endpoints");
			v.contours.push_back(std::move(points));
		}
		c.profiles.push_back(std::move(v));
	}
	auto findProfile = [&](const std::string &key)
	{
		for (unsigned i = 0; i < c.profiles.size(); ++i)
			if (c.profiles[i].key == key)
				return i;
		throw std::runtime_error("Terrain tileset: unknown profile " + key);
	};
	keys.clear();
	require(j.at("materials").is_array() && !j.at("materials").empty() &&
				j.at("materials").size() < 65536,
			"invalid material count");
	for (const auto &m : j.at("materials"))
	{
		Material v;
		v.key = m.at("key").get<std::string>();
		v.sprite = dataPath(m.at("sprite"));
		require(!v.key.empty() && keys.insert(v.key).second, "duplicate material key");
		v.salt = keyHash(v.key);
		v.profile = findProfile(m.at("profile"));
		// Water is an ordinary (animated) tile material; the scrolling ocean
		// backdrop and per-material backdrops were retired.
		require(!m.contains("ocean") && !m.contains("backdrop"),
				"ocean and backdrop materials are no longer supported");
		const auto edges = m.value("edges", nlohmann::json("blend"));
		require(edges == "blend" || edges == "periodic", "edges must be 'blend' or 'periodic'");
		v.periodicEdges = edges == "periodic";
		if (m.contains("variant_grid"))
		{
			// A power of two divides every map size, so the block wraps on the torus.
			v.variantGrid = integerInRange(m.at("variant_grid"), 1, 16);
			require((v.variantGrid & (v.variantGrid - 1)) == 0, "variant_grid must be a power of two");
			require(v.periodicEdges, "variant_grid requires periodic edges");
		}
		if (m.contains("decor"))
		{
			const auto &d = m.at("decor");
			require(d.is_object(), "decor must be an object");
			v.decor.sprite = dataPath(d.at("sprite"));
			for (const auto *key : {"full", "edge"})
			{
				auto &frames = std::string(key) == "full" ? v.decor.full : v.decor.edge;
				require(d.at(key).is_array() && !d.at(key).empty() && d.at(key).size() <= 256,
						"decor frames must be a non-empty array");
				for (const auto &frame : d.at(key))
					frames.push_back(integerInRange(frame, 0, 65535));
			}
		}
		v.animationFrames = integerInRange(m.value("animation_frames", nlohmann::json(1)), 1, 256);
		v.animationTicks = integerInRange(m.value("animation_ticks", nlohmann::json(1)), 1,
										  std::numeric_limits<int>::max());
		v.animationStride =
			integerInRange(m.value("animation_stride", nlohmann::json(0)), 0, 65535);
		require(v.animationFrames == 1 || v.animationStride > 0,
				"animated material needs a frame stride");
		v.preview = rgbColor(m.at("preview"));
		v.minimap = rgbColor(m.value("minimap", m.at("preview")));
		if (m.contains("seam"))
		{
			require(version >= 3, "seam treatments require catalog version 3");
			const auto &s = m.at("seam");
			require(s.is_object(), "seam must be an object");
			v.seam.height = integerInRange(s.value("height", nlohmann::json(0)), 0, 255);
			v.seam.cast = integerInRange(s.value("cast_q8", nlohmann::json(0)), 0, 256);
			v.seam.castWidth = integerInRange(s.value("cast_width_q8", nlohmann::json(0)), 0, 2048);
			v.seam.fringe = integerInRange(s.value("fringe_q8", nlohmann::json(0)), 0, 256);
			v.seam.fringeWidth =
				integerInRange(s.value("fringe_width_q8", nlohmann::json(0)), 0, 2048);
			if (s.contains("fringe"))
				v.seam.fringeColor = rgbColor(s.at("fringe"));
		}
		require(m.at("variants").is_array() && !m.at("variants").empty(),
				"material needs variants");
		for (const auto &a : m.at("variants"))
		{
			const auto w = integerInRange(a.at("weight"), 1, 1000000);
			const int frame = integerInRange(a.at("frame"), 0, 65535);
			require(v.totalWeight <= 1000000000 - w, "variant weight sum exceeds limit");
			require(frame + (v.animationFrames - 1) * v.animationStride < 65536,
					"animation exceeds supported texture frame range");
			v.variants.push_back({frame, unsigned(w)});
			v.totalWeight += unsigned(w);
		}
		require(!v.variantGrid || v.variants.size() == std::size_t(v.variantGrid * v.variantGrid),
				"variant_grid needs one variant per block cell");
		c.materials.push_back(std::move(v));
	}
	require(j.at("bindings").is_object(), "bindings must be an object");
	for (const auto &[key, value] : j.at("bindings").items())
		c.bindings.emplace(key, c.find(value));
	// Every paintable built-in terrain needs a material.
	for (unsigned type = 0; type < TERRAIN_COUNT; ++type)
		if (terrainPaintable(TerrainType(type)))
			require(c.bindings.contains(terrainPresentation(TerrainType(type)).name),
					"missing terrain binding");
	std::set<std::pair<MaterialId, MaterialId>> pairs;
	const auto treatments = j.value("pair_treatments", nlohmann::json::array());
	require(treatments.is_array(), "pair treatments must be an array");
	for (const auto &p : treatments)
	{
		auto a = c.find(p.at("a")), b = c.find(p.at("b"));
		if (a > b)
			std::swap(a, b);
		require(a != b && pairs.insert({a, b}).second, "duplicate or self pair treatment");
		c.treatments.push_back({a, b, findProfile(p.at("profile"))});
	}
	return c;
}
unsigned Catalog::profileFor(MaterialId a, MaterialId b) const
{
	auto lo = std::min(a, b), hi = std::max(a, b);
	for (const auto &p : treatments)
		if (p.a == lo && p.b == hi)
			return p.profile;
	const auto &aa = materials[a], &bb = materials[b];
	// Stable under manifest reordering, including equal roughness.
	return profiles[aa.profile].roughness > profiles[bb.profile].roughness ? aa.profile
		   : profiles[aa.profile].roughness < profiles[bb.profile].roughness
			   ? bb.profile
			   : (aa.key < bb.key ? aa.profile : bb.profile);
}
bool Catalog::contextualFor(MaterialId a, MaterialId b) const
{
	// Both surfaces must opt in: an organic default must not round a road,
	// boardwalk or sharp hazard just because it has the greater roughness.
	return a != b && profiles[materials[a].profile].contextual && profiles[materials[b].profile].contextual &&
		profiles[profileFor(a, b)].contextual;
}
std::uint32_t mapSeedSalt(std::uint32_t seed)
{
	// Spread the seed before it meets the material and profile salts, so small
	// seeds do not merely flip low bits of every hash.
	return seed ? hash(seed, 0x7f4a7c15u, 0x9e3779b9u) : 0;
}
unsigned Catalog::variantIndex(MaterialId id, int x, int y, std::uint32_t seed) const
{
	const auto &m = materials[id];
	if (m.variantGrid)
	{
		const unsigned mask = unsigned(m.variantGrid) - 1;
		return (unsigned(y) & mask) * unsigned(m.variantGrid) + (unsigned(x) & mask);
	}
	unsigned n = hash(x, y, m.salt ^ mapSeedSalt(seed)) % m.totalWeight;
	for (unsigned i = 0; i < m.variants.size(); ++i)
	{
		if (n < m.variants[i].weight)
			return i;
		n -= m.variants[i].weight;
	}
	return unsigned(m.variants.size() - 1);
}
int Catalog::decorFrame(MaterialId id, int x, int y, bool edge, std::uint32_t seed) const
{
	const auto &d = materials[id].decor;
	const auto &frames = edge ? d.edge : d.full;
	if (frames.empty())
		return -1;
	// A salt distinct from the ground variant's, so the two choices are independent.
	return frames[hash(x, y, materials[id].salt ^ mapSeedSalt(seed) ^ 0x6dec0u) % frames.size()];
}
int Catalog::frame(MaterialId id, int x, int y, int time, std::uint32_t seed) const
{
	const auto &m = materials[id];
	return m.variants[variantIndex(id, x, y, seed)].frame +
		   (unsigned(time) / m.animationTicks % m.animationFrames) * m.animationStride;
}
PreparedCoverage::Curve::Curve(const Profile *p, unsigned motif) : profile(p)
{
	// Version-1 packs keep their four motifs. Later packs read any curve count
	// and quadruple the variety with mirrored and negated readings of each one.
	const bool varied = !p->legacyEdges;
	const auto &curve = p->contours[varied ? motif % p->contours.size() : motif & 3];
	points = curve.data();
	segments = int(curve.size()) - 1;
	mirror = varied && (motif & 0x100);
	sign = varied && (motif & 0x200) ? -1 : 1;
}
int PreparedCoverage::Curve::wave(int t) const
{
	// Artist-authored piecewise-linear contours; zero at both sample centers.
	// Q12 in/out, with the original rounding.
	if (mirror)
		t = 4096 - t;
	const int scaled = t * segments;
	const int i = std::min(scaled / 4096, segments - 1), f = scaled - i * 4096;
	return sign * (points[i] * (4096 - f) + points[i + 1] * f) / 4096;
}
// Marching-square segments are prepared from the same one-vertex halo on every
// view. Ambiguous diagonals and multi-material junctions retain their old masks.
void PreparedCoverage::prepareBorders(const Catalog &c, const Recipe &r)
{
	if (!r.hasNeighborhood || c.version < 3)
		return;
	const auto a = r.corners[0];
	MaterialId b = a;
	for (auto id : r.corners)
		if (id != a) b = id;
	if (a == b || !c.contextualFor(a, b) ||
		std::any_of(r.corners.begin(), r.corners.end(),
			[a, b](auto id) { return id != a && id != b; }))
		return;
	struct Segment { Point from, to; bool valid = false; };
	std::array<Segment, 9> segments{};
	const auto sub = [](Point a, Point b) { return Point{a.x - b.x, a.y - b.y}; };
	const auto length = [](Point p) { return std::sqrt(p.x * p.x + p.y * p.y); };
	const auto vertex = [&](int x, int y) { return r.neighborhood[(y + 1) * 4 + x + 1]; };
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			const MaterialId ids[] = {vertex(x, y), vertex(x + 1, y),
				vertex(x + 1, y + 1), vertex(x, y + 1)};
			if (std::any_of(std::begin(ids), std::end(ids),
				[a, b](auto id) { return id != a && id != b; })) continue;
			const Point midpoints[] = {{x * 32.f + 16, y * 32.f},
				{x * 32.f + 32, y * 32.f + 16}, {x * 32.f + 16, y * 32.f + 32},
				{x * 32.f, y * 32.f + 16}};
			Point ends[4];
			unsigned count = 0;
			for (int edge = 0; edge < 4; ++edge)
				if (ids[edge] != ids[(edge + 1) % 4]) ends[count++] = midpoints[edge];
			if (count != 2) continue;
			auto &segment = segments[(y + 1) * 3 + x + 1];
			segment = {ends[0], ends[1], true};
		}
	const auto &center = segments[4];
	if (!center.valid) return;
	borderMaterials = {a, b};
	borderBlendScale = 33554432.f / (c.profiles[c.materials[a].profile].feather +
		c.profiles[c.materials[b].profile].feather);
	Point controls[] = {center.from, {}, {}, center.to};
	const float chord = length(sub(center.to, center.from));
	for (int end = 0; end < 2; ++end)
	{
		const Point p = end ? center.to : center.from;
		const Point other = end ? center.from : center.to;
		Point direction = sub(other, p);
		float handle = chord / 3;
		for (int index : {1, 3, 5, 7})
		{
			const auto &neighbor = segments[index];
			if (!neighbor.valid) continue;
			Point continuation;
			if (neighbor.from.x == p.x && neighbor.from.y == p.y) continuation = neighbor.to;
			else if (neighbor.to.x == p.x && neighbor.to.y == p.y) continuation = neighbor.from;
			else continue;
			direction = sub(other, continuation);
			handle = std::min(chord, length(sub(continuation, p))) / 3;
			break;
		}
		const float norm = length(direction);
		Point control{p.x + direction.x * handle / norm, p.y + direction.y * handle / norm};
		// A convex control hull inside the tile prevents loops, crossings and
		// loss of a narrow corner region. Its chord distance bounds the whole curve.
		control.x = std::clamp(control.x, 0.f, 32.f);
		control.y = std::clamp(control.y, 0.f, 32.f);
		const auto delta = sub(center.to, center.from);
		const auto offset = sub(control, p);
		const float distance = std::abs(delta.x * offset.y - delta.y * offset.x) / chord;
		if (distance > 4)
		{
			control.x = p.x + offset.x * 4 / distance;
			control.y = p.y + offset.y * 4 / distance;
		}
		controls[end ? 2 : 1] = control;
	}
	// Monotone projection along the chord rules out self intersections and
	// reversed connections. A failed check falls back to the straight contour.
	const auto chordVector = sub(center.to, center.from);
	const auto projection = [&](Point p)
	{
		const auto offset = sub(p, center.from);
		return offset.x * chordVector.x + offset.y * chordVector.y;
	};
	const bool vertical = std::abs(chordVector.y) > std::abs(chordVector.x);
	const auto along = [&](Point p) { return vertical ? p.y : p.x; };
	const float axisSign = along(center.to) > along(center.from) ? 1.f : -1.f;
	if (projection(controls[1]) < 0 || projection(controls[2]) > chord * chord ||
		projection(controls[1]) > projection(controls[2]) ||
		axisSign * (along(controls[1]) - along(controls[0])) < 0 ||
		axisSign * (along(controls[2]) - along(controls[1])) < 0 ||
		axisSign * (along(controls[3]) - along(controls[2])) < 0)
	{
		controls[1] = {center.from.x + chordVector.x / 3, center.from.y + chordVector.y / 3};
		controls[2] = {center.to.x - chordVector.x / 3, center.to.y - chordVector.y / 3};
	}
	// Orient the curve so material a lies on its left. All a corners of an
	// unambiguous marching square lie on the same side of the straight segment.
	const auto delta = sub(center.to, center.from), offset = sub(Point{}, center.from);
	if (delta.x * offset.y - delta.y * offset.x < 0)
		std::reverse(std::begin(controls), std::end(controls));
	auto &border = naturalBorder;
	// Smooth noise along the contour adds uneven lobes and smaller scallops.
	// Keep the primary coordinate monotone: sideways displacement cannot fold
	// the contour or create detached regions. Salt by the pair and map look,
	// independently of the order in which the materials occur in the cell.
	const auto &profile = c.profiles[c.profileFor(a, b)];
	const auto motif = hash(r.x, r.y, mapSeedSalt(r.seed) ^
		c.materials[std::min(a, b)].salt ^ c.materials[std::max(a, b)].salt ^ 0x53ca110fu);
	const auto scallop = [&](float t, unsigned intervals, unsigned salt)
	{
		const float scaled = t * intervals;
		const unsigned index = std::min(unsigned(scaled), intervals - 1);
		const float f = scaled - index, blend = f * f * (3 - 2 * f);
		const auto value = [&](unsigned i)
		{ return float(hash(i, salt, motif) & 65535) / 32767.5f - 1; };
		return value(index) * (1 - blend) + value(index + 1) * blend;
	};
	const float detail = std::clamp(float(profile.roughness) / 256, 0.f, 1.5f);
	for (unsigned i = 0; i < border.points.size(); ++i)
	{
		const float t = float(i) / (border.points.size() - 1), u = 1 - t;
		auto &p = border.points[i];
		p = {u*u*u*controls[0].x + 3*u*u*t*controls[1].x + 3*u*t*t*controls[2].x + t*t*t*controls[3].x,
			u*u*u*controls[0].y + 3*u*u*t*controls[1].y + 3*u*t*t*controls[2].y + t*t*t*controls[3].y};
		// Zero displacement and slope at the endpoints keep their guided tangent.
		const float taper = std::min(1.f, 64 * t*t*u*u);
		const float window = taper * taper * (3 - 2 * taper);
		const float displacement = .7f * detail * window *
			(9.f * scallop(t, 3, 0) + 6.f * scallop(t, 5, 1) + 1.2f * scallop(t, 9, 2));
		if (vertical) p.x += displacement;
		else p.y += displacement;
		// A ten-pixel curve budget includes both smoothing and detail.
		const float cross = chordVector.x * (p.y - center.from.y) -
			chordVector.y * (p.x - center.from.x);
		const float excess = cross - std::clamp(cross, -10 * chord, 10 * chord);
		if (vertical) p.x += excess / chordVector.y;
		else p.y -= excess / chordVector.x;
		p.x = std::clamp(p.x, 0.f, 32.f);
		p.y = std::clamp(p.y, 0.f, 32.f);
		// Keep a pocket at each corner even when a deep lobe approaches it.
		// The clearance relaxes smoothly towards the cell centre, where the
		// full depth is available. Marching-square endpoints remain untouched.
		const float primary = vertical ? p.y : p.x;
		const float edge = std::min(primary, 32 - primary);
		const float f = std::clamp((edge - 4) / 8, 0.f, 1.f);
		const float clearance = 10 * (1 - f*f*(3 - 2*f));
		if (vertical) p.x = std::clamp(p.x, clearance, 32 - clearance);
		else p.y = std::clamp(p.y, clearance, 32 - clearance);
	}
	const auto first = border.points.front(), last = border.points.back();
	border.vertical = std::abs(last.y - first.y) > std::abs(last.x - first.x);
	border.direction = border.vertical ? (last.y > first.y ? -1.f : 1.f) : (last.x > first.x ? 1.f : -1.f);
	const auto primary = [&](Point p) { return border.vertical ? p.y : p.x; };
	const auto secondary = [&](Point p) { return border.vertical ? p.x : p.y; };
	// Tabulate the monotone contour and its unit normal at logical pixel rows.
	// Runtime samples interpolate this resolution-independent geometry instead
	// of searching the curve segments for every native/HD pixel.
	for (unsigned coordinate = 0; coordinate < border.ordinate.size(); ++coordinate)
	{
		unsigned segment = 1;
		for (unsigned i = 1; i < border.points.size(); ++i)
			if (coordinate >= std::min(primary(border.points[i - 1]), primary(border.points[i])) &&
				coordinate <= std::max(primary(border.points[i - 1]), primary(border.points[i])))
			{ segment = i; break; }
			else if (std::abs(float(coordinate) - primary(border.points[i])) <
				std::abs(float(coordinate) - primary(border.points[segment]))) segment = i;
		const auto p = border.points[segment - 1], q = border.points[segment];
		const float dp = primary(q) - primary(p), ds = secondary(q) - secondary(p);
		const float t = std::clamp((float(coordinate) - primary(p)) / dp, 0.f, 1.f);
		border.ordinate[coordinate] = secondary(p) + t * ds;
		border.normal[coordinate] = std::abs(dp) / std::sqrt(dp * dp + ds * ds);
	}
	hasBorder = true;
}

Coverage PreparedCoverage::at(int px, int py) const
{
	if (!hasBorder) return patchCoverage(px, py);
	// Keep historical crossings and feather weights in a 1px seam band. Blend
	// to the contextual geometry by 4px; this also joins complex junctions.
	const int edgeDistance = std::min({px, py, 8192 - px, 8192 - py});
	if (edgeDistance <= 256) return patchCoverage(px, py);
	int dx = 0, dy = 0;
	for (const auto &layer : warp)
	{
		const auto d = layer.at(px, py);
		dx += d[0]; dy += d[1];
	}
	// Reserve at most two pixels of vector displacement for the world field,
	// leaving ten for curve shaping, including on diagonal boundaries.
	const float warpSquared = float(dx) * dx + float(dy) * dy;
	const float warpScale = warpSquared > 1536 * 1536 ? 512 / std::sqrt(warpSquared) : 1.f / 3;
	const Point sample{float(px) / 256 + dx * warpScale / 256,
		float(py) / 256 + dy * warpScale / 256};
	const auto &border = naturalBorder;
	const float coordinate = std::clamp(border.vertical ? sample.y : sample.x, 0.f, 32.f);
	const unsigned index = std::min(unsigned(coordinate), 31u);
	const float t = coordinate - index;
	const float ordinate = border.ordinate[index] * (1 - t) + border.ordinate[index + 1] * t;
	const float normal = border.normal[index] * (1 - t) + border.normal[index + 1] * t;
	float signedDistance = ((border.vertical ? sample.x : sample.y) - ordinate) * normal * border.direction;
	const float first = border.vertical ? border.points.front().y : border.points.front().x;
	const float last = border.vertical ? border.points.back().y : border.points.back().x;
	const float along = border.vertical ? sample.y : sample.x;
	if (along < std::min(first, last) || along > std::max(first, last))
	{
		// Outside the curve's primary-axis extent the nearest endpoint supplies
		// distance. Extending its normal would smear a nearly vertical tangent
		// across an entire corner region.
		const bool atStart = std::abs(along - first) < std::abs(along - last);
		const auto p = atStart ? border.points.front() : border.points[border.points.size() - 2];
		const auto q = atStart ? border.points[1] : border.points.back();
		const auto end = atStart ? p : q;
		const float cross = (q.x - p.x) * (sample.y - end.y) - (q.y - p.y) * (sample.x - end.x);
		const float squared = (sample.x - end.x) * (sample.x - end.x) +
			(sample.y - end.y) * (sample.y - end.y);
		// All feather and seam treatments end within eight logical pixels.
		signedDistance = (cross < 0 ? -1.f : 1.f) * (squared >= 64 ? 256.f : std::sqrt(squared));
	}
	Coverage result;
	result.material[0] = borderMaterials[0]; result.material[1] = borderMaterials[1];
	result.weight[0] = unsigned(std::clamp(32768.f + signedDistance * borderBlendScale, 0.f, 65536.f));
	result.weight[1] = 65536 - result.weight[0];
	result.neighbor = borderMaterials[result.weight[0] >= result.weight[1] ? 1 : 0];
	result.margin = unsigned(std::min(65535.f, std::abs(signedDistance) * 256));
	if (edgeDistance < 1024)
	{
		const std::array<int, 2> displacement{dx, dy};
		const auto old = patchCoverage(px, py, &displacement);
		unsigned oldA = 0;
		for (unsigned i = 0; i < 4; ++i)
			if (old.material[i] == borderMaterials[0]) oldA += old.weight[i];
		int blend = (edgeDistance - 256) * 4096 / 768;
		blend = int(std::int64_t(blend) * blend * (12288 - 2 * blend) >> 24);
		result.weight[0] = (std::uint64_t(result.weight[0]) * blend + std::uint64_t(oldA) * (4096 - blend)) / 4096;
		result.weight[1] = 65536 - result.weight[0];
		result.margin = (result.margin * blend + old.margin * (4096 - blend)) / 4096;
		result.neighbor = borderMaterials[result.weight[0] >= result.weight[1] ? 1 : 0];
	}
	return result;
}

PreparedCoverage::PreparedCoverage(const Catalog &c, const Recipe &r)
{
	prepareBorders(c, r);
	// A single world-space field bends the complete material partition. Using
	// different fields per material would pull junctions apart. Hash only grid
	// vertices here; native and HD pixels interpolate the same prepared field.
	// Local contours displace samples inside their own patch, so this field is
	// the only displacement that consumes the eight-pixel halo of prepared patches.
	legacy = c.version == 1;
	seed = mapSeedSalt(r.seed);
	constexpr int scales[] = {64, 32, 8};
	for (unsigned i = 0; i < std::size(warp); ++i)
	{
		const int amplitude = c.boundaryWarp[i];
		if (!amplitude)
			continue;
		auto &layer = warp[i];
		const int spacing = std::min({scales[i], r.width * 32, r.height * 32});
		layer.shift = 8;
		for (int s = spacing; s > 1; s >>= 1)
			++layer.shift;
		layer.offsetX = (r.x * 32 % spacing) * 256;
		layer.offsetY = (r.y * 32 % spacing) * 256;
		const int columns = std::min(4, (layer.offsetX / 256 + 32) / spacing + 1);
		const int rows = std::min(4, (layer.offsetY / 256 + 32) / spacing + 1);
		for (int y = 0; y <= rows; ++y)
			for (int x = 0; x <= columns; ++x)
			{
				// The endpoint at the map extent is the same vertex as coordinate 0.
				const int gx = wrap(r.x * 32 / spacing + x, r.width * 32 / spacing);
				const int gy = wrap(r.y * 32 / spacing + y, r.height * 32 / spacing);
				for (unsigned axis = 0; axis < 2; ++axis)
					layer.vertices[y * 5 + x][axis] =
						int(hash(gx, gy,
								 salted(0x61c88647u + i * 0x9e3779b9u + axis * 0x85ebca6bu)) %
							(2 * amplitude + 1)) -
						amplitude;
			}
	}
	for (int sy = 0; sy < 3; ++sy)
		for (int sx = 0; sx < 3; ++sx)
		{
			auto &patch = patches[sy * 3 + sx];
			// Lattice columns sx and sx + 1 take corner columns sx / 2 and
			// (sx + 1) / 2: the outer patches hold one corner column or row.
			const int left = sx / 2, right = (sx + 1) / 2, top = sy / 2, bottom = (sy + 1) / 2;
			const MaterialId ids[] = {r.corners[top * 2 + left], r.corners[top * 2 + right],
									  r.corners[bottom * 2 + left], r.corners[bottom * 2 + right]};
			if (ids[0] == ids[1] && ids[0] == ids[2] && ids[0] == ids[3])
			{
				patch.materials[0] = ids[0];
				patch.count = 1;
				continue;
			}
			const int qx = r.x * 2 + sx - 1, qy = r.y * 2 + sy - 1;
			const auto edge = [&](MaterialId a, MaterialId b, int x, int y, bool vertical)
			{
				if (a == b)
					return Curve{};
				const auto salt =
					c.materials[a].salt ^ c.materials[b].salt ^ (vertical ? 0x46ac23u : 0x973adafu);
				const auto &profile = c.profiles[c.profileFor(a, b)];
				const auto motif =
					hash(wrap(x, r.width * 2), wrap(y, r.height * 2), salted(salt));
				Curve curve{&profile, motif};
				curve.anchor = std::clamp(curve.wave(2048) * profile.roughness / 256,
										  -profile.amplitude, profile.amplitude);
				return curve;
			};
			// Adjoining patches use the same wrapped coordinates and endpoint keys.
			patch.edges = {
				edge(ids[0], ids[1], qx, qy, false), edge(ids[2], ids[3], qx, qy + 1, false),
				edge(ids[0], ids[2], qx, qy, true), edge(ids[1], ids[3], qx + 1, qy, true)};
			// Interpolate material softness from the same corner labels as coverage.
			// A patch-wide maximum would disagree across edges at mixed junctions.
			for (int i = 0; i < 4; ++i)
			{
				const auto &profile = c.profiles[c.materials[ids[i]].profile];
				patch.feather[i] = profile.feather;
				patch.speckle[i] = legacy ? 0 : profile.speckle;
			}
			const auto vertex = hash(wrap(qx, r.width * 2), wrap(qy, r.height * 2),
									 salted(c.materials[ids[0]].salt + c.materials[ids[1]].salt +
											c.materials[ids[2]].salt + c.materials[ids[3]].salt));
			const auto contour =
				[&](MaterialId a, MaterialId b, MaterialId d, MaterialId e, unsigned motif)
			{
				if (a == b && d == e)
					return Curve{};
				unsigned profile = a == b ? c.profileFor(d, e) : c.profileFor(a, b);
				if (d != e)
				{
					const auto other = c.profileFor(d, e);
					if (c.profiles[other].roughness > c.profiles[profile].roughness)
						profile = other;
				}
				return Curve{&c.profiles[profile], motif};
			};
			patch.contours = {contour(ids[0], ids[1], ids[2], ids[3], vertex),
							  contour(ids[0], ids[2], ids[1], ids[3], vertex >> 2)};
			// Join side-connected corners only. Joining diagonal labels produces
			// a broad translucent bridge instead of two distinct regions.
			int component[] = {0, 1, 2, 3};
			for (const auto &pair :
				 {std::pair{0, 1}, std::pair{0, 2}, std::pair{1, 3}, std::pair{2, 3}})
				if (ids[pair.first] == ids[pair.second])
				{
					const int from = component[pair.second], to = component[pair.first];
					for (auto &label : component)
						if (label == from)
							label = to;
				}
			int slots[] = {-1, -1, -1, -1};
			for (int i = 0; i < 4; ++i)
			{
				int &slot = slots[component[i]];
				if (slot < 0)
				{
					slot = patch.count++;
					patch.materials[slot] = ids[i];
				}
				patch.slots[i] = slot;
			}
			if (legacy)
				continue;
			// Two lobes of one material meeting across the center join through a
			// narrow neck, as in the original diagonal tiles, instead of a hard cross.
			if (patch.count == 4)
			{
				const bool choice = (vertex >> 4) & 1;
				if (ids[0] == ids[3] && choice)
					patch.bridge = {int(patch.slots[0]), int(patch.slots[3])};
				else if (ids[1] == ids[2] && !choice)
					patch.bridge = {int(patch.slots[1]), int(patch.slots[2])};
				if (patch.bridge[0] >= 0)
					patch.bridgeScale = patch.contours[0].profile->bridge * 3 / 2;
			}
			if (std::any_of(patch.speckle.begin(), patch.speckle.end(), [](unsigned s) { return s; }))
				for (unsigned slot = 0; slot < patch.count; ++slot)
					patch.pebbles[slot] = pebbleField(c, r, patch.materials[slot]);
		}
}
int PreparedCoverage::pebbleField(const Catalog &c, const Recipe &r, MaterialId id)
{
	for (unsigned i = 0; i < pebbles.size(); ++i)
		if (pebbles[i].material == id)
			return int(i);
	PebbleField field;
	field.material = id;
	const int columns = r.width * 8, rows = r.height * 8;
	for (int cy = -1; cy <= 8; ++cy)
		for (int cx = -1; cx <= 8; ++cx)
		{
			const int gx = wrap(r.x * 8 + cx, columns), gy = wrap(r.y * 8 + cy, rows);
			const auto h = hash(gx, gy, salted(c.materials[id].salt ^ 0x2545f491u));
			if (h & 0x80)
				continue; // Half of the cells carry a pebble.
			const auto detail = hash(gx, gy, salted(c.materials[id].salt ^ 0x9e3779b9u));
			auto &pebble = field.cells[(cy + 1) * 10 + cx + 1];
			pebble.x = cx * 1024 + int((h >> 8) & 1023);
			pebble.y = cy * 1024 + int((h >> 18) & 1023);
			pebble.radius = 320 + int(detail & 255) * 3 / 2; // 1.25 to 2.75 pixels
			pebble.strength = 128 + int((detail >> 8) & 127);
		}
	pebbles.push_back(field);
	return int(pebbles.size() - 1);
}
int PreparedCoverage::PebbleField::at(int px, int py) const
{
	const int cx = px >> 10, cy = py >> 10;
	int best = 0;
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int ix = cx + dx + 1, iy = cy + dy + 1;
			if (ix < 0 || ix >= 10 || iy < 0 || iy >= 10)
				continue;
			const auto &pebble = cells[iy * 10 + ix];
			if (!pebble.strength)
				continue;
			const std::int64_t ox = px - pebble.x, oy = py - pebble.y;
			const std::int64_t squared = ox * ox + oy * oy,
							   radius = std::int64_t(pebble.radius) * pebble.radius;
			if (squared >= radius)
				continue;
			best = std::max(best, int((4096 - squared * 4096 / radius) * pebble.strength >> 8));
		}
	return best;
}
std::array<int, 2> PreparedCoverage::WarpLayer::at(int px, int py) const
{
	if (!shift)
		return {};
	const int x = px + offsetX, y = py + offsetY;
	// Clamp the interval, not the coordinate: the last vertex is sampled as
	// t=1 on the preceding interval, including diagnostic samples at tile edges.
	const int ix = std::min(x >> shift, 3), iy = std::min(y >> shift, 3);
	int u = ((x - (ix << shift)) << 12) >> shift;
	int v = ((y - (iy << shift)) << 12) >> shift;
	if (shift > 11)
	{
		const auto smooth = [](int t) { return int(std::int64_t(t) * t * (12288 - 2 * t) >> 24); };
		u = smooth(u);
		v = smooth(v);
	}
	const auto lerp = [](int a, int b, int t) { return a + (b - a) * t / 4096; };
	std::array<int, 2> displacement{};
	for (unsigned axis = 0; axis < 2; ++axis)
		displacement[axis] = lerp(
			lerp(vertices[iy * 5 + ix][axis], vertices[iy * 5 + ix + 1][axis], u),
			lerp(vertices[(iy + 1) * 5 + ix][axis], vertices[(iy + 1) * 5 + ix + 1][axis], u), v);
	return displacement;
}
Coverage PreparedCoverage::patchCoverage(int px, int py, const std::array<int, 2> *displacement) const
{
	// Sum the layers at the original coordinate; composing them sequentially
	// would amplify their slopes and their maximum displacement. The catalog
	// limits their sum to six pixels; local contours share the remaining halo.
	const int originalX = px, originalY = py;
	if (displacement)
	{
		px += (*displacement)[0]; py += (*displacement)[1];
	}
	else for (const auto &layer : warp)
	{
		const auto displacement = layer.at(originalX, originalY);
		px += displacement[0];
		py += displacement[1];
	}
	const int sx = (px + 2048) / 4096, sy = (py + 2048) / 4096;
	int u = (px + 2048) % 4096, v = (py + 2048) % 4096;
	const auto &patch = patches[sy * 3 + sx];
	Coverage out;
	out.material = patch.materials;
	out.neighbor = patch.materials[0];
	if (patch.count == 1)
	{
		out.weight[0] = 65536;
		return out;
	}
	const auto edge = [](const Curve &curve, int t)
	{
		if (!curve.profile)
			return 0;
		// A shared edge has one crossing. Sampling a jagged curve along that
		// edge can fold it back on itself, producing detached slivers. Anchor the
		// crossing and taper to the vertices; put the detail in interior shears.
		if (!curve.profile->legacyEdges)
			return curve.anchor * std::min(t, 4096 - t) / 2048;
		return std::clamp(curve.wave(t) * curve.profile->roughness / 256,
						  -curve.profile->amplitude, curve.profile->amplitude);
	};
	const int du = (edge(patch.edges[0], u) * (4096 - v) + edge(patch.edges[1], u) * v) / 4096;
	const int dv = (edge(patch.edges[2], v) * (4096 - u) + edge(patch.edges[3], v) * u) / 4096;
	u = std::clamp(u + du, 0, 4096);
	v = std::clamp(v + dv, 0, 4096);
	// Bounded interior shears vanish on every patch edge. Version-1 packs taper
	// them linearly from the edges; later packs hold the full displacement over
	// the central half of the patch so authored pixel amplitudes survive. That
	// window stays fold-free up to the four-pixel amplitude limit.
	const auto contour = [](const Curve &curve, int t)
	{
		return curve.profile ? std::clamp(curve.wave(t) * curve.profile->roughness /
											  curve.profile->shearScale,
										  -curve.profile->amplitude, curve.profile->amplitude)
							 : 0;
	};
	const auto window = [this](int t)
	{ return legacy ? 2 * std::min(t, 4096 - t) : std::min(4096, 4 * std::min(t, 4096 - t)); };
	u += contour(patch.contours[0], v) * window(u) / 4096;
	v += contour(patch.contours[1], u) * window(v) / 4096;
	u = std::clamp(u, 0, 4096);
	v = std::clamp(v, 0, 4096);
	if (!legacy)
	{
		// Rounder lobes: a smoothstep keeps edge midpoints and shared-edge
		// weights while single-corner regions approach quarter discs.
		const auto smooth = [](int t) { return int(std::int64_t(t) * t * (12288 - 2 * t) >> 24); };
		u = smooth(u);
		v = smooth(v);
	}
	const unsigned weights[] = {unsigned((4096 - u) * (4096 - v)), unsigned(u * (4096 - v)),
								unsigned((4096 - u) * v), unsigned(u * v)};
	std::array<unsigned, 4> scores{};
	for (int i = 0; i < 4; ++i)
		scores[patch.slots[i]] += weights[i];
	std::uint64_t featherSum = 0, speckleSum = 0;
	for (int i = 0; i < 4; ++i)
	{
		featherSum += std::uint64_t(weights[i]) * patch.feather[i];
		speckleSum += std::uint64_t(weights[i]) * patch.speckle[i];
	}
	const unsigned feather = unsigned(featherSum >> 12);
	if (patch.bridge[0] >= 0)
	{
		const int tent = (4096 - std::abs(2 * u - 4096)) * (4096 - std::abs(2 * v - 4096)) >> 12;
		scores[patch.bridge[0]] += unsigned(tent * patch.bridgeScale);
		scores[patch.bridge[1]] += unsigned(tent * patch.bridgeScale);
	}
	// Pebbles of a material raise its score near an edge: inside its own region
	// nothing changes, across the edge a speck or a bite appears where the
	// pebble outweighs the distance to the boundary. Interior samples skip them.
	if (const unsigned speckle = unsigned(speckleSum >> 24))
	{
		unsigned first = 0, second = 0;
		for (unsigned i = 0; i < patch.count; ++i)
			if (scores[i] > first)
			{
				second = first;
				first = scores[i];
			}
			else
				second = std::max(second, scores[i]);
		if (first - second < speckle * 12288 + feather)
			for (unsigned i = 0; i < patch.count; ++i)
				if (patch.pebbles[i] >= 0)
				{
					const auto shape = pebbles[patch.pebbles[i]].at(originalX, originalY);
					scores[i] += (speckle * unsigned(shape) >> 12) * 12288;
				}
	}
	const unsigned maximum = *std::max_element(scores.begin(), scores.end());
	// Along a straight edge the score gap grows by 12288 per Q8 pixel, so it
	// doubles as a cheap distance estimate to the nearest other material.
	{
		unsigned top = 0, next = patch.count;
		for (unsigned i = 1; i < patch.count; ++i)
			if (scores[i] > scores[top])
				top = i;
		for (unsigned i = 0; i < patch.count; ++i)
			if (patch.materials[i] != patch.materials[top] &&
				(next == patch.count || scores[i] > scores[next]))
				next = i;
		if (next < patch.count)
		{
			out.neighbor = patch.materials[next];
			out.margin = std::min(65535u, (scores[top] - scores[next]) / 12288);
		}
		else
			out.neighbor = patch.materials[top];
	}
	unsigned total = 0;
	for (unsigned i = 0; i < patch.count; ++i)
	{
		out.weight[i] = scores[i] + feather > maximum ? scores[i] + feather - maximum : 0;
		total += out.weight[i];
	}
	unsigned sum = 0;
	unsigned largest = 0;
	for (unsigned i = 0; i < patch.count; ++i)
	{
		if (scores[i] > scores[largest])
			largest = i;
		out.weight[i] = std::uint64_t(out.weight[i]) * 65536 / total;
		sum += out.weight[i];
	}
	out.weight[largest] += 65536 - sum;
	return out;
}
Coverage coverage(const Catalog &c, const Recipe &r, int px, int py)
{
	return PreparedCoverage(c, r).at(px, py);
}
} // namespace TerrainVisual
