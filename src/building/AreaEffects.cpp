// SPDX-License-Identifier: GPL-3.0-or-later
#include "AreaEffects.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "Team.h"
#include <algorithm>
#include <bit>

namespace BuildingAreaEffects
{
void Runtime::reset()
{
	world = 0;
	scan = true;
	teams = 0;
	pending.clear();
	candidates.clear();
	emitters.clear();
	chunkEmitters.clear();
	dirty.clear();
	for (auto &field : fields)
		std::vector<Uint16>().swap(field);
	std::vector<Uint16>().swap(fertility);
	scratch.clear();
	metrics = {};
}
void Runtime::configure(const Game &game)
{
	reset();
	enabled_ = false;
	for (std::size_t i = 0; i < game.buildingsTypes.size(); ++i)
		enabled_ |= game.buildingsTypes.get(i)->semantics.areaEffects.enabled();
}
Building *Runtime::building(Game &game, Uint16 gid) const
{
	const int t = Building::GIDtoTeam(gid);
	return t < game.mapHeader.getNumberOfTeams() && game.teams[t]
			   ? game.teams[t]->myBuildings[Building::GIDtoID(gid)]
			   : nullptr;
}
int Runtime::emittingType(const Building &b) const
{
	if (b.buildingState == Building::DEAD || b.buildingState == Building::WAITING_FOR_DESTRUCTION)
		return -1;
	if (b.constructionResultState == Building::REPAIR && b.constructionOriginTypeNum >= 0)
		return b.constructionOriginTypeNum;
	return b.buildingState == Building::ALIVE && !b.type->isBuildingSite &&
				   b.constructionResultState == Building::NO_CONSTRUCTION
			   ? b.typeNum
			   : -1;
}
Runtime::Emitter Runtime::describe(const Building &b, int type) const
{
	const auto &definition = *b.owner->game->buildingsTypes.get(type);
	return {b.posX - b.type->decLeft + definition.decLeft,
			b.posY - b.type->decTop + definition.decTop,
			definition.width,
			definition.height,
			type,
			b.owner->allies | Team::teamNumberToMask(b.owner->teamNumber),
			b.owner->attackableTeams(),
			definition.semantics.areaEffects};
}
bool Runtime::covers(const Emitter &e, int x, int y) const
{
	// A wrapped interval wider than the map covers each tile exactly once.
	const int r = e.spec.radius;
	const auto axis = [](int p, int start, int length, int size)
	{ return length >= size || ((p - start) & (size - 1)) < length; };
	return axis(x, e.x - r, e.width + 2 * r, geometry.width) &&
		   axis(y, e.y - r, e.height + 2 * r, geometry.height);
}
std::vector<std::size_t> Runtime::chunks(const Emitter &e) const
{
	std::vector<std::size_t> result;
	const int r = e.spec.radius;
	// Enumerate touched chunk coordinates, rather than the area in tiles.
	std::vector<int> xs, ys;
	const auto axis = [&](int start, int length, int size, std::vector<int> &out)
	{
		const int count = (size + 15) / 16;
		if (length >= size)
		{
			for (int c = 0; c < count; ++c)
				out.push_back(c);
			return;
		}
		const int first = start & (size - 1), last = (first + length - 1) / 16;
		for (int c = first / 16; c <= last; ++c)
			out.push_back(c % count);
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
	};
	axis(e.x - r, e.width + 2 * r, geometry.width, xs);
	axis(e.y - r, e.height + 2 * r, geometry.height, ys);
	for (int y : ys)
		for (int x : xs)
			result.push_back(std::size_t(y) * geometry.chunksWide + x);
	return result;
}
void Runtime::allocate(const Emitter &e)
{
	const auto &s = e.spec;
	const bool used[Channels] = {s.healingQ8 != 0,
								 s.damageQ8 != 0,
								 s.feedingQ8 != 0,
								 (s.attackBuffBps || s.attackWeaknessBps) != 0,
								 (s.armorBuffBps || s.armorWeaknessBps) != 0,
								 s.attackBuffBps != 0,
								 s.armorBuffBps != 0};
	const auto cells = std::size_t(geometry.width) * geometry.height;
	for (int c = 0; c < Channels; ++c)
		if (used[c] && fields[c].empty())
		{
			fields[c].assign(cells * teams, c < UnitAttack ? 0 : Neutral);
			++metrics.allocations;
		}
	if ((s.fertilityBuffBps || s.fertilityWeaknessBps) && fertility.empty())
	{
		fertility.assign(cells, Neutral);
		fertilityChanges.markAll();
		++metrics.allocations;
	}
}
void Runtime::reconcile(Game &game, Uint16 gid)
{
	auto *b = building(game, gid);
	const int type = b ? emittingType(*b) : -1;
	const bool valid = type >= 0 && game.buildingsTypes.get(type)->semantics.areaEffects.enabled();
	// Keep sites/temporarily inactive buildings discoverable at later transitions.
	if (b && (valid || b->type->semantics.areaEffects.enabled() || b->areaFundingType >= 0))
		candidates.insert(gid);
	else
		candidates.erase(gid);
	if (b && (b->areaFundingType != type || b->areaFundingTeam != b->owner->teamNumber))
	{
		b->areaFunded = false;
		b->areaFundingType = -1;
		b->areaFundingTeam = -1;
	}
	auto old = emitters.find(gid);
	const bool active =
		valid && b->areaFunded && Uint32(game.stepCounter - b->areaFundingTick) < PulseTicks;
	Emitter current;
	if (active)
		current = describe(*b, type);
	if (old != emitters.end() && active && old->second == current)
		return;
	if (old != emitters.end())
	{
		for (auto chunk : chunks(old->second))
		{
			auto &list = chunkEmitters[chunk];
			std::erase(list, gid);
			dirty.insert(chunk);
		}
		emitters.erase(old);
	}
	if (active)
	{
		allocate(current);
		emitters.emplace(gid, current);
		for (auto chunk : chunks(current))
		{
			chunkEmitters[chunk].push_back(gid);
			dirty.insert(chunk);
		}
	}
}
void Runtime::beginTick(Game &game, bool fund)
{
	if (!enabled_)
		return;
	if (world != game.map.identity() || teams != game.mapHeader.getNumberOfTeams())
	{
		reset();
		world = game.map.identity();
		teams = game.mapHeader.getNumberOfTeams();
		geometry.reset(game.map.getW(), game.map.getH(), std::countr_zero(Uint32(game.map.getW())),
					   game.map.getMaskW());
		chunkEmitters.resize(geometry.count());
		fertilityChanges.reset(geometry.count());
		scratch.resize(std::size_t(teams) * 256);
		++metrics.allocations;
	}
	if (scan)
	{
		for (int t = 0; t < teams; ++t)
			if (game.teams[t])
				for (int id = 0; id < Building::MAX_COUNT; ++id)
					if (auto *b = game.teams[t]->myBuildings[id])
						changed(b->gid);
		scan = false;
	}
	bool diplomacy = false;
	for (int t = 0; t < teams; ++t)
	{
		const auto *team = game.teams[t];
		const auto a = team ? team->allies : 0, e = team ? team->attackableTeams() : 0;
		diplomacy |= allies[t] != a || enemies[t] != e;
		allies[t] = a;
		enemies[t] = e;
	}
	if (diplomacy)
		for (auto gid : candidates)
			changed(gid);
	for (auto gid : pending)
		reconcile(game, gid);
	pending.clear();
	if (fund && (game.stepCounter & (PulseTicks - 1)) == 0)
	{
		for (auto gid : candidates)
			if (auto *b = building(game, gid))
			{
				const int type = emittingType(*b);
				if (type < 0 || !game.buildingsTypes.get(type)->semantics.areaEffects.enabled())
					continue;
				// A restored interval has already been paid: do not charge it twice.
				if (b->areaFundingTick == game.stepCounter && b->areaFundingType == type)
					continue;
				const auto &spec = game.buildingsTypes.get(type)->semantics.areaEffects;
				b->areaFunded = !spec.costMask || b->reserveMaterials(spec.cost);
				b->areaFundingType = type;
				b->areaFundingTeam = b->owner->teamNumber;
				b->areaFundingTick = game.stepCounter;
				if (b->areaFunded && spec.costMask)
				{
					b->consumeReservedMaterials(spec.cost, GameplayMeasurements::AREA_UPKEEP);
				}
				// Funding cannot change the emitting type, so this candidate remains
				// valid. Reconcile directly instead of allocating a pending-set node.
				reconcile(game, gid);
			}
		for (auto gid : pending)
			reconcile(game, gid);
		pending.clear();
	}
	if (!dirty.empty())
		rebuild();
}
void Runtime::rebuild()
{
	const auto cells = std::size_t(geometry.width) * geometry.height;
	const auto factor = [](int plus, int minus)
	{ return Uint16(std::clamp(int(Neutral) + plus - minus, 0, 40000)); };
	++metrics.rebuilds;
	for (auto chunk : dirty)
	{
		++metrics.rebuiltChunks;
		std::fill(scratch.begin(), scratch.end(), Accumulator{});
		bool fertilityChanged = false;
		const int x0 = int(chunk % geometry.chunksWide) * 16,
				  y0 = int(chunk / geometry.chunksWide) * 16;
		const int x1 = std::min(x0 + 16, geometry.width), y1 = std::min(y0 + 16, geometry.height);
		for (auto gid : chunkEmitters[chunk])
		{
			++metrics.emitterVisits;
			const auto &e = emitters.at(gid);
			const auto &s = e.spec;
			for (int y = y0; y < y1; ++y)
				for (int x = x0; x < x1; ++x)
					if (covers(e, x, y))
					{
						const int local = (y - y0) * 16 + x - x0;
						// Land has no recipient team. Its accumulator shares the first
						// team's scratch row; combat/service members remain independent.
						auto &land = scratch[local];
						land.fertility = std::max<int>(land.fertility, s.fertilityBuffBps);
						land.fertilityWeak =
							std::max<int>(land.fertilityWeak, s.fertilityWeaknessBps);
						for (int t = 0; t < teams; ++t)
						{
							auto &a = scratch[std::size_t(t) * 256 + local];
							const auto bit = Team::teamNumberToMask(t);
							if (e.allies & bit)
							{
								a.heal = std::max<int>(a.heal, s.healingQ8);
								a.feed = std::max<int>(a.feed, s.feedingQ8);
								a.attack = std::max<int>(a.attack, s.attackBuffBps);
								a.armor = std::max<int>(a.armor, s.armorBuffBps);
							}
							if (e.enemies & bit)
							{
								a.damage = std::max<int>(a.damage, s.damageQ8);
								a.attackWeak = std::max<int>(a.attackWeak, s.attackWeaknessBps);
								a.armorWeak = std::max<int>(a.armorWeak, s.armorWeaknessBps);
							}
						}
					}
		}
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
			{
				const auto tile = std::size_t(y) * geometry.width + x;
				const int local = (y - y0) * 16 + x - x0;
				for (int t = 0; t < teams; ++t)
				{
					const auto &a = scratch[std::size_t(t) * 256 + local];
					const Uint16 values[Channels] = {a.heal,
													 a.damage,
													 a.feed,
													 factor(a.attack, a.attackWeak),
													 factor(a.armor, a.armorWeak),
													 factor(a.attack, 0),
													 factor(a.armor, 0)};
					for (int c = 0; c < Channels; ++c)
						if (!fields[c].empty())
							fields[c][std::size_t(t) * cells + tile] = values[c];
				}
				if (!fertility.empty())
				{
					const auto modifier =
						factor(scratch[local].fertility, scratch[local].fertilityWeak);
					fertilityChanged |= fertility[tile] != modifier;
					fertility[tile] = modifier;
				}
			}
		if (fertilityChanged)
			fertilityChanges.mark(chunk);
	}
	dirty.clear();
}
void Runtime::captureFertility(FertilitySnapshot &out, Uint64 &bytesCopied) const
{
	if (fertility.empty())
	{
		out = {};
		return;
	}
	const bool full = out.world != world || out.values.size() != fertility.size();
	if (full)
	{
		out.values.resize(fertility.size());
		out.stamps.assign(geometry.count(), 0);
	}
	for (std::size_t c = 0; c < geometry.count(); ++c)
		if (full || out.stamps[c] != fertilityChanges.chunks[c])
		{
			geometry.forEachRow(c,
								[&](std::size_t offset, std::size_t length)
								{
									std::copy_n(fertility.begin() + offset, length,
												out.values.begin() + offset);
									bytesCopied += length * sizeof(Uint16);
								});
			out.stamps[c] = fertilityChanges.chunks[c];
		}
	out.world = world;
	out.generation = fertilityChanges.generation;
}
std::size_t Runtime::fieldBytes() const
{
	std::size_t bytes = fertility.capacity() * sizeof(Uint16);
	for (const auto &f : fields)
		bytes += f.capacity() * sizeof(Uint16);
	return bytes;
}
} // namespace BuildingAreaEffects
