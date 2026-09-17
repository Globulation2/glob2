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
		//   strength  — measured, not judged: see the note below. 0 for NONE.
		//
		// Difficulty and strength both come from a tournament of 2,294 randomly
		// drawn games covering every AI, all three formats and every map generator
		// and size, rated by maximum likelihood over the finishing orders
		// (tools/tournaments_ai_leaderboard.py). Strength is on Elo's scale --
		// 400 points is a factor of ten in the odds -- averaged over the formats:
		//
		//   nicowar 1688 | maxima 1663 | cabino 1648 || cortex 1538 ||
		//   econo 1410 | castor 1386 | warrush 1371 | numbi 1295
		//
		// The two widest gaps in that ladder are the 110 points below cabino and
		// the 128 below cortex, and those are where the labels are cut, which is
		// why Medium holds one AI and Easy four. Earlier labels were assigned by
		// judgement and had cabino, castor and warrush all as Medium; the games
		// put cabino among the strongest and the other two near the bottom.
		const struct { int id; const char* cliName; const char* stringKey; const char* difficulty; int strength; } aiTable[] = {
			{AI::NONE,            nullptr,           "AINone", "No AI orders", 0},
			{AI::NUMBI,           "numbi",           "AINumbi", "Easy", 1295},
			{AI::CASTOR,          "castor",          "AICastor", "Easy", 1386},
			{AI::WARRUSH,         "warrush",         "AIWarrush", "Easy", 1371},
			{AI::ECONO, "econo", "AIEcono", "Easy", 1410},
			{AI::NICOWAR,         "nicowar",         "AINicowar", "Hard", 1688},
			{AI::MAXIMA,          "maxima",          "AIMaxima", "Hard", 1663},
			{AI::CORTEX,          "cortex",          "AICortex", "Medium", 1538},
			{AI::CABINO,          "cabino",          "AICabino", "Hard", 1648},
		};
	}

	const std::vector<int>& selectionOrder()
	{
		// Weakest first, by measured strength rather than by guess, so the list
		// a player scrolls reads as a ladder.
		static const std::vector<int> order = {AI::NUMBI, AI::WARRUSH, AI::CASTOR, AI::ECONO, AI::CORTEX, AI::CABINO, AI::MAXIMA, AI::NICOWAR, AI::NONE};
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
