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
		const auto ocean = m.value("ocean", nlohmann::json(false));
		require(ocean.is_boolean(), "ocean must be a boolean");
		v.ocean = ocean.get<bool>();
		if (m.contains("backdrop"))
		{
			const auto &b = m.at("backdrop");
			v.backdrop = {dataPath(b.at("sprite")),
						  integerInRange(b.value("first_frame", nlohmann::json(0)), 0, 65535),
						  integerInRange(b.value("frames", nlohmann::json(1)), 1, 256),
						  integerInRange(b.value("ticks", nlohmann::json(1)), 1,
										 std::numeric_limits<int>::max())};
			require(!v.ocean, "ocean material cannot have a separate backdrop");
			require(v.backdrop.firstFrame + v.backdrop.frames <= 65536,
					"backdrop animation exceeds supported frame range");
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
		c.materials.push_back(std::move(v));
	}
	require(j.at("bindings").is_object(), "bindings must be an object");
	for (const auto &[key, value] : j.at("bindings").items())
		c.bindings.emplace(key, c.find(value));
	// Every paintable built-in terrain needs a material; legacy shores resolve
	// through their corner materials.
	for (unsigned type = 0; type < TERRAIN_COUNT; ++type)
		if (terrainPaintable(TerrainType(type)))
			require(c.bindings.contains(terrainPresentation(TerrainType(type)).name),
					"missing terrain binding");
	require(c.materials[c.bindings.at("water")].ocean, "water binding must use ocean backdrop");
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
std::uint32_t mapSeedSalt(std::uint32_t seed)
{
	// Spread the seed before it meets the material and profile salts, so small
	// seeds do not merely flip low bits of every hash.
	return seed ? hash(seed, 0x7f4a7c15u, 0x9e3779b9u) : 0;
}
unsigned Catalog::variantIndex(MaterialId id, int x, int y, std::uint32_t seed) const
{
	const auto &m = materials[id];
	unsigned n = hash(x, y, m.salt ^ mapSeedSalt(seed)) % m.totalWeight;
	for (unsigned i = 0; i < m.variants.size(); ++i)
	{
		if (n < m.variants[i].weight)
			return i;
		n -= m.variants[i].weight;
	}
	return unsigned(m.variants.size() - 1);
}
int Catalog::frame(MaterialId id, int x, int y, int time, std::uint32_t seed) const
{
	const auto &m = materials[id];
	return m.variants[variantIndex(id, x, y, seed)].frame +
		   (unsigned(time) / m.animationTicks % m.animationFrames) * m.animationStride;
}
std::array<unsigned, 4> legacyCorners(unsigned frame)
{
	if (frame < 16)
		return {2, 2, 2, 2};
	if (frame >= 256)
		return {0, 0, 0, 0};
	if (frame >= 128 && frame < 144)
		return {1, 1, 1, 1};
	// Bits TL/TR/BL/BR of the higher material in each eight-frame shore group.
	constexpr unsigned masks[] = {8, 4, 1, 2, 3, 12, 5, 10, 7, 11, 14, 13, 6, 9};
	const bool shore = frame >= 144;
	const unsigned group = (frame - (shore ? 144 : 16)) / 8;
	// The two diagonal groups in the water shoreline atlas are reversed
	// relative to grass/sand (Map::lookup rows 240 and 248).
	const unsigned mask = masks[group] ^ ((shore && group >= 12) ? 15u : 0u);
	std::array<unsigned, 4> result{};
	for (int i = 0; i < 4; ++i)
		result[i] = (mask & (1u << i)) ? (shore ? 1 : 2) : (shore ? 0 : 1);
	return result;
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
PreparedCoverage::PreparedCoverage(const Catalog &c, const Recipe &r)
{
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
			const MaterialId ids[] = {r.samples[sy * 4 + sx], r.samples[sy * 4 + sx + 1],
									  r.samples[(sy + 1) * 4 + sx],
									  r.samples[(sy + 1) * 4 + sx + 1]};
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
Coverage PreparedCoverage::at(int px, int py) const
{
	// Sum the layers at the original coordinate; composing them sequentially
	// would amplify their slopes and their maximum displacement. The catalog
	// limits their sum to six pixels; local contours share the remaining halo.
	const int originalX = px, originalY = py;
	for (const auto &layer : warp)
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
