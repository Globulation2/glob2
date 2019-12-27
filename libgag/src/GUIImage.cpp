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

#include <GUIImage.h>
#include <stdarg.h>
#include <SupportFunctions.h>
#include <assert.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <algorithm>

using namespace GAGCore;

namespace GAGGUI
{
	Image::Image(int x, int y, Uint32 hAlign, Uint32 vAlign, GAGCore::DrawableSurface *image)
	{
		assert(image);
		this->image = image;

		this->x = x;
		this->y = y;
		this->w = image->getW();
		this->h = image->getH();
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;
	}

	void Image::paint(void)
	{
		int wDec, hDec;
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);

		assert(parent);
		assert(parent->getSurface());


		if (hAlignFlag==ALIGN_FILL)
			wDec=(w-image->getW())>>1;
		else
			wDec=0;

		if (vAlignFlag==ALIGN_FILL)
			hDec=(h-image->getH())>>1;
		else
			hDec=0;

		parent->getSurface()->drawSurface(x+wDec, y+hDec, image);
	}
}
