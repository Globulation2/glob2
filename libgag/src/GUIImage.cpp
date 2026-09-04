// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat & Luc-Olivier de Charrière

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
