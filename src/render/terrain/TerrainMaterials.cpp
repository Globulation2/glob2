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
int integer(const nlohmann::json &value, int minimum, int maximum)
{
	require(value.is_number_integer() && value >= minimum && value <= maximum,
			"integer field outside supported range");
	return value.get<int>();
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
	require(c.compiledPack.empty() || (c.compiledPack.starts_with("data/") &&
									   c.compiledPack.find("..") == std::string::npos),
			"invalid compiled pack path");
	require(j.at("version") == 1, "unsupported version");
	require(j.at("profiles").is_array() && !j.at("profiles").empty(), "missing boundary profiles");
	std::set<std::string> keys;
	for (const auto &p : j.at("profiles"))
	{
		Profile v{p.at("key").get<std::string>(), integer(p.at("roughness_q8"), 0, 512)};
		require(!v.key.empty() && keys.insert(v.key).second, "duplicate boundary profile");
		require(v.roughness >= 0 && v.roughness <= 512, "roughness outside 0..512");
		const auto &curves = p.at("contours_q12");
		require(curves.is_array() && curves.size() == 4, "profile requires four contours");
		for (int i = 0; i < 4; ++i)
		{
			require(curves[i].is_array() && curves[i].size() == 5, "contour requires five points");
			for (int k = 0; k < 5; ++k)
			{
				v.contours[i][k] = integer(curves[i][k], -256, 256);
				require(std::abs(v.contours[i][k]) <= 256, "contour displacement too large");
			}
			require(v.contours[i][0] == 0 && v.contours[i][4] == 0,
					"contours must share zero endpoints");
		}
		c.profiles.push_back(v);
	}
	auto profile = [&](const std::string &key)
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
		v.sprite = m.at("sprite").get<std::string>();
		require(!v.key.empty() && keys.insert(v.key).second, "duplicate material key");
		require(v.sprite.starts_with("data/") && v.sprite.find("..") == std::string::npos,
				"invalid sprite path");
		v.salt = keyHash(v.key);
		v.profile = profile(m.at("profile"));
		v.ocean = m.value("ocean", false);
		if (m.contains("backdrop"))
		{
			const auto &b = m.at("backdrop");
			v.backdrop = {
				b.at("sprite").get<std::string>(),
				integer(b.value("first_frame", nlohmann::json(0)), 0, 65535),
				integer(b.value("frames", nlohmann::json(1)), 1, 256),
				integer(b.value("ticks", nlohmann::json(1)), 1, std::numeric_limits<int>::max())};
			require(!v.ocean && v.backdrop.sprite.starts_with("data/") &&
						v.backdrop.sprite.find("..") == std::string::npos,
					"invalid backdrop sprite");
			require(v.backdrop.firstFrame >= 0 && v.backdrop.frames > 0 &&
						v.backdrop.frames <= 256 && v.backdrop.ticks > 0 &&
						std::int64_t(v.backdrop.firstFrame) + v.backdrop.frames <= 65536,
					"invalid backdrop animation");
		}
		v.animationFrames = integer(m.value("animation_frames", nlohmann::json(1)), 1, 256);
		v.animationTicks = integer(m.value("animation_ticks", nlohmann::json(1)), 1,
								   std::numeric_limits<int>::max());
		v.animationStride = integer(m.value("animation_stride", nlohmann::json(0)), 0, 65535);
		require(v.animationFrames > 0 && v.animationFrames <= 256 && v.animationTicks > 0 &&
					v.animationStride >= 0 && v.animationStride <= 65535,
				"invalid animation");
		require(v.animationFrames == 1 || v.animationStride > 0,
				"animated material needs a frame stride");
		const auto &rgb = m.at("preview");
		require(rgb.is_array() && rgb.size() == 3, "invalid preview color");
		for (int i = 0; i < 3; ++i)
		{
			int n = integer(rgb[i], 0, 255);
			require(n >= 0 && n <= 255, "invalid preview channel");
			v.preview[i] = n;
		}
		require(m.at("variants").is_array() && !m.at("variants").empty(),
				"material needs variants");
		for (const auto &a : m.at("variants"))
		{
			const auto w = integer(a.at("weight"), 1, 1000000);
			const int frame = integer(a.at("frame"), 0, 65535);
			require(w > 0 && w <= 1000000 && v.totalWeight <= 1000000000 - w,
					"invalid variant weight");
			require(frame >= 0 && std::int64_t(frame) +
										  std::int64_t(v.animationFrames - 1) * v.animationStride <
									  65536,
					"invalid texture frame");
			v.variants.push_back({frame, unsigned(w)});
			v.totalWeight += unsigned(w);
		}
		c.materials.push_back(std::move(v));
	}
	const char *names[] = {"water", "sand", "grass", "ice", "road"};
	for (const auto &[key, value] : j.at("bindings").items())
		c.bindings.emplace(key, c.find(value));
	for (const auto *name : names)
		require(c.bindings.contains(name), "missing terrain binding");
	require(c.materials[c.bindings.at("water")].ocean, "water binding must use ocean backdrop");
	std::set<std::pair<MaterialId, MaterialId>> pairs;
	for (const auto &p : j.value("pair_treatments", nlohmann::json::array()))
	{
		auto a = c.find(p.at("a")), b = c.find(p.at("b"));
		if (a > b)
			std::swap(a, b);
		require(a != b && pairs.insert({a, b}).second, "duplicate or self pair treatment");
		c.treatments.push_back({a, b, profile(p.at("profile"))});
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
int Catalog::frame(MaterialId id, int x, int y, int time) const
{
	const auto &m = materials[id];
	unsigned n = hash(x, y, m.salt) % m.totalWeight;
	for (const auto &v : m.variants)
	{
		if (n < v.weight)
			return v.frame +
				   (unsigned(time) / m.animationTicks % m.animationFrames) * m.animationStride;
		n -= v.weight;
	}
	return m.variants.back().frame;
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
Coverage coverage(const Catalog &c, const Recipe &r, int px, int py)
{
	const int sx = (px + 2048) / 4096, sy = (py + 2048) / 4096;
	int u = (px + 2048) % 4096, v = (py + 2048) % 4096;
	const MaterialId ids[] = {r.samples[sy * 4 + sx], r.samples[sy * 4 + sx + 1],
							  r.samples[(sy + 1) * 4 + sx], r.samples[(sy + 1) * 4 + sx + 1]};
	Coverage out;
	if (ids[0] == ids[1] && ids[0] == ids[2] && ids[0] == ids[3])
	{
		out.material[0] = ids[0];
		out.weight[0] = 65536;
		return out;
	}
	const int qx = r.x * 2 + sx - 1, qy = r.y * 2 + sy - 1;
	auto edge = [&](MaterialId a, MaterialId b, int x, int y, bool vertical, int t)
	{
		if (a == b)
			return 0;
		const auto salt =
			c.materials[a].salt ^ c.materials[b].salt ^ (vertical ? 0x46ac23u : 0x973adafu);
		const auto motif = hash(wrap(x, r.width * 2), wrap(y, r.height * 2), salt);
		const auto &profile = c.profiles[c.profileFor(a, b)];
		return wave(profile, t, motif) * profile.roughness / 256;
	};
	// Both patches adjoining an edge use identical coordinates, endpoints and motif.
	const int du = (edge(ids[0], ids[1], qx, qy, false, u) * (4096 - v) +
					edge(ids[2], ids[3], qx, qy + 1, false, u) * v) /
				   4096;
	const int dv = (edge(ids[0], ids[2], qx, qy, true, v) * (4096 - u) +
					edge(ids[1], ids[3], qx + 1, qy, true, v) * u) /
				   4096;
	u = std::clamp(u + du, 0, 4096);
	v = std::clamp(v + dv, 0, 4096);
	// Edge intersections alone give slightly tilted straight borders. Add a
	// bounded contour inside the patch, vanishing on all four sides. Sequential
	// monotone shears preserve connectivity while making the silhouette jagged.
	const auto vertex = hash(wrap(qx, r.width * 2), wrap(qy, r.height * 2),
							 c.materials[ids[0]].salt + c.materials[ids[1]].salt +
								 c.materials[ids[2]].salt + c.materials[ids[3]].salt);
	auto contour =
		[&](MaterialId a, MaterialId b, MaterialId d, MaterialId e, int t, unsigned motif)
	{
		if (a == b && d == e)
			return 0;
		unsigned profile = a == b ? c.profileFor(d, e) : c.profileFor(a, b);
		if (d != e)
		{
			const auto other = c.profileFor(d, e);
			if (c.profiles[other].roughness > c.profiles[profile].roughness)
				profile = other;
		}
		const auto &p = c.profiles[profile];
		return std::clamp(wave(p, t, motif) * p.roughness / 128, -512, 512);
	};
	u += contour(ids[0], ids[1], ids[2], ids[3], v, vertex) * std::min(u, 4096 - u) / 2048;
	v += contour(ids[0], ids[2], ids[1], ids[3], u, vertex >> 2) * std::min(v, 4096 - v) / 2048;
	const unsigned weights[] = {unsigned((4096 - u) * (4096 - v)), unsigned(u * (4096 - v)),
								unsigned((4096 - u) * v), unsigned(u * v)};
	// Join only side-connected corners. Summing disconnected diagonal corners
	// creates a saddle with a broad translucent bridge at its center.
	int component[] = {0, 1, 2, 3};
	for (const auto &edge : {std::pair{0, 1}, std::pair{0, 2}, std::pair{1, 3}, std::pair{2, 3}})
		if (ids[edge.first] == ids[edge.second])
		{
			const int from = component[edge.second], to = component[edge.first];
			for (auto &label : component)
				if (label == from)
					label = to;
		}
	std::array<unsigned, 4> scores{};
	int count = 0;
	int slots[] = {-1, -1, -1, -1};
	for (int i = 0; i < 4; ++i)
	{
		int &slot = slots[component[i]];
		if (slot < 0)
		{
			slot = count++;
			out.material[slot] = ids[i];
		}
		scores[slot] += weights[i];
	}
	const unsigned maximum = *std::max_element(scores.begin(), scores.end());
	unsigned total = 0;
	constexpr unsigned feather = 1u << 20;
	for (int i = 0; i < count; ++i)
	{
		out.weight[i] = scores[i] + feather > maximum ? scores[i] + feather - maximum : 0;
		total += out.weight[i];
	}
	unsigned sum = 0;
	int largest = 0;
	for (int i = 0; i < count; ++i)
	{
		if (scores[i] > scores[largest])
			largest = i;
		out.weight[i] = std::uint64_t(out.weight[i]) * 65536 / total;
		sum += out.weight[i];
	}
	out.weight[largest] += 65536 - sum;
	return out;
}
} // namespace TerrainVisual
