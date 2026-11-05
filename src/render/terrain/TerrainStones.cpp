// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainStones.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <stdexcept>

namespace TerrainVisual
{
StoneMap::StoneMap(const std::uint8_t *rgba, int size, int pitch) : size(size)
{
	if (size < TileTexels || size % TileTexels || size / TileTexels > 16 ||
		((size / TileTexels) & (size / TileTexels - 1)))
		throw std::runtime_error("A stone map must span a power-of-two block of 128-texel cells");
	tileCount = size / TileTexels;
	texels.resize(std::size_t(size) * std::size_t(size));
	seconds.resize(texels.size());
	// Each stone's centre is the mean of its texels, measured from the cell
	// that holds it; stones never reach more than one cell beyond it.
	struct Sum
	{
		std::int64_t x = 0, y = 0, n = 0;
	};
	std::vector<std::vector<Sum>> sums(std::size_t(tileCount) * std::size_t(tileCount));
	tileReach.assign(sums.size(), {});
	for (int y = 0; y < size; ++y)
	{
		const auto *row = rgba + std::size_t(y) * std::size_t(pitch);
		for (int x = 0; x < size; ++x)
		{
			const auto *t = row + 4 * x;
			if (t[0] >= MaxStonesPerTile || t[2] >= MaxStonesPerTile || (t[1] >> 4) > 8 || (t[1] & 15) > 8)
				throw std::runtime_error("Invalid stone map texel");
			const auto *u = row + 4 * (size + x);
			if (u[0] >= MaxStonesPerTile || (u[1] & 15) > 8)
				throw std::runtime_error("Invalid stone map texel");
			texels[std::size_t(y) * std::size_t(size) + std::size_t(x)] =
				std::uint32_t(t[0]) | std::uint32_t(t[1]) << 8 | std::uint32_t(t[2]) << 16 | std::uint32_t(t[3]) << 24;
			seconds[std::size_t(y) * std::size_t(size) + std::size_t(x)] =
				std::uint32_t(u[0]) | std::uint32_t(u[1] & 15) << 8 | std::uint32_t(u[2]) << 16 | std::uint32_t(u[3]) << 24;
			const int code = t[1] >> 4, dx = code % 3 - 1, dy = code / 3 - 1;
			const int tx = x / TileTexels, ty = y / TileTexels;
			tileReach[std::size_t(ty) * std::size_t(tileCount) + std::size_t(tx)][code] |= std::uint64_t(1) << t[0];
			const int hx = (tx + dx + tileCount) % tileCount, hy = (ty + dy + tileCount) % tileCount;
			auto &stones = sums[std::size_t(hy) * std::size_t(tileCount) + std::size_t(hx)];
			if (stones.size() <= t[0])
				stones.resize(t[0] + 1u);
			auto &sum = stones[t[0]];
			sum.x += x - tx * TileTexels - dx * TileTexels;
			sum.y += y - ty * TileTexels - dy * TileTexels;
			++sum.n;
		}
	}
	tileCentres.resize(sums.size());
	for (std::size_t i = 0; i < sums.size(); ++i)
		for (const auto &sum : sums[i])
		{
			// Texel centres, four per logical pixel, as Q8 logical pixels in the cell.
			const auto centre = [&](std::int64_t total)
			{
				return sum.n ? std::clamp(int((total * 2 + sum.n) * 32 / sum.n), 0, 8191) : 4096;
			};
			tileCentres[i].push_back({centre(sum.x), centre(sum.y)});
		}
}

StoneMap::Texel StoneMap::at(int tx, int ty, int px, int py) const
{
	const int x = tx * TileTexels + std::clamp(px >> 6, 0, TileTexels - 1);
	const int y = ty * TileTexels + std::clamp(py >> 6, 0, TileTexels - 1);
	const auto i = std::size_t(y) * std::size_t(size) + std::size_t(x);
	const auto t = texels[i], u = seconds[i];
	// The map stores native pixels x 32; a native pixel is 256 Q8 logical units.
	return {std::uint8_t(t), std::uint8_t(t >> 12 & 15), std::uint8_t(t >> 16), std::uint8_t(t >> 8 & 15),
			(t >> 24) * 8u, std::uint8_t(u), std::uint8_t(u >> 8 & 15), (u >> 24) * 8u,
			(int(u >> 16 & 255) - 128) * 8};
}

StoneMaps::StoneMaps(const Catalog &c)
{
	slots.resize(c.materials.size());
	for (std::size_t id = 0; id < c.materials.size(); ++id)
	{
		const auto &path = c.materials[id].stones;
		if (path.empty())
			continue;
		// Missing data is a packaging fault: report it while loading the catalog,
		// not in the middle of a frame.
		auto *stream = GAGCore::Toolkit::getFileManager()->openImage(path.c_str());
		if (!stream)
			throw std::runtime_error("Missing terrain stone map: " + path);
		SDL_CloseIO(stream);
		slots[id] = std::make_unique<Slot>();
		slots[id]->path = path;
	}
}

const StoneMap *StoneMaps::get(MaterialId id) const
{
	if (!has(id))
		return nullptr;
	auto &slot = *slots[id];
	std::call_once(slot.once, [&]
	{
		auto *stream = GAGCore::Toolkit::getFileManager()->openImage(slot.path.c_str());
		if (!stream)
			throw std::runtime_error("Missing terrain stone map: " + slot.path);
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> loaded(IMG_Load_IO(stream, true),
																		   SDL_DestroySurface);
		if (!loaded)
			throw std::runtime_error("Unreadable terrain stone map: " + slot.path);
		std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
			SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
		if (!rgba || rgba->w != 2 * rgba->h)
			throw std::runtime_error("Invalid terrain stone map: " + slot.path);
		slot.map = std::make_unique<StoneMap>(static_cast<const std::uint8_t *>(rgba->pixels), rgba->h,
											  rgba->pitch);
	});
	return slot.map.get();
}

void applyStones(const Recipe &r, const StoneMap &map, int px, int py, Coverage &c)
{
	const unsigned wrap = unsigned(map.tiles() - 1);
	const auto t = map.at(int(unsigned(r.x) & wrap), int(unsigned(r.y) & wrap), px, py);
	const auto kept = [&](unsigned tile, unsigned stone) { return (r.stonesKept[tile] >> stone & 1u) != 0; };
	const bool own = kept(t.ownTile, t.own);
	// The nearer neighbour of the other state, within the joint band.
	unsigned edge = 65535;
	const auto across = [&](unsigned tile, unsigned stone, unsigned joint)
	{
		if ((tile != t.ownTile || stone != t.own) && kept(tile, stone) != own)
			edge = std::min(edge, joint);
	};
	across(t.acrossTile, t.across, t.joint);
	across(t.secondTile, t.second, t.secondJoint);
	// A kept stone beside a dropped one ends at its own face: the joint there
	// shows the ground, and the face fades into it over StoneRimQ8, half on
	// either side. Joints between kept stones, and every dropped stone, stay
	// whole.
	unsigned target = own ? 65536u : 0u;
	if (own && edge != 65535)
		target = unsigned(std::clamp(32768 + t.face * 32768 / int(StoneRimQ8 / 2), 0, 65536));

	int stone = -1;
	unsigned stoneWeight = 0;
	for (int i = 0; i < 4; ++i)
		if (c.weight[i] && c.material[i] == r.stoneMaterial)
		{
			stone = i;
			stoneWeight = c.weight[i];
		}
	if (target != stoneWeight)
	{
		const auto freeSlot = [&]
		{
			for (int i = 0; i < 4; ++i)
				if (!c.weight[i]) return i;
			return -1;
		};
		const unsigned others = 65536 - stoneWeight, rest = 65536 - target;
		if (stone < 0 && (stone = freeSlot()) < 0)
			return; // Four other materials: keep the smooth border here.
		c.material[stone] = r.stoneMaterial;
		c.weight[stone] = 0;
		if (others)
		{
			for (int i = 0; i < 4; ++i)
				if (i != stone)
					c.weight[i] = unsigned(std::uint64_t(c.weight[i]) * rest / others);
		}
		else if (rest)
		{
			// The smooth border gave this pixel wholly to the stones: a dropped
			// stone reveals the ground.
			int ground = -1;
			for (int i = 0; i < 4 && ground < 0; ++i)
				if (i != stone && c.material[i] == r.stoneGround) ground = i;
			if (ground < 0)
				for (int i = 0; i < 4 && ground < 0; ++i)
					if (i != stone && !c.weight[i]) ground = i;
			c.material[ground] = r.stoneGround;
			c.weight[ground] = rest;
		}
		c.weight[stone] = target;
		unsigned sum = 0;
		int largest = 0;
		for (int i = 0; i < 4; ++i)
		{
			sum += c.weight[i];
			if (c.weight[i] > c.weight[largest]) largest = i;
		}
		c.weight[largest] += 65536 - sum;
	}
	int dominant = 0;
	for (int i = 1; i < 4; ++i)
		if (c.weight[i] > c.weight[dominant]) dominant = i;
	if (edge != 65535)
	{
		// Seam shading measures from the stone edge: a kept stone's face meets
		// the ground beside it, and its joint and the dropped stone beyond lie
		// in the stone's contact shadow.
		// Beyond a dropped stone's joint the kept face lies about one grout
		// width further than the joint, so the shadow continues across it.
		c.margin = own ? unsigned(std::abs(t.face)) : edge + unsigned(std::max(0, -t.face));
		if (c.material[dominant] == r.stoneMaterial)
		{
			int ground = -1;
			for (int i = 0; i < 4; ++i)
				if (i != dominant && c.weight[i] && (ground < 0 || c.weight[i] > c.weight[ground])) ground = i;
			c.neighbor = ground < 0 ? r.stoneGround : c.material[ground];
		}
		else
			c.neighbor = r.stoneMaterial;
	}
	else if (c.neighbor == r.stoneMaterial || c.material[dominant] == r.stoneMaterial)
	{
		// The smooth border no longer marks a stone edge here.
		c.margin = 65535;
		c.neighbor = c.material[dominant];
	}
}
} // namespace TerrainVisual
