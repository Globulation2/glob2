// SPDX-License-Identifier: GPL-3.0-or-later
#include "SimulationInvariants.h"

#include "Glob2Test.h"

#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "Map.h"
#include "Team.h"
#include "TeamStat.h"
#include "Unit.h"
#include "UnitCargo.h"

#include <numeric>
#include <utility>
#include <sstream>

namespace glob2test
{
	void expectBuildingInvariants(Game& game)
	{
		REQUIRE(game.integrity());
		for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
			for (auto* building : game.teams[team]->liveBuildings.entries())
			{
				if (!building) continue;
				CAPTURE(team);
				CAPTURE(building->gid);
				CHECK(building->unitsInside.size() <= std::size_t(building->maxUnitInside));
				for (int resource = 0; resource < MaterialCount; ++resource)
				{
					CAPTURE(resource);
					CHECK(building->material(resource) >= 0);
					CHECK(building->reservedMaterials[resource] >= 0);
					CHECK(building->reservedMaterials[resource] <= building->material(resource));
				}
			}
	}

	StockAmount& StockAmount::operator+=(const StockAmount& other)
	{
		const std::int64_t common = std::lcm(denominator, other.denominator);
		numerator = numerator * (common / denominator) + other.numerator * (common / other.denominator);
		denominator = common;
		const std::int64_t divisor = std::gcd(numerator, denominator);
		if (divisor > 1) { numerator /= divisor; denominator /= divisor; }
		return *this;
	}

	bool StockAmount::operator==(const StockAmount& other) const
	{
		return numerator * other.denominator == other.numerator * denominator;
	}

	std::string StockAmount::text() const
	{
		return denominator == 1 ? std::to_string(numerator)
			: std::to_string(numerator) + "/" + std::to_string(denominator);
	}

	namespace
	{
		std::string materialLabel(int material)
		{
			const auto key = materialKey(static_cast<MaterialId>(material));
			return key.empty() ? "slot " + std::to_string(material) : std::string(key);
		}

		// Packets a unit carries, by material, as fractions of one harvested unit.
		template <typename Fn>
		void forEachCarriedPacket(const Game& game, const Unit& unit, Fn&& fn)
		{
			if (!unit.widePrimaryCargo() && unit.carriedMaterial >= 0 && unit.carriedMaterial < MaterialSlotCount)
				fn(unit.carriedMaterial, std::int64_t(unit.carriedPacket.numerator), std::int64_t(unit.carriedPacket.denominator));
			if (unit.hasCapability(UnitRuntimeTraits::ExtendedCargo) || unit.widePrimaryCargo())
				if (const auto* cargo = game.unitCargo.find(unit.gid))
					for (const auto& entry : *cargo)
						if (entry.material >= 0 && entry.material < MaterialSlotCount)
							fn(entry.material, std::int64_t(entry.packet.numerator), std::int64_t(entry.packet.denominator));
		}

		template <typename Fn>
		void forEachLiveUnit(Game& game, Fn&& fn)
		{
			for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
				for (Unit* unit : game.teams[team]->liveUnits.entries())
					if (unit && !unit->isDead) fn(*unit);
		}

		template <typename Fn>
		void forEachLiveBuilding(Game& game, Fn&& fn)
		{
			for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
				for (Building* building : game.teams[team]->liveBuildings.entries())
					if (building && building->buildingState != Building::DEAD) fn(*building);
		}
	}

	std::array<std::int64_t, MaterialSlotCount> packetMultipliers(const Game& game,
		std::array<bool, MaterialSlotCount>* consistent)
	{
		// One packet becomes materialMultiplier stock units in the building that
		// accepts it; the census needs that factor to agree across the catalog.
		std::array<std::int64_t, MaterialSlotCount> multiplier{};
		for (int r = 0; r < MaterialSlotCount; ++r)
		{
			bool agrees = true;
			for (std::size_t id = 0; id < game.buildingsTypes.size(); ++id)
			{
				const auto* type = game.buildingsTypes.get(id);
				if (!type || type->maxMaterial[r] <= 0) continue;
				if (!multiplier[r]) multiplier[r] = type->materialMultiplier[r];
				else if (multiplier[r] != type->materialMultiplier[r]) agrees = false;
			}
			if (!multiplier[r]) multiplier[r] = 1;
			if (consistent) (*consistent)[r] = agrees;
		}
		return multiplier;
	}

