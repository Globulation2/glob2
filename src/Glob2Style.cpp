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
	else
		Style::drawTextButtonBackground(target, x, y, w, h, highlight);
}
