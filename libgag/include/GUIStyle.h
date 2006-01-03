/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charrière
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

#ifndef __GUISTYLE_H
#define __GUISTYLE_H

#include "GraphicContext.h"

namespace GAGGUI
{
	class Style
	{
	public:
		static GAGCore::Color frontColor;
		static GAGCore::Color frontFrameColor;
		static GAGCore::Color listSelectedElementColor;
		static GAGCore::Color backColor;
		static GAGCore::Color backOverlayColor;
		static Style *style;
		
	public:
		virtual ~Style() { }
		virtual void drawOnOffButton(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight, bool state);
		virtual void drawTextButtonBackground(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
		virtual void drawFrame(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
	};
	
	extern Style defaultStyle;
}

#endif
