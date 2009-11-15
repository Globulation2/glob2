/*
  Copyright (C) 2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

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
