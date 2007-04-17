/*
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __GLOB2_SCREEN_H
#define __GLOB2_SCREEN_H

#include <GUIBase.h>

using namespace GAGCore;
using namespace GAGGUI;

class Glob2Screen : public Screen
{
public:
	Glob2Screen();
	virtual ~Glob2Screen();
	virtual void paint(void);
	
private:
	unsigned getNextTerrain(void);
	int randomSeed;
};

#endif

