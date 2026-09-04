// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "AI.h"
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
		case AI::TOUBIB: sAi="[AIToubib]";
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
		case AI::TOUBIB: sAi="[AIToubib-Description]";
			break;
		default:
			return "unknown AI";
		}
		return Toolkit::getStringTable()->getString(sAi);
	}
}
