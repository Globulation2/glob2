// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GameHeader.h"
#include "WinningConditions.h"
#include "ExperimentalFeatures.h"
#include <cerrno>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Runtime rule overrides shared by both headless entry points. Stable names are
// also exposed to scripts and recorded with tournament results for reproduction.
inline void applyGameRule(GameHeader& header, const std::string& item)
{
	struct Rule
	{
		const char* name;
		long maximum;
		std::function<void(GameHeader&, int)> apply;
	};
	std::vector<Rule> rules = {
		{"noGrowth", 1, [](GameHeader& h, int v) { h.setResourceGrowthDisabled(v); }},
		{"scarcity", 3, [](GameHeader& h, int v) { h.setResourceScarcityLevel(v); }},
		{"instantConstruction", 1, [](GameHeader& h, int v) { h.setInstantConstructionEnabled(v); }},
		{"stockpile", 3, [](GameHeader& h, int v) { h.setStockpileStartLevel(v); }},
		{"noHunger", 1, [](GameHeader& h, int v) { h.setHungerDisabled(v); }},
		{"noUpgrades", 1, [](GameHeader& h, int v) { h.setUnitUpgradesDisabled(v); }},
		{"glassCannon", 2, [](GameHeader& h, int v) { h.setGlassCannonLevel(v); }},
		{"fearless", 1, [](GameHeader& h, int v) { h.setUnitsFearless(v); }},
		{"noPermadeath", 1, [](GameHeader& h, int v) { h.setPermadeathDisabled(v); }},
		{"peaceful", 1, [](GameHeader& h, int v) { h.setPeacefulModeEnabled(v); }},
		{"fortress", 2, [](GameHeader& h, int v) { h.setBuildingHpLevel(v); }},
		{"suddenDeathTick", 100000000, [](GameHeader& h, int v)
		{
			WinningCondition::setSuddenDeathWinCondition(h.getWinningConditions(),
			v ? std::optional<Uint32>(v) : std::nullopt);
		}},
		{"winProbabilityPermille", 1000, [](GameHeader& h, int v)
		{
			WinningCondition::setWinProbabilityWinCondition(h.getWinningConditions(),
			v ? std::optional<Uint32>(v) : std::nullopt);
		}},
	};
	// One 0/1 rule per experiment, named by its key (ExperimentalFeatures.cpp).
	for (const auto& definition : experimentDefinitions())
		rules.push_back({definition.key, 1, [id = definition.id](GameHeader& h, int v) { h.getExperiments().set(id, v != 0); }});
    const size_t equals=item.find('=');
    const std::string name=item.substr(0, equals);
    const Rule* rule=nullptr;
    for (const auto& candidate:rules) if (name==candidate.name) rule=&candidate;
    char* end=nullptr;
    errno=0;
    const long value=equals==std::string::npos ? -1 : strtol(item.c_str()+equals+1, &end, 10);
    if (!rule || equals==std::string::npos || errno || *end || end==item.c_str()+equals+1
        || value<0 || value>rule->maximum
        || (name=="winProbabilityPermille" && value!=0 && value<501))
        throw std::invalid_argument("invalid game rule: "+item);
    rule->apply(header, int(value));
}

inline std::vector<std::pair<std::string, int>> gameRuleValues(const GameHeader& h)
{
    int timer=0, probability=0;
    for (const auto& c:h.getWinningConditions())
    {
        if(c->getType()==WCSuddenDeath) timer=int(static_cast<const WinningConditionSuddenDeath&>(*c).endStepTick);
        if(c->getType()==WCWinProbability) probability=int(static_cast<const WinningConditionWinProbability&>(*c).thresholdPermille);
    }
    return {{"noGrowth", h.isResourceGrowthDisabled()}, {"scarcity", h.getResourceScarcityLevel()},
        {"instantConstruction", h.isInstantConstructionEnabled()}, {"stockpile", h.getStockpileStartLevel()},
        {"noHunger", h.isHungerDisabled()}, {"noUpgrades", h.isUnitUpgradesDisabled()},
        {"glassCannon", h.getGlassCannonLevel()}, {"fearless", h.isUnitsFearless()},
        {"noPermadeath", h.isPermadeathDisabled()}, {"peaceful", h.isPeacefulModeEnabled()},
        {"fortress", h.getBuildingHpLevel()}, {"suddenDeathTick", timer}, {"winProbabilityPermille", probability}};
}
