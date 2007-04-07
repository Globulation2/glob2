/*
  Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière
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
		enum StyleMetrics
		{
			STYLE_METRIC_FRAME_TOP_HEIGHT = 0,
			STYLE_METRIC_FRAME_LEFT_WIDTH,
			STYLE_METRIC_FRAME_RIGHT_WIDTH,
			STYLE_METRIC_FRAME_BOTTOM_HEIGHT,
			STYLE_METRIC_LIST_SCROLLBAR_WIDTH,
			STYLE_METRIC_LIST_SCROLLBAR_TOP_WIDTH,
			STYLE_METRIC_LIST_SCROLLBAR_BOTTOM_WIDTH
		};
		
		GAGCore::Color textColor; //!< color of text
		GAGCore::Color highlightColor; //!< color of highlighted elements
		GAGCore::Color frameColor; //!< base color of frames
		GAGCore::Color listSelectedElementColor;
		GAGCore::Color backColor; //!< background color
		GAGCore::Color backOverlayColor; //!< overlay background color
		
		static Style *style;
		
	public:
		Style();
		virtual ~Style() { }
		virtual void drawOnOffButton(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight, bool state);
		virtual void drawTextButtonBackground(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
		virtual void drawFrame(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
		virtual void drawScrollBar(GAGCore::DrawableSurface *target, int x, int y, int w, int h, int blockPos, int blockLength);
		
		virtual int getStyleMetric(StyleMetrics metric);
	};
	
	extern Style defaultStyle;
}

#endif
