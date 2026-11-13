// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingSeedTemplates.h"
#include "BuildingSeedAudit.h"
#include "SeedCells.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace
{
using MapState::SeedPassability;
constexpr std::array<SeedPassability, MapState::SeedPassabilityCount> Kinds{
	SeedPassability::Walk, SeedPassability::WalkSwim, SeedPassability::WaterOnly, SeedPassability::Air};

//! Sets one bit, allocating a team's bitset on its first set bit; returns
//! whether it changed.
bool setBit(MapState::BuildingSeedPlanes::Bits& bits, std::size_t index, bool value, std::size_t cells)
{
	if (bits.empty())
	{
		if (!value) return false;
		bits.assign((cells + 63) / 64, 0);
	}
	const auto bit = std::uint64_t(1) << (index % 64);
	auto& word = bits[index / 64];
	const auto next = (word & ~bit) | (value ? bit : 0);
	const bool changed = next != word;
	word = next;
	return changed;
}

std::atomic<bool> auditFlag = [] {
	const char* value = std::getenv("GLOB2_BUILDING_SEED_AUDIT");
	return value && *value && *value != '0';
}();
}

namespace building_seed
{
bool auditEnabled() { return auditFlag.load(std::memory_order_relaxed); }
void setAuditEnabled(bool enabled) { auditFlag.store(enabled, std::memory_order_relaxed); }
}

void BuildingSeedTemplates::reset()
{
	data = {};
	geometry = {};
	changes = {};
	stale = true;
}

bool BuildingSeedTemplates::refresh(const MapState::View& view, std::size_t index, bool teamCells)
{
	bool changed = false;
	for (const auto kind : Kinds)
	{
		auto& value = data.planes[std::size_t(kind)][index];
		const auto next = gradient_preparation::buildingTemplateCell(view, index, kind);
		changed |= value != next;
		value = next;
	}
	if (!teamCells) return changed;
	const Uint32 forbidden = view.areas[index].forbidden;
	const auto building = view.occupancy[index].building;
	const int owner = building == NOGBID ? -1 : BuildingUtils::GIDtoTeam(building);
	for (int team = 0; team < TEAM_MAX_COUNT; ++team)
	{
		changed |= setBit(data.forbidden[team], index, forbidden & (Uint32(1) << team), data.cells);
		changed |= setBit(data.buildings[team], index, owner == team, data.cells);
	}
	return changed;
}

void BuildingSeedTemplates::changed(const MapState::View& view, std::size_t index, bool teamCells)
{
	// A stale or resized set is rebuilt whole on its next use.
	if (stale || data.cells != std::size_t(view.width) * view.height || index >= data.cells) return;
	if (refresh(view, index, teamCells)) changes.mark(geometry.chunkOf(index));
}

const MapState::BuildingSeedPlanes& BuildingSeedTemplates::ensure(const MapState::View& view)
{
	const auto cells = std::size_t(view.width) * view.height;
	if (!stale && data.cells == cells) return data;
	for (auto& plane : data.planes) plane.resize(cells);
	for (auto& bits : data.forbidden) bits.clear();
	for (auto& bits : data.buildings) bits.clear();
	for (std::size_t i = 0; i < cells; ++i)
	{
		for (const auto kind : Kinds)
			data.planes[std::size_t(kind)][i] = gradient_preparation::buildingTemplateCell(view, i, kind);
		for (Uint32 mask = view.areas[i].forbidden; mask; mask &= mask - 1)
		{
			const auto team = std::countr_zero(mask);
			if (team < TEAM_MAX_COUNT) setBit(data.forbidden[team], i, true, cells);
		}
		if (const auto building = view.occupancy[i].building; building != NOGBID)
		{
			const auto team = BuildingUtils::GIDtoTeam(building);
			if (team >= 0 && team < TEAM_MAX_COUNT) setBit(data.buildings[team], i, true, cells);
		}
	}
	data.cells = cells;
	geometry.reset(view.width, view.height, view.wDec, view.wMask);
	changes.reset(geometry.count());
	stale = false;
	++epoch;
	return data;
}

void BuildingSeedTemplates::capture(MapState::BuildingSeedSnapshot& out, std::uint64_t world, std::uint64_t& bytesCopied) const
{
	const auto cells = data.cells;
	const bool full = out.world != world || out.epoch != epoch || out.planes.cells != cells
		|| out.stamps.size() != changes.chunks.size();
	// A team's bitset allocated since this copy was filled is copied whole.
	const auto copyAllocated = [&](const MapState::BuildingSeedPlanes::Bits& from, MapState::BuildingSeedPlanes::Bits& to) {
		if (to.size() == from.size()) return;
		to = from;
		bytesCopied += from.size() * sizeof(std::uint64_t);
	};
	for (int team = 0; team < TEAM_MAX_COUNT; ++team)
	{
		copyAllocated(data.forbidden[team], out.planes.forbidden[team]);
		copyAllocated(data.buildings[team], out.planes.buildings[team]);
	}
	if (full)
	{
		for (auto& plane : out.planes.planes) plane.resize(cells);
		out.stamps.assign(changes.chunks.size(), 0);
	}
	const auto copyRange = [&](std::size_t start, std::size_t length) {
		for (std::size_t k = 0; k < data.planes.size(); ++k)
			std::copy_n(data.planes[k].begin() + start, length, out.planes.planes[k].begin() + start);
		bytesCopied += length * sizeof(std::uint16_t) * data.planes.size();
		// Whole words: neighbouring cells copied with them are live values too,
		// and their own chunks are recopied whenever their stamps move.
		const auto first = start / 64, last = (start + length + 63) / 64;
		const auto copyBits = [&](const MapState::BuildingSeedPlanes::Bits& from, MapState::BuildingSeedPlanes::Bits& to) {
			if (from.empty()) return;
			std::copy(from.begin() + first, from.begin() + last, to.begin() + first);
			bytesCopied += (last - first) * sizeof(std::uint64_t);
		};
		for (int team = 0; team < TEAM_MAX_COUNT; ++team)
		{
			copyBits(data.forbidden[team], out.planes.forbidden[team]);
			copyBits(data.buildings[team], out.planes.buildings[team]);
		}
	};
	std::size_t dirty = 0;
	if (!full)
		for (std::size_t c = 0; c < changes.chunks.size(); ++c) dirty += out.stamps[c] != changes.chunks[c];
	if (full || dirty > changes.chunks.size() / 2)
	{
		if (cells) copyRange(0, cells);
		out.stamps.assign(changes.chunks.begin(), changes.chunks.end());
	}
	else
		for (std::size_t c = 0; c < changes.chunks.size(); ++c)
			if (out.stamps[c] != changes.chunks[c])
			{
				geometry.forEachRow(c, copyRange);
				out.stamps[c] = changes.chunks[c];
			}
	out.planes.cells = cells;
	out.world = world;
	out.epoch = epoch;
	out.generation = changes.generation;
}
