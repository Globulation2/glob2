/*
 * Globulation 2 some usefull and stupid utilities
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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
	bool ptInRect(int x, int y, SDL_Rect *r);
	void rectClipRect(int &x, int &y, int &w, int &h, SDL_Rect &r);
	void rectExtendRect(SDL_Rect *rs, SDL_Rect *rd);
	void rectExtendRect(int xs, int ys, int ws, int hs, int *xd, int *yd, int *wd, int *hd);
	void sdcRects(SDL_Rect *source, SDL_Rect *destination, SDL_Rect clipping);
};

#endif

