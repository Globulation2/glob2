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
	struct RuleDefinition
	{
		const char *label;
		const char *category;
	};
	static constexpr std::array<RuleDefinition, 19> ruleDefinitions = {
		{{"Victory", "Victory"},
		 {"Map knowledge", "World & diplomacy"},
		 {"Alliances", "World & diplomacy"},
		 {"Game speed", "Starting conditions & pace"},
		 {"Starting workers", "Starting conditions & pace"},
		 {"No resource growth", "Economy"},
		 {"Scarce resources", "Economy"},
		 {"Instant construction", "Economy"},
		 {"Stockpile start", "Economy"},
		 {"No hunger", "Economy"},
		 {"No upgrades", "Combat"},
		 {"Glass cannon", "Combat"},
		 {"Fearless", "Combat"},
		 {"No permadeath", "Combat"},
		 {"Peaceful mode", "Combat"},
		 {"Fortress buildings", "Combat"},
		 {"Veteran/Fast start", "Starting conditions & pace"},
		 {"Sudden-death timer", "Victory"},
		 {"Probability victory", "Victory"}}};
	bool ruleChanged(int index) const
	{
		switch (index)
		{
		case 0:
			return !prestige;
		case 1:
			return revealed;
		case 2:
			return !locked;
		case 3:
			return speed != 0;
		case 4:
			return random &&
				   generator.nbWorkers !=
					   GenerationRequest::control(generator.method, "workers").defaultValue;
		case 5:
			return noResourceGrowth;
		case 6:
			return resourceScarcity != 0;
		case 7:
			return instantConstruction;
		case 8:
			return stockpileStart != 0;
		case 9:
			return noHunger;
		case 10:
			return unitUpgradesDisabled;
		case 11:
			return glassCannonLevel != 0;
		case 12:
			return unitsFearless;
		case 13:
			return permadeathDisabled;
		case 14:
			return peacefulMode;
		case 15:
			return buildingHpLevel != 0;
		case 16:
			return random && startingUnitLevel != 0;
		case 17:
			return suddenDeathMinutes != 0;
		case 18:
			return winProbabilityPermille != 0;
		default:
			return false;
		}
	}
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
	// Confidence at which the fitted win probability model ends the game (0 = off,
	// the default -- a normal game plays exactly as before). Measured on the AI
	// tournament, 970 called the eventual winner in over 99% of the games it
	// ended; 950 ends more of them and is wrong rather more often.
	static constexpr std::array<int, 4> winProbabilityChoices = {0, 950, 970, 990};
	int winProbabilityPermille = 0;
	std::string format = "FFA", ruleset = "Standard";
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
	void presetRules(int preset)
	{
		prestige = preset != 3;
		revealed = preset == 2;
		locked = true;
		speed = preset == 1 ? 3 : 0;
		const auto &workerControl = GenerationRequest::control(generator.method, "workers");
		int workers = preset == 1 ? workerControl.maximum : workerControl.defaultValue;
		if (generator.nbWorkers != workers)
		{
			generator.nbWorkers = workers;
			++mapRevision;
		}
		noResourceGrowth = false;
		resourceScarcity = 0;
		instantConstruction = false;
		stockpileStart = 0;
		noHunger = false;
		unitUpgradesDisabled = false;
		glassCannonLevel = 0;
		unitsFearless = false;
		permadeathDisabled = false;
		peacefulMode = false;
		buildingHpLevel = 0;
		if (startingUnitLevel != 0)
		{
			startingUnitLevel = 0;
			++mapRevision;
		}
		suddenDeathMinutes = 0;
		winProbabilityPermille = 0;
		ruleset = preset == 0	? "Standard"
				  : preset == 1 ? "Quick clash"
				  : preset == 2 ? "Open book"
								: "Last colony standing";
	}
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
			return "Shared control needs a free controller slot (maximum 12).";
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
		std::optional<Uint32> winProbabilityThreshold;
		if (winProbabilityPermille != 0)
			winProbabilityThreshold = static_cast<Uint32>(winProbabilityPermille);
		WinningCondition::setWinProbabilityWinCondition(header.getWinningConditions(), winProbabilityThreshold);
	}
};