	MaterialCensus takeMaterialCensus(Game& game)
	{
		const auto multiplier = packetMultipliers(game);
		MaterialCensus result;
		const Map& map = game.map;
		for (std::size_t cell = 0; cell < map.cellCount(); ++cell)
		{
			if (map.getResource(cell).type == NO_RES_TYPE) continue;
			const auto stocks = map.materialStocksAt(cell);
			for (unsigned r = 0; r < MaterialCount; ++r) result.map[r] += stocks[r];
		}
		for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
			for (int r = 0; r < MaterialSlotCount; ++r)
				result.stock[r] += game.teams[team]->teamMaterials[r];
		forEachLiveBuilding(game, [&](Building& building) {
			if (building.type->isVirtual || building.type->useTeamMaterials) return;
			for (int r = 0; r < MaterialSlotCount; ++r) result.stock[r] += building.material(r);
		});
		forEachLiveUnit(game, [&](Unit& unit) {
			forEachCarriedPacket(game, unit, [&](int r, std::int64_t numerator, std::int64_t denominator) {
				result.carried[r] += StockAmount{numerator * multiplier[r], denominator};
			});
		});
		return result;
	}

	MaterialLedger::MaterialLedger(Game& game) : game(game)
	{
		// Before collection starts, events recorded during a step can be replaced by
		// the next published statistics frame; the first engine step starts it.
		for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
			// events() reads the live collector, not the published frame, once collection is on.
			REQUIRE_MESSAGE(&std::as_const(game.teams[team]->stats).events() != &game.teams[team]->stats.measurements(),
				"step the game once before creating a MaterialLedger: event counters are live only once statistics collection has started");
		multiplier = packetMultipliers(game, &multiplierConsistent);
		for (int r = 0; r < MaterialSlotCount; ++r)
			if (!multiplierConsistent[r])
				colonyInexact[r] = "buildings accepting " + materialLabel(r) + " convert packets at different multipliers";
		colonyInitial = census();
		colonyFlows = mapFlows = previousFlows = flows();
		mapInitial = colonyInitial.map;
		for (int r = 0; r < MaterialSlotCount; ++r) clearedAtLastCheck[r] = colonyFlows.cleared[r];
		observeColonyLosses(colonyFlows);
		losses.clear();
		observeDeposits();
	}

	MaterialCensus MaterialLedger::census() const { return takeMaterialCensus(game); }

	MaterialFlows MaterialLedger::flows() const
	{
		MaterialFlows result;
		for (int team = 0; team < game.mapHeader.getNumberOfTeams(); ++team)
		{
			const auto& events = std::as_const(game.teams[team]->stats).events();
			for (int r = 0; r < MaterialSlotCount; ++r)
			{
				if (team == 0)
				{
					result.grown[r] = std::int64_t(events.growthGlobal[1][r]);
					result.decayed[r] = std::int64_t(events.growthGlobal[2][r]);
				}
				result.harvested[r] += std::int64_t(events.harvested[r]);
				result.cleared[r] += std::int64_t(events.cleared[r]);
				for (int purpose = 0; purpose < GameplayMeasurements::PURPOSES; ++purpose)
					result.consumed[r] += std::int64_t(events.consumed[purpose][r]);
			}
			result.spillageEvents += events.materialSpillageEvents;
			for (const auto& byType : events.completed[GameplayMeasurements::REPAIRED])
				for (const auto count : byType) result.repairs += count;
		}
		return result;
	}

	std::string MaterialLedger::observeColonyLosses(const MaterialFlows& now)
	{
		std::string reason;
		const auto note = [&](const char* why) { if (reason.empty()) reason = why; ++losses[why]; };
		if (now.spillageEvents != previousFlows.spillageEvents)
			note("materialSpillageEvents grew: a discarded packet fraction is counted, not sized");
		if (now.repairs != previousFlows.repairs)
			note("a repair completed: repair consumption is not recorded in consumed");

		// Carriers that died or vanished since the previous check took their cargo.
		std::map<Uint16, bool> nowCarriers, alive;
		forEachLiveUnit(game, [&](Unit& unit) {
			alive[unit.gid] = true;
			forEachCarriedPacket(game, unit, [&](int, std::int64_t, std::int64_t) { nowCarriers[unit.gid] = true; });
		});
		for (const auto& [gid, carried] : carriers)
			if (carried && !alive.count(gid))
				note("a carrier died: the material it carried is not measured");
		carriers = nowCarriers;

		// Buildings that held stock at the previous check and are gone took it with them.
		std::map<Uint16, bool> nowStocked;
		forEachLiveBuilding(game, [&](Building& building) {
			bool holds = false;
			for (int r = 0; r < MaterialSlotCount; ++r) holds |= building.material(r) > 0;
			nowStocked[building.gid] = holds;
		});
		for (const auto& [gid, holds] : stocked)
			if (holds && !nowStocked.count(gid))
				note("a stocked building was removed: its stock is not measured");
		stocked = nowStocked;
		return reason;
	}

