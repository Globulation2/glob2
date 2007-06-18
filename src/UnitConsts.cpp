/*
  Copyright (C) Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Toolkit.h"
#include "StringTable.h"

#include "UnitConsts.h"

using namespace GAGCore;

std::string getUnitName(int type)
{
	if(type == WORKER)
		return Toolkit::getStringTable()->getString("[Worker]");
	if(type == WARRIOR)
		return Toolkit::getStringTable()->getString("[Warrior]");
	if(type == EXPLORER)
		return Toolkit::getStringTable()->getString("[Explorer]");
}
