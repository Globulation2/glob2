#include "TerrainMaterials.h"
#include <algorithm>
namespace TerrainVisual { namespace {
int wrap(int x,int size) { x%=size; return x<0?x+size:x; }
int wave(const Profile &p,int t,unsigned motif) { int i=std::min(t/1024,3),f=t-i*1024; return (p.contours[motif&3][i]*(1024-f)+p.contours[motif&3][i+1]*f)/1024; }
}
Coverage referenceCoverage(const Catalog &c, const Recipe &r, int px, int py)
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
}
