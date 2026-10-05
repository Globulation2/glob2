// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainMaterials.h"
#include <nlohmann/json.hpp>
#include <algorithm>
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
// Four piecewise-linear contours; zero at both sample centers. Q12 in/out.
int wave(const Profile &profile, int t, unsigned motif)
{
	const auto &points = profile.contours;
	const int i = std::min(t / 1024, 3), f = t - i * 1024;
	return (points[motif & 3][i] * (1024 - f) + points[motif & 3][i + 1] * f) / 1024;
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
	integerInRange(j.at("version"), 1, 1);
	require(j.at("profiles").is_array() && !j.at("profiles").empty(), "missing boundary profiles");
	std::set<std::string> keys;
	for (const auto &p : j.at("profiles"))
	{
		Profile v{p.at("key").get<std::string>(), integerInRange(p.at("roughness_q8"), 0, 512)};
		require(!v.key.empty() && keys.insert(v.key).second, "duplicate boundary profile");
		const auto &curves = p.at("contours_q12");
		require(curves.is_array() && curves.size() == 4, "profile requires four contours");
		for (int i = 0; i < 4; ++i)
		{
			require(curves[i].is_array() && curves[i].size() == 5, "contour requires five points");
			for (int k = 0; k < 5; ++k)
				v.contours[i][k] = integerInRange(curves[i][k], -256, 256);
			require(v.contours[i][0] == 0 && v.contours[i][4] == 0,
					"contours must share zero endpoints");
		}
		c.profiles.push_back(v);
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
	const char *names[] = {"water", "sand", "grass", "ice", "road"};
	require(j.at("bindings").is_object(), "bindings must be an object");
	for (const auto &[key, value] : j.at("bindings").items())
		c.bindings.emplace(key, c.find(value));
	for (const auto *name : names)
		require(c.bindings.contains(name), "missing terrain binding");
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
unsigned Catalog::variantIndex(MaterialId id, int x, int y) const
{
	const auto &m = materials[id];
	unsigned n = hash(x, y, m.salt) % m.totalWeight;
	for (unsigned i = 0; i < m.variants.size(); ++i)
	{
		if (n < m.variants[i].weight)
			return i;
		n -= m.variants[i].weight;
	}
	return unsigned(m.variants.size() - 1);
}
int Catalog::frame(MaterialId id, int x, int y, int time) const
{
	const auto &m = materials[id];
	return m.variants[variantIndex(id, x, y)].frame +
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
PreparedCoverage::PreparedCoverage(const Catalog &c, const Recipe &r)
{
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
				return Curve{&c.profiles[c.profileFor(a, b)],
							 hash(wrap(x, r.width * 2), wrap(y, r.height * 2), salt)};
			};
			// Adjoining patches use the same wrapped coordinates and endpoint keys.
			patch.edges = {
				edge(ids[0], ids[1], qx, qy, false), edge(ids[2], ids[3], qx, qy + 1, false),
				edge(ids[0], ids[2], qx, qy, true), edge(ids[1], ids[3], qx + 1, qy, true)};
			const auto vertex = hash(wrap(qx, r.width * 2), wrap(qy, r.height * 2),
									 c.materials[ids[0]].salt + c.materials[ids[1]].salt +
										 c.materials[ids[2]].salt + c.materials[ids[3]].salt);
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
		}
}
Coverage PreparedCoverage::at(int px, int py) const
{
	const int sx = (px + 2048) / 4096, sy = (py + 2048) / 4096;
	int u = (px + 2048) % 4096, v = (py + 2048) % 4096;
	const auto &patch = patches[sy * 3 + sx];
	Coverage out;
	out.material = patch.materials;
	if (patch.count == 1)
	{
		out.weight[0] = 65536;
		return out;
	}
	const auto edge = [](const Curve &curve, int t)
	{
		return curve.profile ? wave(*curve.profile, t, curve.motif) * curve.profile->roughness / 256
							 : 0;
	};
	const int du = (edge(patch.edges[0], u) * (4096 - v) + edge(patch.edges[1], u) * v) / 4096;
	const int dv = (edge(patch.edges[2], v) * (4096 - u) + edge(patch.edges[3], v) * u) / 4096;
	u = std::clamp(u + du, 0, 4096);
	v = std::clamp(v + dv, 0, 4096);
	// Bounded interior shears vanish on every patch edge. Their sequential
	// evaluation preserves connectivity and the original integer rounding.
	const auto contour = [](const Curve &curve, int t)
	{
		return curve.profile ? std::clamp(wave(*curve.profile, t, curve.motif) *
											  curve.profile->roughness / 128,
										  -512, 512)
							 : 0;
	};
	u += contour(patch.contours[0], v) * std::min(u, 4096 - u) / 2048;
	v += contour(patch.contours[1], u) * std::min(v, 4096 - v) / 2048;
	const unsigned weights[] = {unsigned((4096 - u) * (4096 - v)), unsigned(u * (4096 - v)),
								unsigned((4096 - u) * v), unsigned(u * v)};
	std::array<unsigned, 4> scores{};
	for (int i = 0; i < 4; ++i)
		scores[patch.slots[i]] += weights[i];
	const unsigned maximum = *std::max_element(scores.begin(), scores.end());
	unsigned total = 0;
	constexpr unsigned feather = 1u << 20;
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
