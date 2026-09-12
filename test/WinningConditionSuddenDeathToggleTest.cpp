// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Standalone regression harness for WinningCondition::setSuddenDeathWinCondition.
// Unlike setPrestigeWinCondition (fixed default-order rank), a newly-enabled
// sudden-death timer must be appended at the very end of the list -- it is a
// last resort, evaluated only after every other condition has had its say
// that tick -- and retuning an already-present instance's tick must edit it
// in place rather than remove+re-add, so its list position (always the end)
// never moves.

#include "WinningConditions.h"

#include <cstdio>
#include <list>
#include <memory>
#include <optional>
#include <vector>

namespace
{
	int failures = 0;

	typedef std::list<std::shared_ptr<WinningCondition> > ConditionList;

	std::vector<WinningConditionType> typesOf(const ConditionList& conditions)
	{
		std::vector<WinningConditionType> types;
		for (ConditionList::const_iterator i = conditions.begin(); i != conditions.end(); ++i)
			types.push_back((*i)->getType());
		return types;
	}

	const char* typeName(WinningConditionType type)
	{
		switch (type)
		{
			case WCDeath: return "Death";
			case WCAllies: return "Allies";
			case WCPrestige: return "Prestige";
			case WCScript: return "Script";
			case WCOpponentsDefeated: return "OpponentsDefeated";
			case WCSuddenDeath: return "SuddenDeath";
			default: return "Unknown";
		}
	}

	void expectTypes(const char* what, const ConditionList& conditions,
	                 const std::vector<WinningConditionType>& expected)
	{
		const std::vector<WinningConditionType> actual = typesOf(conditions);
		if (actual == expected)
		{
			std::printf("PASS %s\n", what);
			return;
		}
		++failures;
		std::printf("FAIL %s\n  expected:", what);
		for (size_t i = 0; i < expected.size(); ++i)
			std::printf(" %s", typeName(expected[i]));
		std::printf("\n  actual:  ");
		for (size_t i = 0; i < actual.size(); ++i)
			std::printf(" %s", typeName(actual[i]));
		std::printf("\n");
	}

	void check(bool ok, const char* what)
	{
		std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
		if (!ok)
			++failures;
	}

	Uint32 tickOf(const ConditionList& conditions)
	{
		for (ConditionList::const_iterator i = conditions.begin(); i != conditions.end(); ++i)
			if ((*i)->getType() == WCSuddenDeath)
				return static_cast<WinningConditionSuddenDeath&>(**i).endStepTick;
		return 0;
	}
}

int main()
{
	const std::vector<WinningConditionType> defaultOrder =
		{ WCDeath, WCAllies, WCPrestige, WCScript, WCOpponentsDefeated };

	// Disabling when already absent is a no-op.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setSuddenDeathWinCondition(conditions, std::nullopt);
		expectTypes("disable when absent is a no-op", conditions, defaultOrder);
	}

	// Enabling appends at the very end, after every default condition --
	// unlike prestige, which inserts at a fixed default-order rank.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setSuddenDeathWinCondition(conditions, 90000u);
		expectTypes("enable appends at the end",
			conditions, { WCDeath, WCAllies, WCPrestige, WCScript, WCOpponentsDefeated, WCSuddenDeath });
		check(tickOf(conditions) == 90000u, "enable sets the configured tick");
	}

	// Retuning an already-present instance edits it in place: same position,
	// new tick -- not a remove-then-append, which would be a no-op on
	// position here but would matter if this ever stopped being last.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setSuddenDeathWinCondition(conditions, 90000u);
		WinningCondition::setSuddenDeathWinCondition(conditions, 45000u);
		expectTypes("retune keeps the same position",
			conditions, { WCDeath, WCAllies, WCPrestige, WCScript, WCOpponentsDefeated, WCSuddenDeath });
		check(tickOf(conditions) == 45000u, "retune updates the tick");
	}

	// Disabling removes exactly the sudden-death entry, preserving the rest.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setSuddenDeathWinCondition(conditions, 90000u);
		WinningCondition::setSuddenDeathWinCondition(conditions, std::nullopt);
		expectTypes("disable removes only sudden death", conditions, defaultOrder);
	}

	// Appending onto an already-non-default list still lands at the end.
	{
		ConditionList conditions;
		conditions.push_back(std::make_shared<WinningConditionOpponentsDefeated>());
		WinningCondition::setSuddenDeathWinCondition(conditions, 1000u);
		expectTypes("appended after a custom list",
			conditions, { WCOpponentsDefeated, WCSuddenDeath });
	}

	if (failures == 0)
		std::printf("all tests passed\n");
	return failures == 0 ? 0 : 1;
}
