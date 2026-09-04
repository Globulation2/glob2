// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __SUPPORT_FUNCTION_H
#define __SUPPORT_FUNCTION_H

#include "GAGSys.h"
#include <stdlib.h>
#include <string>
#include <stdarg.h>

namespace GAGCore
{
	// rectangle
	//! return true if (x,y) is in r
	bool ptInRect(int x, int y, SDL_Rect *r);
	//! FIXME : please Luc document this
	void rectClipRect(int &x, int &y, int &w, int &h, SDL_Rect &r);
	//! FIXME : please Luc document this
	void rectExtendRect(SDL_Rect *rs, SDL_Rect *rd);
	//! FIXME : please Luc document this
	void rectExtendRect(int xs, int ys, int ws, int hs, int *xd, int *yd, int *wd, int *hd);
	//! FIXME : please Luc document this
	void sdcRects(SDL_Rect *source, SDL_Rect *destination, const SDL_Rect &clipping);

	// color space conversion
	//! do a color space conversion from RGB to HSV
	void RGBtoHSV( float r, float g, float b, float *h, float *s, float *v );
	//! do a color space conversion from HSV to RGB
	void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v );
	// color space conversion support functions
	//! return min of f1, f2 and f3
	float fmin(float f1, float f2, float f3);
	//! return max of f1, f2 and f3
	float fmax(float f1, float f2, float f3);
};

#endif

