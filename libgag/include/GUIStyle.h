// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2007 Stephane Magnenat & Luc-Olivier de Charrière

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
			STYLE_METRIC_LIST_SCROLLBAR_BOTTOM_WIDTH,
			STYLE_METRIC_PROGRESS_BAR_HEIGHT
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
		virtual void drawTriButton(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight, Uint8 state);
		virtual void drawTextButtonBackground(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
		virtual void drawFrame(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
		virtual void drawScrollBar(GAGCore::DrawableSurface *target, int x, int y, int w, int h, int blockPos, int blockLength);
		virtual void drawProgressBar(GAGCore::DrawableSurface *target, int x, int y, int w, int value, int range);
		
		virtual int getStyleMetric(StyleMetrics metric);
	};
	
	extern Style defaultStyle;
}

#endif
