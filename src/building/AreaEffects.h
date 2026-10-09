// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingCatalog.h"
#include "MapChangeTracking.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <vector>
#include <span>

class Game;
class Building;
namespace BuildingAreaEffects
{
constexpr Uint32 PulseTicks = 16;
constexpr Uint16 Neutral = 10000;
enum Channel
{
	Healing,
	Damage,
	Feeding,
	UnitAttack,
	UnitArmor,
	BuildingAttack,
	BuildingArmor,
	Channels
};
inline int scale(int value, Uint16 factor)
{
	return int(std::clamp<Sint64>(Sint64(value) * factor / Neutral, std::numeric_limits<int>::min(),
								  std::numeric_limits<int>::max()));
}
struct FertilitySnapshot
{
	std::vector<Uint16> values;
	std::vector<Uint64> stamps;
	Uint64 world = 0, generation = 0;
};
struct Metrics
{
	// Allocations count coverage field/scratch buffers, not all engine allocations.
	Uint64 rebuilds = 0, rebuiltChunks = 0, emitterVisits = 0, allocations = 0;
};
// Simulation-owned. Definitions remain cold; recipients read dense team-major planes.
class Runtime
{
	struct Emitter
	{
		int x = 0, y = 0, width = 0, height = 0, type = -1;
		Uint32 allies = 0, enemies = 0;
		BuildingAreaEffectsSpec spec;
		bool operator==(const Emitter &) const = default;
	};
	struct Accumulator
	{
		Uint16 heal = 0, damage = 0, feed = 0, attack = 0, attackWeak = 0, armor = 0, armorWeak = 0;
		Uint16 fertility = 0, fertilityWeak = 0;
	};
	bool enabled_ = false, scan = true;
	Uint64 world = 0;
	int teams = 0;
	std::array<Uint32, 32> allies{}, enemies{};
	// Sorted GIDs give deterministic upkeep order when shared stock is scarce.
	std::set<Uint16> pending, candidates;
	std::map<Uint16, Emitter> emitters;
	std::vector<std::vector<Uint16>> chunkEmitters;
	std::set<std::size_t> dirty;
	std::array<std::vector<Uint16>, Channels> fields;
	std::vector<Uint16> fertility;
	MapState::ChunkGeometry geometry;
	MapState::ChangeTracker fertilityChanges;
	std::vector<Accumulator> scratch;
	Building *building(Game &, Uint16) const;
	int emittingType(const Building &) const;
	Emitter describe(const Building &, int) const;
	std::vector<std::size_t> chunks(const Emitter &) const;
	bool covers(const Emitter &, int x, int y) const;
	void reconcile(Game &, Uint16);
	void rebuild();
	void allocate(const Emitter &);

  public:
	Metrics metrics;
	void configure(const Game &);
	void reset();
	bool enabled() const { return enabled_; }
	void changed(Uint16 gid)
	{
		if (enabled_)
			pending.insert(gid);
	}
	// fund=false reconstructs loaded coverage without charging or replaying a pulse.
	void beginTick(Game &, bool fund = true);
	Uint16 at(Channel channel, int team, std::size_t tile) const
	{
		const auto &plane = fields[channel];
		return plane.empty() ? (channel < UnitAttack ? 0 : Neutral)
							 : plane[std::size_t(team) * geometry.width * geometry.height + tile];
	}
	Uint64 fertilityGeneration() const { return fertilityChanges.generation; }
	std::span<const Uint16> fertilityValues() const { return fertility; }
	void captureFertility(FertilitySnapshot &, Uint64 &bytesCopied) const;
	std::size_t fieldBytes() const;
};
} // namespace BuildingAreaEffects
