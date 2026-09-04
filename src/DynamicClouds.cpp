// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#include "DynamicClouds.h"
#include "GraphicContext.h"
#include "GlobalContainer.h"
#include "SimplexNoise.h"

#define INT_ROUND_RSHIFT(x,places)  ( ((x)+(1<<((places)-1))) >> (places) )

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int viewPortWdth, const int viewPortHeight, const int time)
{
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
	{
		//tribute to the torrodial world: the viewport must never jump by more than 31.
		//if it does, we assume a jump in the opposite direction
		static int vpX=0;
		static int vpY=0;
		//Correlated Noise
		static PerlinNoise pn;

		static float offsetX=0, offsetY=0;
		//TODO: magic numbers!!!
		offsetX+=pn.Noise((float)time/windStability+0.7f)*windStability*maxCloudSpeed/1000.0f;
		offsetY+=pn.Noise((float)time/windStability+1.6f)*windStability*maxCloudSpeed/1000.0f;

		vpX += (viewPortX-vpX%64+96)%64-32;
		vpY += (viewPortY-vpY%64+96)%64-32;

		wGrid=viewPortWdth/granularity+1;
		hGrid=viewPortHeight/granularity+1;
		alphaMap.resize(wGrid*hGrid);

		int iCloudSize = (int)((1<<16) /cloudSize);
		int iCloudStability = (int)((1<<16) /cloudStability);
		int iOffsetX = (int)(((vpX<<5) + offsetX)*iCloudSize),
		    iOffsetY = (int)(((vpY<<5) + offsetY)*iCloudSize);

 		int noiseMultiplier = (int)((1<<8) *rootOfMaxAlpha*1.8f);
		for (int y=0; y<hGrid; y++)
			for (int x=0; x<wGrid; x++) {
				int nx = INT_ROUND_RSHIFT(x*granularity*iCloudSize + iOffsetX, 8);
				int ny = INT_ROUND_RSHIFT(y*granularity*iCloudSize + iOffsetY, 8);
				int nz = INT_ROUND_RSHIFT(time * iCloudStability, 8);
				int noise = (SimplexNoise::getNoise3D(nx,ny,nz)) - 128;
				int a = INT_ROUND_RSHIFT(noiseMultiplier * (-21+noise), 8);
				int alpha = INT_ROUND_RSHIFT(a*a, 16);
				if (alpha<0)
					alpha=0;
				if (alpha>maxAlpha)
					alpha=maxAlpha;
				alphaMap[wGrid*y+x] = alpha;
			}
	}
}

void DynamicClouds::render(DrawableSurface *dest, const int viewPortWidth, const int viewPortHeight, DynamicClouds::Layer layer)
{
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
	{
		Color c;
		int offsetX, offsetY, gran;
		switch(layer) {
		case DynamicClouds::CLOUD:
			c=Color(255,255,255);
			offsetX=(1.0f-cloudHeight)*.5f*viewPortWidth;
			offsetY=(1.0f-cloudHeight)*.5f*viewPortHeight;
			gran=cloudHeight*granularity;
			break;
		case DynamicClouds::SHADOW:
			c=Color(0,0,0);
			offsetX=0;
			offsetY=0;
			gran=granularity;
			break;
		default:
			assert(false);
		}
		//magnify cloud map by cloud height in white (clouds)
		//TODO: (int)(cloudheight*granularity) might round unexpectedly for
			//low granularity resulting in unpainted areas/unscaled clouds.
		dest->drawAlphaMap(alphaMap,
			wGrid, hGrid,
			offsetX, offsetY,
			gran, gran,
			c);
	}
}
