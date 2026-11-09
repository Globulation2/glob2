// SPDX-License-Identifier: GPL-3.0-or-later
// Oracles that hold for any live game, independent of a committed golden:
//
//   expectBuildingInvariants(game)  structural checks every building must satisfy
//   MaterialLedger                  per-material conservation of map, building and
//                                   carried stock against the measured flows
//
// Needs a Game, so it links into glob2-sim-tests and glob2-engine-tests.
#pragma once

#include "Material.h"

#include <SDL3/SDL_stdinc.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>

class Game;

namespace glob2test
{
	// Game::integrity, then for every live building of every team: units inside
	// within maxUnitInside, and for every material non-negative stock and a
	// reservation between zero and the stock.
	void expectBuildingInvariants(Game& game);

	// A non-negative exact fraction, for carried packets (numerator/denominator of
	// one harvested unit) converted into building stock units.
	struct StockAmount
	{
		std::int64_t numerator = 0, denominator = 1;
		StockAmount& operator+=(const StockAmount& other);
		bool operator==(const StockAmount& other) const;
		std::string text() const;
	};

	// One census of every material in the game, per material slot.
	struct MaterialCensus
	{
		// Σ over cells of Map::materialStocksAt: harvestable map stock.
		std::array<std::int64_t, MaterialSlotCount> map{};
		// Building stock (Building::material of buildings without team materials)
		// plus every team's teamMaterials, in each building's stock units.
		std::array<std::int64_t, MaterialSlotCount> stock{};
		// Packets carried by live units, in stock units at the ledger's multiplier.
		std::array<StockAmount, MaterialSlotCount> carried{};
	};

	// Stock units one carried packet of each material becomes in the buildings that
	// accept it (BuildingType::materialMultiplier); `consistent`, when given, records
	// per material whether every accepting building type agrees.
	std::array<std::int64_t, MaterialSlotCount> packetMultipliers(const Game& game,
		std::array<bool, MaterialSlotCount>* consistent = nullptr);
	// Counts every material on the map, in stock and carried. Needs no running ledger.
	MaterialCensus takeMaterialCensus(Game& game);

	// The measured flows (GameplayMeasurements events) the ledger balances, summed
	// over teams; growth is recorded once per team for the whole map, so it is read
	// from team 0 alone.
	struct MaterialFlows
	{
		std::array<std::int64_t, MaterialSlotCount> grown{}, decayed{}, harvested{}, cleared{}, consumed{};
		std::uint64_t spillageEvents = 0, repairs = 0;
	};

	// Material conservation, checked per call (call it every tick). Two compartments
	// balance separately against the measured flows, per material r:
	//
	//   map:     map(t)    = map(s) + grown - decayed - harvested - cleared
	//   colony:  stock(t) + carried(t) = stock(s) + carried(s)
	//                        + harvested * M[r] - consumed + credited
	//
	// with flows counted since the segment start s, and M[r] the stock units one
	// harvested packet becomes in every building that accepts r (the catalog's
	// materialMultiplier; 1 for the basic materials, 10 for fruit). Both are exact
	// identities while every flow is measured. Some flows are counted but not sized;
	// at a check where one occurred the compartment may only have lost material
	// (actual <= expected: nothing appears from nowhere), and an exact segment then
	// restarts from the actual census:
	//
	//   colony: materialSpillageEvents grew (a discarded packet fraction, or a
	//     carried packet replaced); completed[REPAIRED] grew (repair consumption is
	//     not recorded in consumed); a unit that carried material at the previous
	//     check died or vanished; a building that held stock was removed;
	//   map: a clearing event for a deposit whose clearConsumption is not One
	//     (cleared counts events, not the whole deposit removed).
	//
	// Two cases are not checked at all for the material: buildings accepting r
	// disagree on the multiplier (colony), or a deposit on the map is harvested
	// other than one unit per event (ResourceConsumption::All, Infinite,
	// destroysDeposit; map). inexactReasons() lists every restart and exclusion.
	//
	// Material placed by the test (Building::setMaterial, addMaterialIntoBuilding,
	// Map edits) between checks must be declared with credit()/creditMap().
	//
	// Create the ledger after the game's first engine step: event counters are live
	// only once that step has started statistics collection (the constructor checks).
	class MaterialLedger
	{
	public:
		explicit MaterialLedger(Game& game);

		// The test put `amount` stock units of `material` into a building (or team).
		void credit(int material, std::int64_t amount) { credited[material] += amount; }
		// The test changed the map stock of `material` by `amount`.
		void creditMap(int material, std::int64_t amount) { mapCredited[material] += amount; }

		// Checks both compartments for every material. The first imbalance fails the
		// test with every term of the identity; later calls stay quiet so a per-tick
		// loop reports one failure. Returns whether the ledger still balances.
		bool check(const std::string& where);

		// Whether every check so far was an exact identity (no restart, no exclusion).
		bool colonyExact() const { return colonyInexact.empty() && colonyRestarts == 0; }
		bool mapExact() const { return mapInexact.empty() && mapRestarts == 0; }
		std::string inexactReasons() const;
		int checks() const { return checkCount; }

		MaterialCensus census() const;
		MaterialFlows flows() const;
		const std::array<std::int64_t, MaterialSlotCount>& multipliers() const { return multiplier; }

	private:
		// The unmeasured colony loss since the previous check, if any.
		std::string observeColonyLosses(const MaterialFlows& now);
		void observeDeposits();
		Game& game;
		MaterialCensus colonyInitial;
		std::array<std::int64_t, MaterialSlotCount> mapInitial{}, clearedAtLastCheck{};
		MaterialFlows colonyFlows, mapFlows, previousFlows;
		std::array<std::int64_t, MaterialSlotCount> credited{}, mapCredited{}, multiplier{};
		std::array<bool, MaterialSlotCount> multiplierConsistent{};
		MaterialMask wholeClearing = 0;
		// Materials excluded from a compartment, and why.
		std::map<int, std::string> colonyInexact, mapInexact;
		std::map<std::string, int> losses;
		int colonyRestarts = 0, mapRestarts = 0;
		// Carriers and stocked buildings at the previous check, by gid.
		std::map<Uint16, bool> carriers, stocked;
		bool failed = false;
		int checkCount = 0;
	};
}
