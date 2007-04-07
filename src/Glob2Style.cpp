/*
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "Glob2Style.h"
#include "GlobalContainer.h"

Glob2Style::Glob2Style()
{
	textColor = Color(255, 255, 255);
	highlightColor = Color(197, 67, 67);
	frameColor = Color(226, 208, 148);
	listSelectedElementColor = Color(170, 170, 240);
	backColor = Color(0, 0, 0);
	backOverlayColor = Color(0, 0, 40);
	
	sprite = Toolkit::getSprite("data/gfx/guitheme");
};

Glob2Style::~Glob2Style()
{
	Toolkit::releaseSprite("data/gfx/guitheme");
}

void Glob2Style::drawTextButtonBackground(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight)
{
	// big buttons
	if (h == 40)
	{
		int ocrX, ocrY, ocrW, ocrH;
		
		// base of buttons
		target->drawSprite(x, y, sprite, 0);
		
		target->getClipRect(&ocrX, &ocrY, &ocrW, &ocrH);
		target->setClipRect(x+20, y, w-40, 40);
		for (int i = 0; i < w-40; i += 40)
			target->drawSprite(x+20+i, y, sprite, 2);
		target->setClipRect(ocrX, ocrY, ocrW, ocrH);
		
		target->drawSprite(x+w-20, y, sprite, 4);
		
		// hightlight of buttons
		if (highlight > 0)
		{
			target->drawSprite(x, y, sprite, 1, highlight);
		
			target->getClipRect(&ocrX, &ocrY, &ocrW, &ocrH);
			target->setClipRect(x+20, y, w-40, 40);
			for (int i = 0; i < w-40; i += 40)
				target->drawSprite(x+20+i, y, sprite, 3, highlight);
			target->setClipRect(ocrX, ocrY, ocrW, ocrH);
			
			target->drawSprite(x+w-20, y, sprite, 5, highlight);
		}
	}
	// small buttons
	else if (h == 20)
	{
		int ocrX, ocrY, ocrW, ocrH;
		
		// base of buttons
		target->drawSprite(x, y, sprite, 6);
		
		target->getClipRect(&ocrX, &ocrY, &ocrW, &ocrH);
		target->setClipRect(x+10, y, w-20, 20);
		for (int i = 0; i < w-20; i += 20)
			target->drawSprite(x+10+i, y, sprite, 8);
		target->setClipRect(ocrX, ocrY, ocrW, ocrH);
		
		target->drawSprite(x+w-10, y, sprite, 10);
		
		// hightlight of buttons
		if (highlight > 0)
		{
			target->drawSprite(x, y, sprite, 7, highlight);
		
			target->getClipRect(&ocrX, &ocrY, &ocrW, &ocrH);
			target->setClipRect(x+10, y, w-20, 20);
			for (int i = 0; i < w-20; i += 20)
				target->drawSprite(x+10+i, y, sprite, 9, highlight);
			target->setClipRect(ocrX, ocrY, ocrW, ocrH);
			
			target->drawSprite(x+w-10, y, sprite, 11, highlight);
		}
	}
	else
		Style::drawTextButtonBackground(target, x, y, w, h, highlight);
}

void Glob2Style::drawFrame(DrawableSurface *target, int x, int y, int w, int h, unsigned highlight)
{
	/*
		Sprites index are:
		
		12			13			14
		15						16
		17			18			19
		
		Width of sprites 13 and 18 must be the same
		Width of sprites 12, 15 and 17 must be the same
		Width of sprites 14, 16 and 19 must be the same
		Height of sprites 15 and 16 must be the same
		Height of sprites 12, 13 and 14 must be the same
		Height of sprites 17, 18 and 19 must be the same
	*/
	
	// save cliprect
	int ocrX, ocrY, ocrW, ocrH;
	target->getClipRect(&ocrX, &ocrY, &ocrW, &ocrH);
	
	// top, bottom
	int contentX = x + sprite->getW(12);
	int contentWidth = w - sprite->getW(12) - sprite->getW(14);
	target->setClipRect(contentX, y, contentWidth, h);
	for (int i = 0; i < contentWidth; i += sprite->getW(13))
	{
		target->drawSprite(contentX + i, y, sprite, 13);
		target->drawSprite(contentX + i, y + h - sprite->getH(18), sprite, 18);
	}
	
	// left, right
	int contentY = y + sprite->getH(12);
	int contentHeight = h - sprite->getH(12) - sprite->getH(17);
	target->setClipRect(x, contentY, w, contentHeight);
	for (int i = 0; i < contentHeight; i += sprite->getH(15))
	{
		target->drawSprite(x, contentY + i, sprite, 15);
		target->drawSprite(x + w - sprite->getW(16), contentY + i, sprite, 16);
	}
	
	// reset cliprect
	target->setClipRect(ocrX, ocrY, ocrW, ocrH);
	
	// corners
	target->drawSprite(x, y, sprite, 12);
	target->drawSprite(x + w - sprite->getW(14), y, sprite, 14);
	target->drawSprite(x, y + h - sprite->getH(17), sprite, 17);
	target->drawSprite(x + w - sprite->getW(19), y + h - sprite->getH(19), sprite, 19);
}

void Glob2Style::drawScrollBar(GAGCore::DrawableSurface *target, int x, int y, int w, int h, int blockPos, int blockLength)
{
	/*
		Width of sprites 20, 21 and 22 must be the same
	*/
	
	target->drawSprite(x, y, sprite, 20);
	target->drawSprite(x, y + h - sprite->getH(22), sprite, 22);
	
	// save cliprect
	int ocrX, ocrY, ocrW, ocrH;
	target->getClipRect(&ocrX, &ocrY, &ocrW, &ocrH);
	
	// scroll background
	int barBackgroundY = y + sprite->getH(20);
	int barBackgroundHeight = h - sprite->getH(20) - sprite->getH(22);
	target->setClipRect(x, barBackgroundY, w, barBackgroundHeight);
	for (int i = 0; i < barBackgroundHeight; i += sprite->getH(21))
		target->drawSprite(x, barBackgroundY + i, sprite, 21);
	
	// scroll bar
	int barForegroundY = barBackgroundY + blockPos;
	int barForegroundHeight = blockLength;
	target->setClipRect(x, barForegroundY, w, barForegroundHeight);
	for (int i = 0; i < barForegroundHeight; i += sprite->getH(23))
		target->drawSprite(x + (sprite->getW(21) - sprite->getW(23)) / 2, barForegroundY + i, sprite, 23);
	
	// reset cliprect
	target->setClipRect(ocrX, ocrY, ocrW, ocrH);
}

int Glob2Style::getStyleMetric(StyleMetrics metric)
{
	switch (metric)
	{
		case STYLE_METRIC_FRAME_TOP_HEIGHT: return sprite->getH(12);
		case STYLE_METRIC_FRAME_LEFT_WIDTH: return sprite->getW(12);
		case STYLE_METRIC_FRAME_RIGHT_WIDTH: return sprite->getW(14);
		case STYLE_METRIC_FRAME_BOTTOM_HEIGHT: return sprite->getH(17);
		case STYLE_METRIC_LIST_SCROLLBAR_WIDTH: return sprite->getW(20);
		case STYLE_METRIC_LIST_SCROLLBAR_TOP_WIDTH: return sprite->getH(20);
		case STYLE_METRIC_LIST_SCROLLBAR_BOTTOM_WIDTH: return sprite->getH(22);
		default: return 0;
	}
}

