// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "RoomSetup.h"

#include "CustomGameSetup.h"
#include "GenerationService.h"
#include "GeneratorDefinition.h"

#include <map>
#include <set>
#include <stdexcept>

namespace Online
{
namespace
{
std::vector<const GeneratorControl*> descriptorControls(int method)
{
	std::vector<const GeneratorControl*> controls;
	for (const auto& control : sharedGeneratorControls())
		controls.push_back(&control);
	if (const auto* definition = GeneratorRegistry::builtins().find(method))
		for (const auto& control : definition->controls)
			controls.push_back(&control);
	return controls;
}

int integer(const Json& value, const char* key, int fallback)
{
	if (!value.is_object() || !value.contains(key) || !value[key].is_number_integer())
		return fallback;
	return value[key].get<int>();
}

bool flag(const Json& value, const char* key, bool fallback)
{
	if (!value.is_object() || !value.contains(key) || !value[key].is_boolean())
		return fallback;
	return value[key].get<bool>();
}
} // namespace

const std::vector<std::string>& defaultRoomGenerators()
{
	// Fair by construction (exact symmetry, solved fairness or repeated wedges), and
	// verified at 128×128 with both 2 and 4 colonies (platform/packages/core/src/
	// queueConfig.ts FAIR_GENERATORS, fourColonies: true).
	static const std::vector<std::string> ids = {"even-ground", "symmetric-arena", "marchland", "carousel", "amphitheatre"};
	return ids;
}

CustomGameSetup defaultRoomSetup(int colonies, std::uint32_t pick)
{
	CustomGameSetup setup;
	const auto& registry = GeneratorRegistry::builtins();
	const auto& ids = defaultRoomGenerators();
	for (std::size_t attempt = 0; attempt < ids.size(); ++attempt)
	{
		try
		{
			const int method = registry.idOf(ids[(pick + attempt) % ids.size()]);
			setup.generator.setMethodDefaults(method);
			break;
		}
		catch (const std::exception&)
		{
			// Not in this build: try the next one; the constructor's default stays otherwise.
		}
	}
	setup.random = true;
	setup.generator.wDec = 7;
	setup.generator.hDec = 7;
	setup.generator.seed = 0;
	setup.generator.nbTeams = colonies;
	setup.setCapacity(colonies);
	setup.capacity = colonies;
	return setup;
}

Json generatorDescriptor(const CustomGameSetup& setup, std::uint32_t fallbackSeed)
{
	const auto* definition = GeneratorRegistry::builtins().find(setup.generator.method);
	if (!definition || definition->editorOnly || !definition->id)
		throw std::invalid_argument("this landscape cannot be generated online");
	GenerationRequest request = setup.generator;
	request.nbTeams = setup.capacity;
	Json params = Json::object();
	for (const auto* control : descriptorControls(request.method))
	{
		// Generator-specific controls missing from the draft take their defaults.
		if (control->group != ControlGroup::Shared && !request.options.count(control->id))
			request.options[control->id] = control->defaultValue;
		params[control->id] = control->get(request);
	}
	params["teams"] = setup.capacity;
	const std::uint32_t seed = request.seed ? request.seed : fallbackSeed;
	return Json{{"generatorId", definition->id},
				{"revision", definition->revision},
				{"params", params},
				{"seed", seed},
				{"candidates", GenerationService::kSampledCandidates},
				// The platform's generator command produces standard starting workers only.
				{"startingUnitLevel", 0}};
}

Json matchRules(const CustomGameSetup& s)
{
	return Json{{"prestigeVictory", s.prestige},
				{"suddenDeathMinutes", s.suddenDeathMinutes},
				{"mapDiscovered", s.revealed},
				{"allyTeamsFixed", s.locked},
				{"resourceGrowthDisabled", s.noResourceGrowth},
				{"resourceScarcityLevel", s.resourceScarcity},
				{"instantConstruction", s.instantConstruction},
				{"stockpileStartLevel", s.stockpileStart},
				{"hungerDisabled", s.noHunger},
				{"unitUpgradesDisabled", s.unitUpgradesDisabled},
				{"glassCannonLevel", s.glassCannonLevel},
				{"unitsFearless", s.unitsFearless},
				{"permadeathDisabled", s.permadeathDisabled},
				{"peacefulMode", s.peacefulMode},
				{"buildingHpLevel", s.buildingHpLevel}};
}

Json setupTeams(const CustomGameSetup& setup)
{
	// Alliances are renumbered densely in order of first appearance, as the
	// platform's SetupTeam rules expect (alliance < team count).
	std::map<int, int> dense;
	Json teams = Json::array();
	for (int i = 0; i < setup.capacity; ++i)
	{
		const int alliance = setup.colonies[std::size_t(i)].alliance;
		auto found = dense.find(alliance);
		if (found == dense.end())
			found = dense.emplace(alliance, int(dense.size())).first;
		teams.push_back(Json{{"team", i}, {"alliance", found->second}});
	}
	return teams;
}

void applyRulesToSetup(const Json& r, CustomGameSetup& s)
{
	s.prestige = flag(r, "prestigeVictory", s.prestige);
	s.suddenDeathMinutes = integer(r, "suddenDeathMinutes", s.suddenDeathMinutes);
	s.revealed = flag(r, "mapDiscovered", s.revealed);
	s.locked = flag(r, "allyTeamsFixed", s.locked);
	s.noResourceGrowth = flag(r, "resourceGrowthDisabled", s.noResourceGrowth);
	s.resourceScarcity = integer(r, "resourceScarcityLevel", s.resourceScarcity);
	s.instantConstruction = flag(r, "instantConstruction", s.instantConstruction);
	s.stockpileStart = integer(r, "stockpileStartLevel", s.stockpileStart);
	s.noHunger = flag(r, "hungerDisabled", s.noHunger);
	s.unitUpgradesDisabled = flag(r, "unitUpgradesDisabled", s.unitUpgradesDisabled);
	s.glassCannonLevel = integer(r, "glassCannonLevel", s.glassCannonLevel);
	s.unitsFearless = flag(r, "unitsFearless", s.unitsFearless);
	s.permadeathDisabled = flag(r, "permadeathDisabled", s.permadeathDisabled);
	s.peacefulMode = flag(r, "peacefulMode", s.peacefulMode);
	s.buildingHpLevel = integer(r, "buildingHpLevel", s.buildingHpLevel);
	s.ruleset = rulesetName(s);
}

bool applyRoomToSetup(const Json& room, CustomGameSetup& setup)
{
	if (room.contains("rules"))
		applyRulesToSetup(room["rules"], setup);
	bool known = false;
	if (room.contains("map") && room["map"].is_object() && room["map"].value("kind", "") == "generated" &&
		room["map"].contains("generator"))
	{
		const Json& g = room["map"]["generator"];
		const int method = GeneratorRegistry::builtins().idOf(g.value("generatorId", ""));
		if (GeneratorRegistry::builtins().find(method))
		{
			GenerationRequest request = setup.generator;
			request.setMethodDefaults(method);
			if (g.contains("params") && g["params"].is_object())
				for (const auto* control : descriptorControls(method))
					if (g["params"].contains(control->id) && g["params"][control->id].is_number_integer())
						control->set(request, g["params"][control->id].get<int>());
			request.seed = g.value("seed", 0u);
			setup.random = true;
			setup.generator = request;
			setup.startingUnitLevel = 0;
			known = true;
		}
	}
	if (room.contains("teams") && room["teams"].is_array() && !room["teams"].empty())
	{
		const int count = std::min<int>(int(room["teams"].size()), Team::MAX_COUNT);
		setup.capacity = count;
		setup.generator.nbTeams = count;
		for (const auto& team : room["teams"])
		{
			const int index = integer(team, "team", -1);
			if (index >= 0 && index < count)
				setup.colonies[std::size_t(index)].alliance = integer(team, "alliance", index);
		}
		setup.format = formatName(room["teams"]);
	}
	return known;
}

std::string rulesetName(const CustomGameSetup& setup)
{
	static const char* names[] = {"Standard", "Quick clash", "Open book", "Last colony standing"};
	for (int preset = 0; preset < 4; ++preset)
	{
		CustomGameSetup reference = setup;
		reference.presetRules(preset);
		// The room carries no game speed or worker count; compare the rules alone.
		if (reference.prestige == setup.prestige && reference.revealed == setup.revealed &&
			reference.locked == setup.locked && reference.noResourceGrowth == setup.noResourceGrowth &&
			reference.resourceScarcity == setup.resourceScarcity &&
			reference.instantConstruction == setup.instantConstruction &&
			reference.stockpileStart == setup.stockpileStart && reference.noHunger == setup.noHunger &&
			reference.unitUpgradesDisabled == setup.unitUpgradesDisabled &&
			reference.glassCannonLevel == setup.glassCannonLevel &&
			reference.unitsFearless == setup.unitsFearless &&
			reference.permadeathDisabled == setup.permadeathDisabled &&
			reference.peacefulMode == setup.peacefulMode && reference.buildingHpLevel == setup.buildingHpLevel &&
			reference.suddenDeathMinutes == setup.suddenDeathMinutes &&
			// Quick clash differs from Standard only by speed and workers.
			preset != 1)
			return names[preset];
	}
	return "Custom rules";
}

std::string formatName(const Json& teams)
{
	if (!teams.is_array() || teams.empty())
		return "FFA";
	std::map<int, int> sizes;
	for (const auto& team : teams)
		++sizes[integer(team, "alliance", integer(team, "team", 0))];
	if (sizes.size() == teams.size())
		return "FFA";
	if (sizes.size() == 2 && sizes.begin()->second == 2 && sizes.rbegin()->second == 2)
		return "2 vs 2";
	if (sizes.size() == 2 && sizes.begin()->second == sizes.rbegin()->second)
		return std::to_string(sizes.begin()->second) + " vs " + std::to_string(sizes.begin()->second);
	return "Custom teams";
}
} // namespace Online
