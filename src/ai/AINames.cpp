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
	std::string getAIText(int id)
	{
		std::string sAi;
		switch(id)
		{
		case AI::NONE: sAi="[AINone]";
			break;
		case AI::NUMBI: sAi="[AINumbi]";
			break;
		case AI::CASTOR: sAi="[AICastor]";
			break;
		case AI::WARRUSH: sAi="[AIWarrush]";
			break;
		case AI::REACHTOINFINITY: sAi="[AIReachToInfinity]";
			break;
		case AI::NICOWAR: sAi="[AINicowar]";
			break;
		default:
			return "unknown AI";
		}
		return Toolkit::getStringTable()->getString(sAi);
	}
	
	std::string getAIDescription(int id)
	{
		std::string sAi;
		switch(id)
		{
		case AI::NONE: sAi="[AINone-Description]";
			break;
		case AI::NUMBI: sAi="[AINumbi-Description]";
			break;
		case AI::CASTOR: sAi="[AICastor-Description]";
			break;
		case AI::WARRUSH: sAi="[AIWarrush-Description]";
			break;
		case AI::REACHTOINFINITY: sAi="[AIReachToInfinity-Description]";
			break;
		case AI::NICOWAR: sAi="[AINicowar-Description]";
			break;
		default:
			return "unknown AI";
		}
		return Toolkit::getStringTable()->getString(sAi);
	}

	int parseAIName(const std::string& name)
	{
		// Single source of truth for CLI-friendly AI names. Both
		// --ai-types and --matchup parsers in GlobalContainer.cpp
		// use this to avoid drift.
		static const struct { const char* name; int id; } table[] = {
			{"numbi",           AI::NUMBI},
			{"castor",          AI::CASTOR},
			{"warrush",         AI::WARRUSH},
			{"reachtoinfinity", AI::REACHTOINFINITY},
			{"nicowar",         AI::NICOWAR},
		};
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++)
		{
			if (lower == table[i].name)
				return table[i].id;
		}
		return AI_UNKNOWN_NAME;
	}
}
