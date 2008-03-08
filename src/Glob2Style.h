/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __GLOB2_STYLE_H
#define __GLOB2_STYLE_H

#include <GUIStyle.h>

using namespace GAGCore;
using namespace GAGGUI;

class Glob2Style : public Style
{
public:
	Glob2Style();
	~Glob2Style();

protected:
	virtual void drawTextButtonBackground(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
	virtual void drawFrame(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
	virtual void drawScrollBar(GAGCore::DrawableSurface *target, int x, int y, int w, int h, int blockPos, int blockLength);
	
	virtual int getStyleMetric(StyleMetrics metric);
	
protected:
	Sprite *sprite;
};

#endif
