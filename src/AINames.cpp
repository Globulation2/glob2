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
		if(id == AI::NONE)
		{
			return Toolkit::getStringTable()->getString("[AINone]");
		}
		else if(id == AI::NUMBI)
		{
			return Toolkit::getStringTable()->getString("[AINumbi]");
		}
		else if(id == AI::CASTOR)
		{
			return Toolkit::getStringTable()->getString("[AICastor]");
		}
		else if(id == AI::WARRUSH)
		{
			return Toolkit::getStringTable()->getString("[AIWarrush]");
		}
		else if(id == AI::REACHTOINFINITY)
		{
			return Toolkit::getStringTable()->getString("[AIReachToInfinity]");
		}
		else if(id == AI::NICOWAR)
		{
			return Toolkit::getStringTable()->getString("[AINicowar]");
		}
		else if(id == AI::TOUBIB)
		{
			return Toolkit::getStringTable()->getString("[AIToubib]");
		}
		return "";
	}
}
