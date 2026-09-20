// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>

#include "AI.h"
#include "AINames.h"
#include "Toolkit.h"
#include "StringTable.h"

using namespace GAGCore;

namespace AINames
{
	namespace
	{
		// Single source of truth for the AI roster, in display order — one
		// row per AI::ImplementationID. Everything user-facing about an AI's
		// identity is derived from here, so the CLI parsers, their error/help
		// text, and the localized UI labels can never drift apart.
		//   cliName   — lowercase name accepted by --ai-types/--matchup, or
		//               nullptr when the AI can't be picked from the CLI (NONE).
		//   stringKey — StringTable base key: the display name is "[<key>]"
		//               and the description "[<key>-Description]".
		//   strength — combined Elo from the completed 20,000-game duel tournament.
		// All games use random 128x128 maps across 60 generators, with probability
		// victory disabled. Ratings fit all outcomes equally (Bradley-Terry),
		// are centred on 1500, and pool builds of the same source revision.
		// Displayed values are rounded once, after the full tournament.
		// Difficulty tiers: below 1450 Easy, 1450-1699 Medium, 1700+ Hard.
		// These describe this duel cohort, not every map, format or human game.
		// See docs/ai-strength.md and docs/ai/ratings.md.
		const struct { int id; const char* cliName; const char* stringKey; const char* difficulty; int strength; } aiTable[] = {
			{AI::NONE,            nullptr,           "AINone", "No AI orders", 0},
			{AI::NUMBI,           "numbi",           "AINumbi", "Easy", 1204},
			{AI::CASTOR,          "castor",          "AICastor", "Easy", 1280},
			{AI::WARRUSH,         "warrush",         "AIWarrush", "Easy", 1401},
			{AI::ECONO, "econo", "AIEcono", "Easy", 1310},
			{AI::NICOWAR,         "nicowar",         "AINicowar", "Medium", 1653},
			{AI::MAXIMA,          "maxima",          "AIMaxima", "Hard", 1873},
			{AI::CORTEX,          "cortex",          "AICortex", "Medium", 1601},
			{AI::CABINO,          "cabino",          "AICabino", "Medium", 1680},
		};
	}

	const std::vector<int>& selectionOrder()
	{
		// Weakest first, by measured strength rather than by guess, so the list
		// a player scrolls reads as a ladder.
		static const std::vector<int> order = {AI::NUMBI, AI::CASTOR, AI::ECONO, AI::WARRUSH, AI::CORTEX, AI::NICOWAR, AI::CABINO, AI::MAXIMA, AI::NONE};
		return order;
	}
	int selectionIndex(int id)
	{
		const auto& order = selectionOrder();
		return int(std::find(order.begin(), order.end(), id) - order.begin());
	}

	int getAIStrength(int id)
	{
		for (const auto& entry : aiTable)
			if (entry.id == id) return entry.strength;
		return 0;
	}

	std::string getCLIName(int id)
	{
		for (const auto& entry : aiTable)
			if (entry.id == id) return entry.cliName ? entry.cliName : "none";
		return "unknown";
	}

	std::string getAIText(int id)
	{
		for (const auto& entry : aiTable)
			if (entry.id == id)
				return Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "]");
		return "unknown AI";
	}

	std::string getAIDescription(int id)
	{
		for (const auto& entry : aiTable)
			if (entry.id == id)
				return Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "-Description]");
		return "unknown AI";
	}

    std::string getAISelectorText(int id)
    {
        for (const auto& entry : aiTable)
            if (entry.id == id)
                return (id == AI::NONE ? Toolkit::getStringTable()->getString("[Inactive]") : getAIText(id))
                    + std::string(" - ") + Toolkit::getStringTable()->getString("[" + std::string(entry.difficulty) + "]")
                    // The number behind the label, so a player can see that two
                    // AIs sharing one can still be far apart.
                    + (entry.strength ? " (" + std::to_string(entry.strength) + ")" : "")
                    + (id == AI::ECONO ? std::string(" - ") + Toolkit::getStringTable()->getString("[No warriors]") : "");
        return "unknown AI";
    }
    std::string getAISummary(int id)
    {
        for (const auto& entry : aiTable)
            if (entry.id == id)
                return Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "-Summary]");
        return "unknown AI";
    }
    std::string getAIProfile(int id)
    {
        for (const auto& entry : aiTable)
            if (entry.id == id)
                {
                    std::string profile=Toolkit::getStringTable()->getString("[" + std::string(entry.stringKey) + "-Profile]");
                    for(size_t p=0;(p=profile.find("\\n",p))!=std::string::npos;++p)profile.replace(p,2,"\n");
                    return getAISelectorText(id)+"\n\n"+getAISummary(id)+"\n\n"+profile;
                }
        return "unknown AI";
    }

	int parseAIName(const std::string& name)
	{
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		for (const auto& entry : aiTable)
		{
			if (entry.cliName && lower == entry.cliName)
				return entry.id;
		}
		return AI_UNKNOWN_NAME;
	}

	std::string validAINames()
	{
		std::string list;
		for (const auto& entry : aiTable)
		{
			if (!entry.cliName)
				continue;
			if (!list.empty())
				list += ", ";
			list += entry.cliName;
		}
		return list;
	}
}
