/*
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

#include "Ressource.h"
#include "StringTable.h"
#include "Toolkit.h"

using namespace GAGCore;

std::string getResourceName(int type)
{
	if(type == WOOD)
		return Toolkit::getStringTable()->getString("[Wood]");
	if(type == CORN)
		return Toolkit::getStringTable()->getString("[Wheat]");
	if(type == PAPYRUS)
		return Toolkit::getStringTable()->getString("[Papyrus]");
	if(type == STONE)
		return Toolkit::getStringTable()->getString("[Stone]");
	if(type == ALGA)
		return Toolkit::getStringTable()->getString("[Alga]");
	if(type == CHERRY)
		return Toolkit::getStringTable()->getString("[Cherry]");
	if(type == ORANGE)
		return Toolkit::getStringTable()->getString("[Orange]");
	if(type == PRUNE)
		return Toolkit::getStringTable()->getString("[Prune]");
	return "";
}
