/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include <GUIStyle.h>
#include <GraphicContext.h>

using namespace GAGCore;

namespace GAGGUI
{
	Style defaultStyle;
	
	Style *Style::style = &defaultStyle;
	
	Style::Style()
	{
		textColor = highlightColor = Color(255, 255, 255);
		frameColor = Color(0, 200, 100);
		listSelectedElementColor = Color(170, 170, 240);
		backColor = Color(0, 0, 0);
		backOverlayColor = Color(0, 0, 40);
	}
	
	void Style::drawOnOffButton(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight, bool state)
	{
		drawFrame(target, x, y, w, h, highlight);
		if (state)
		{
			target->drawLine(x+(w/5)+1, y+(h/2), x+(w/2), y+4*(w/5)-1, 0, 255, 0);
			target->drawLine(x+(w/5), y+(h/2), x+(w/2), y+4*(w/5), 0, 255, 0);
			target->drawLine(x+(w/2), y+4*(w/5)-1, x+4*(w/5), y+(w/5), 0, 255, 0);
			target->drawLine(x+(w/2), y+4*(w/5), x+4*(w/5)-1, y+(w/5), 0, 255, 0);
		}
	}
	
	void Style::drawTextButtonBackground(DrawableSurface *target, int x, int y, int w, int h, unsigned highlight)
	{
		drawFrame(target, x, y, w, h, highlight);
	}
	
	void Style::drawFrame(DrawableSurface *target, int x, int y, int w, int h, unsigned highlight)
	{
		target->drawRect(x, y, w, h, frameColor);
		if (highlight > 0)
			target->drawRect(x+1, y+1, w-2, h-2, frameColor.applyAlpha(highlight));
	}
}
