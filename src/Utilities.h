/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __UTILITIES_H
#define __UTILITIES_H

#include "Header.h"
#include <stdlib.h>

Uint32 syncRand(void);
void setSyncRandSeed();
void setSyncRandSeedA(Uint32 seed);
void setSyncRandSeedB(Uint32 seed);
void setSyncRandSeedC(Uint32 seed);
Uint32 getSyncRandSeedA(void);
Uint32 getSyncRandSeedB(void);
Uint32 getSyncRandSeedC(void);

int sign(int s);
int distSquare(int x1, int y1, int x2, int y2);
#define SIGN(s) ((s) == 0 ? 0 : ((s)>0 ? 1 : -1) )

namespace Utilities
{
	// rectangle
	//! return true if (x,y) is in r
	bool ptInRect(int x, int y, SDL_Rect *r);
	// FIXME : please Luc document this :
	void rectClipRect(int &x, int &y, int &w, int &h, SDL_Rect &r);
	void rectExtendRect(SDL_Rect *rs, SDL_Rect *rd);
	void rectExtendRect(int xs, int ys, int ws, int hs, int *xd, int *yd, int *wd, int *hd);
	void sdcRects(SDL_Rect *source, SDL_Rect *destination, SDL_Rect clipping);

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

	//! data for computing minimap
	void computeMinimapData(int resolution, int mW, int mH, int *maxSize, int *sizeX, int *sizeY, int *decX, int *decY);

	char *concat(const char *a, const char *b);
	char *dencat(const char *a, const char *b);
};

#endif

