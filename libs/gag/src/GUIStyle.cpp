// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
		backColor = Color(0, 0, 0);
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
	
		
	void Style::drawProgressBar(GAGCore::DrawableSurface *target, int x, int y, int w, int value, int range)
	{
		int h = getStyleMetric(STYLE_METRIC_PROGRESS_BAR_HEIGHT);
		drawFrame(target, x, y, w, h, Color::ALPHA_OPAQUE);
		x += getStyleMetric(STYLE_METRIC_FRAME_LEFT_WIDTH);
		y += getStyleMetric(STYLE_METRIC_FRAME_TOP_HEIGHT);
		w -= getStyleMetric(STYLE_METRIC_FRAME_LEFT_WIDTH) + getStyleMetric(STYLE_METRIC_FRAME_RIGHT_WIDTH);
		h -= getStyleMetric(STYLE_METRIC_FRAME_TOP_HEIGHT) + getStyleMetric(STYLE_METRIC_FRAME_BOTTOM_HEIGHT);
		int len = (value*w)/range;
		target->drawFilledRect(x, y, len, h, highlightColor);
	}
	
	int Style::getStyleMetric(StyleMetrics metric)
	{
		switch (metric)
		{
			case STYLE_METRIC_FRAME_TOP_HEIGHT: return 1;
			case STYLE_METRIC_FRAME_LEFT_WIDTH: return 1;
			case STYLE_METRIC_FRAME_RIGHT_WIDTH: return 1;
			case STYLE_METRIC_FRAME_BOTTOM_HEIGHT: return 1;
			case STYLE_METRIC_LIST_SCROLLBAR_WIDTH: return 22;
			case STYLE_METRIC_LIST_SCROLLBAR_TOP_WIDTH: return 22;
			case STYLE_METRIC_LIST_SCROLLBAR_BOTTOM_WIDTH: return 22;
			case STYLE_METRIC_PROGRESS_BAR_HEIGHT:return 22;
			default: return 0;
		}
	}
}
