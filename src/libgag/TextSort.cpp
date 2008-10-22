/*
  Copyright (C) 2001-2008 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "TextSort.h"

extern "C"
{
	#include "natsort/strnatcmp.c"
};

namespace GAGCore
{
	bool naturalStringSort(const std::string& lhs, const std::string& rhs)
	{
		int val = strnatcasecmp(lhs.c_str(), rhs.c_str());
		if(val == 1)
			return false;
		return true;
	}
}

