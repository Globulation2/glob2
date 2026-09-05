// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

// Standalone regression harness for WinningCondition::setPrestigeWinCondition,
// the in-place prestige toggle used by CustomGameOtherOptions. The old code
// reset the whole list to getDefaultWinningConditions() before maybe erasing
// prestige, silently discarding any other customized conditions. The helper
// must instead edit only the prestige entry, and — because
// Team::checkWinConditions stops at the first condition that declares a
// win/loss — re-insert it at its default-order evaluation position rather
// than appending. Pre-fix tree fails to LINK (the helper does not exist).

#include "WinningConditions.h"

#include <cstdio>
#include <list>
#include <memory>
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
}

int main()
{
	const std::vector<WinningConditionType> defaultOrder =
		{ WCDeath, WCAllies, WCPrestige, WCScript, WCOpponentsDefeated };
	const std::vector<WinningConditionType> defaultWithoutPrestige =
		{ WCDeath, WCAllies, WCScript, WCOpponentsDefeated };

	// Sanity: the assumed default order matches getDefaultWinningConditions.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		expectTypes("default order sanity", conditions, defaultOrder);
	}

	// Disabling removes exactly the prestige entry, preserving the rest.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setPrestigeWinCondition(conditions, false);
		expectTypes("disable removes only prestige", conditions, defaultWithoutPrestige);
	}

	// Disabling when already absent is a no-op.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setPrestigeWinCondition(conditions, false);
		WinningCondition::setPrestigeWinCondition(conditions, false);
		expectTypes("disable twice is idempotent", conditions, defaultWithoutPrestige);
	}

	// Re-enabling restores the exact default evaluation order.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setPrestigeWinCondition(conditions, false);
		WinningCondition::setPrestigeWinCondition(conditions, true);
		expectTypes("disable then enable restores default order", conditions, defaultOrder);
	}

	// Enabling when already present is a no-op (no duplicate).
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		WinningCondition::setPrestigeWinCondition(conditions, true);
		expectTypes("enable when present is idempotent", conditions, defaultOrder);
	}

	// The regression this harness exists for: other customizations survive
	// the toggle. A list missing Script must not get Script back.
	{
		ConditionList conditions = WinningCondition::getDefaultWinningConditions();
		conditions.remove_if([](const std::shared_ptr<WinningCondition>& c)
			{ return c->getType() == WCScript; });
		WinningCondition::setPrestigeWinCondition(conditions, false);
		WinningCondition::setPrestigeWinCondition(conditions, true);
		expectTypes("custom list survives toggle",
			conditions, { WCDeath, WCAllies, WCPrestige, WCOpponentsDefeated });
	}

	// Insertion position: prestige goes before the first condition that
	// follows it in default order, even when earlier default entries are gone.
	{
		ConditionList conditions;
		conditions.push_back(std::make_shared<WinningConditionOpponentsDefeated>());
		WinningCondition::setPrestigeWinCondition(conditions, true);
		expectTypes("inserted before later-ranked condition",
			conditions, { WCPrestige, WCOpponentsDefeated });
	}

	// ...and appends when every remaining condition precedes it.
	{
		ConditionList conditions;
		conditions.push_back(std::make_shared<WinningConditionDeath>());
		conditions.push_back(std::make_shared<WinningConditionAllies>());
		WinningCondition::setPrestigeWinCondition(conditions, true);
		expectTypes("appended after earlier-ranked conditions",
			conditions, { WCDeath, WCAllies, WCPrestige });
	}

	if (failures == 0)
		std::printf("all tests passed\n");
	return failures == 0 ? 0 : 1;
}
