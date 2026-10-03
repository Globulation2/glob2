// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EngineTiming.h"
#include "GameHeader.h"
#include "GenerationRequest.h"
#include "GenerationValidation.h"
#include "GeneratorRegistry.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace CustomGameRules
{
struct Rule;
}
struct Ruleset;

// Draft state is independent of widgets and of the serialized game header.
struct CustomGameSetup
{
	enum Controller
	{
		Human,
		Computer,
		Shared,
		Closed
	};
	struct Colony
	{
		Controller controller = Computer;
		AI::ImplementationID ai = AI::NUMBI;
		int alliance = 0;
	};
	std::array<Colony, Team::MAX_COUNT> colonies;
	GenerationRequest generator;
	GenerationHistory generatorHistory;
	int capacity;
	bool random = false, prestige = true, revealed = false, locked = true;
	int speed = 0;
	bool noResourceGrowth = false, instantConstruction = false, noHunger = false;
	int resourceScarcity = 0, stockpileStart = 0;
	bool unitUpgradesDisabled = false, unitsFearless = false, permadeathDisabled = false, peacefulMode = false;
	int glassCannonLevel = 0, buildingHpLevel = 0;
	// Level the generated map's starting workers spawn at; applied to the generated map, so
	// changing it regenerates the preview (mapRevision).
	int startingUnitLevel = 0;
	// Sudden-death timer choices in game minutes (0 = off). Prestige comes only from top-level
	// schools, so an earlier buzzer almost always finds every colony tied at zero; in default AI
	// matches the first sole prestige leader appeared 13-28 minutes in.
	static constexpr std::array<int, 5> suddenDeathMinuteChoices = {0, 30, 45, 60, 90};
	int suddenDeathMinutes = 0;
	std::string format = "FFA";
	// The ruleset the rules started from (data/rulesets.json). Edits do not change it: what
	// differs from it is derived (rulesetDiff), so undoing an edit makes the ruleset whole again.
	std::string rulesetId = "standard";
	std::string premadeMap;
	unsigned mapRevision = 0;
	CustomGameSetup()
	{
		generator.setMethodDefaults(GeneratorRegistry::builtins().methods(false).front());
		capacity = generator.nbTeams;
		for (int i = 0; i < Team::MAX_COUNT; ++i)
			colonies[i].alliance = i;
		colonies[0].controller = Human;
	}
	int controllerCount() const
	{
		int n = 0;
		for (int i = 0; i < capacity; ++i)
			n += colonies[i].controller == Shared ? 2 : colonies[i].controller != Closed;
		return n;
	}
	int activeColonies() const
	{
		int n = 0;
		for (int i = 0; i < capacity; ++i)
			n += colonies[i].controller != Closed;
		return n;
	}
	std::optional<int> humanColony() const
	{
		for (int i = 0; i < capacity; ++i)
			if (colonies[i].controller == Human || colonies[i].controller == Shared)
				return i;
		return {};
	}
	bool setController(int index, Controller c)
	{
		if (index < 0 || index >= capacity)
			return false;
		const auto old = colonies;
		if (c == Human || c == Shared)
			for (int i = 0; i < Team::MAX_COUNT; ++i)
				if (i != index &&
					(colonies[i].controller == Human || colonies[i].controller == Shared))
					colonies[i].controller = Computer;
		colonies[index].controller = c;
		if (controllerCount() > Team::MAX_COUNT)
		{
			colonies = old;
			return false;
		}
		format = "Custom teams";
		return true;
	}
	void setCapacity(int n)
	{
		if (n >= 1 && n <= Team::MAX_COUNT && n != capacity)
		{
			capacity = n;
			++mapRevision;
		}
	}
	bool presetTeams(int preset)
	{
		if (preset == 1 && activeColonies() != 4)
			return false;
		if (preset == 2 && (!humanColony() || activeColonies() < 2))
			return false;
		int position = 0;
		for (int i = 0; i < capacity; ++i)
			if (colonies[i].controller != Closed)
			{
				colonies[i].alliance = preset == 0			 ? i
									   : preset == 1		 ? position / 2
									   : i == *humanColony() ? 0
															 : 1;
				++position;
			}
		format = preset == 0 ? "FFA" : preset == 1 ? "2 vs 2" : "You vs all";
		return true;
	}
	// Rules, through the CustomGameRules registry (defined in CustomGameRules.cpp).
	int ruleValue(const CustomGameRules::Rule &rule) const;
	// Clamps to the rule's range. Returns true when the generated map must be regenerated
	// (mapRevision has then been bumped).
	bool setRule(const CustomGameRules::Rule &rule, int value);
	// Every rule to the ruleset's values (Standard for an unknown id). Returns setRule's result.
	bool applyRuleset(const std::string &id);
	const Ruleset &baseRuleset() const;
	// `room`: compare only what an online room carries and shows.
	bool ruleCounts(const CustomGameRules::Rule &rule, bool room = false) const;
	bool ruleChanged(const CustomGameRules::Rule &rule, bool room = false) const;
	bool ruleNonStandard(const CustomGameRules::Rule &rule, bool room = false) const;
	std::vector<const CustomGameRules::Rule *> rulesetDiff(bool room = false) const;
	// "Blitz", or "Blitz + 2 changes", translated.
	std::string rulesetTitle(bool room = false) const;
	std::string validation() const
	{
		if (capacity < 1 || capacity > Team::MAX_COUNT)
			return "Invalid colony count.";
		if (random)
		{
			const auto *definition = GeneratorRegistry::builtins().find(generator.method);
			if (!definition)
				return "Unknown generator";
			auto request = generator;
			request.nbTeams = capacity;
			const auto error = validateGenerationRequest(request, *definition);
			if (!error.empty())
				return error;
		}
		if (controllerCount() > Team::MAX_COUNT)
			return "Shared control needs a free controller slot (maximum %0).";
		if (activeColonies() < 1)
			return "Open at least one colony to start a match.";
		return {};
	}
	void writeHeader(GameHeader &header, const std::string &username) const
	{
		for (int i = 0; i < Team::MAX_COUNT; ++i)
			header.getBasePlayer(i) = BasePlayer();
		int count = 0;
		// Keep the real local controller first for existing save/load paths.
		auto human = humanColony();
		if (human)
			header.getBasePlayer(count++) =
				BasePlayer(0, username.c_str(), *human, BasePlayer::P_LOCAL);
		for (int i = 0; i < capacity; ++i)
		{
			const Colony &c = colonies[i];
			if (c.controller == Computer || c.controller == Shared)
			{
				std::string name = "AI " + std::to_string(i + 1);
				header.getBasePlayer(count) = BasePlayer(
					count, name.c_str(), i, BasePlayer::playerTypeFromImplementationID(c.ai));
				++count;
			}
		}
		header.setNumberOfPlayers(count);
		for (int i = 0; i < Team::MAX_COUNT; ++i)
			header.setAllyTeamNumber(i, colonies[i].alliance + 1);
		header.setAllyTeamsFixed(locked);
		header.setMapDiscovered(revealed);
		WinningCondition::setPrestigeWinCondition(header.getWinningConditions(), prestige);
		header.setResourceGrowthDisabled(noResourceGrowth);
		header.setResourceScarcityLevel(static_cast<Uint8>(resourceScarcity));
		header.setInstantConstructionEnabled(instantConstruction);
		header.setStockpileStartLevel(static_cast<Uint8>(stockpileStart));
		header.setHungerDisabled(noHunger);
		header.setUnitUpgradesDisabled(unitUpgradesDisabled);
		header.setGlassCannonLevel(static_cast<Uint8>(glassCannonLevel));
		header.setUnitsFearless(unitsFearless);
		header.setPermadeathDisabled(permadeathDisabled);
		header.setPeacefulModeEnabled(peacefulMode);
		header.setBuildingHpLevel(static_cast<Uint8>(buildingHpLevel));
		std::optional<Uint32> endStepTick;
		if (suddenDeathMinutes != 0)
			endStepTick = static_cast<Uint32>(suddenDeathMinutes) * 60 * GAME_TICKS_PER_SECOND;
		WinningCondition::setSuddenDeathWinCondition(header.getWinningConditions(), endStepTick);
	}
};