	void MaterialLedger::observeDeposits()
	{
		// Deposits whose harvest removes other than one unit per harvest event.
		const Map& map = game.map;
		for (std::size_t cell = 0; cell < map.cellCount(); ++cell)
		{
			const auto type = map.getResource(cell).type;
			if (type == NO_RES_TYPE) continue;
			const auto& properties = map.resourcePropertiesByIndex(type);
			const auto& yields = map.resourceRegistry().yields(static_cast<ResourceId>(type));
			for (unsigned r = 0; r < MaterialCount; ++r)
				if ((properties.materialMask & (1u << r))
					&& (yields[r].consumption != ResourceConsumption::One || yields[r].destroysDeposit))
					mapInexact.emplace(int(r), "a " + materialLabel(int(r)) + " deposit is not harvested one unit at a time");
			if (properties.clearable && properties.clearConsumption != ResourceConsumption::One)
				wholeClearing |= MaterialMask(1u << materialIndex(properties.primaryMaterial));
		}
	}

	bool MaterialLedger::check(const std::string& where)
	{
		++checkCount;
		const MaterialFlows now = flows();
		const std::string loss = observeColonyLosses(now);
		observeDeposits();
		previousFlows = now;
		if (failed) return false;
		const MaterialCensus current = census();
		for (int r = 0; r < MaterialSlotCount && !failed; ++r)
		{
			if (!mapInexact.count(r))
			{
				const std::int64_t grown = now.grown[r] - mapFlows.grown[r];
				const std::int64_t decayed = now.decayed[r] - mapFlows.decayed[r];
				const std::int64_t harvested = now.harvested[r] - mapFlows.harvested[r];
				const std::int64_t cleared = now.cleared[r] - mapFlows.cleared[r];
				const std::int64_t expected = mapInitial[r] + grown - decayed - harvested - cleared + mapCredited[r];
				// A clearing that removes a whole deposit removes at least the one unit
				// the cleared event counts; the segment restarts after it.
				const bool whole = (wholeClearing & (1u << r)) && now.cleared[r] != clearedAtLastCheck[r];
				if (whole ? current.map[r] > expected : current.map[r] != expected)
				{
					std::ostringstream message;
					message << where << ": the map's " << materialLabel(r) << " does not balance: map " << current.map[r]
						<< (whole ? " > " : " != ") << "start " << mapInitial[r] << " + grown " << grown << " - decayed "
						<< decayed << " - harvested " << harvested << " - cleared " << cleared << " + credited "
						<< mapCredited[r] << " = " << expected;
					FAIL_CHECK(message.str());
					failed = true;
				}
				if (whole)
				{
					++mapRestarts;
					mapInitial[r] = current.map[r];
					mapFlows.grown[r] = now.grown[r]; mapFlows.decayed[r] = now.decayed[r];
					mapFlows.harvested[r] = now.harvested[r]; mapFlows.cleared[r] = now.cleared[r];
					mapCredited[r] = 0;
				}
			}
			clearedAtLastCheck[r] = now.cleared[r];

			if (multiplierConsistent[r] && !failed)
			{
				const std::int64_t harvested = now.harvested[r] - colonyFlows.harvested[r];
				const std::int64_t consumed = now.consumed[r] - colonyFlows.consumed[r];
				StockAmount actual{current.stock[r], 1};
				actual += current.carried[r];
				StockAmount expected{colonyInitial.stock[r] + harvested * multiplier[r] - consumed + credited[r], 1};
				expected += colonyInitial.carried[r];
				// Losses are the only unmeasured colony flow: nothing may appear from nowhere.
				const bool balanced = loss.empty() ? actual == expected
					: actual.numerator * expected.denominator <= expected.numerator * actual.denominator;
				if (!balanced)
				{
					std::ostringstream message;
					message << where << ": the colony's " << materialLabel(r) << " does not balance"
						<< (loss.empty() ? "" : " even allowing an unmeasured loss (" + loss + ")") << ": stock "
						<< current.stock[r] << " + carried " << current.carried[r].text() << (loss.empty() ? " != " : " > ")
						<< "start stock " << colonyInitial.stock[r] << " + start carried " << colonyInitial.carried[r].text()
						<< " + harvested " << harvested << " x " << multiplier[r] << " - consumed " << consumed
						<< " + credited " << credited[r] << " = " << expected.text();
					FAIL_CHECK(message.str());
					failed = true;
				}
			}
		}
		if (!loss.empty())
		{
			// Start a new exact segment from what is actually held now.
			++colonyRestarts;
			colonyInitial = current;
			colonyFlows = now;
			credited.fill(0);
		}
		return !failed;
	}

	std::string MaterialLedger::inexactReasons() const
	{
		std::ostringstream text;
		for (const auto& [r, reason] : colonyInexact) text << "colony " << materialLabel(r) << ": " << reason << '\n';
		for (const auto& [reason, count] : losses)
			text << "colony: " << count << " checks restarted after an unmeasured loss: " << reason << '\n';
		for (const auto& [r, reason] : mapInexact) text << "map " << materialLabel(r) << ": " << reason << '\n';
		if (mapRestarts) text << "map: " << mapRestarts << " restarts after clearing removed whole deposits\n";
		return text.str();
	}
}
