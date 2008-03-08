/***************************************************************************
 *            HeightMapGenerator.h
 *
 *  Sun Feb  4 16:17:38 2007
 *  Copyright  2007  Leo Wandersleb
 *  Email: Leo.Wandersleb@gmx.de
*/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#include "DynamicClouds.h"
#include "GraphicContext.h"
#include "GlobalContainer.h"
#include "SimplexNoise.h"

#define INT_ROUND_RSHIFT(x,places)  ( ((x)+(1<<((places)-1))) >> (places) )

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int w, const int h, const int time)
{
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
	{
		//tribute to the torrodial world: the viewpot must never jump by more than 31.
		//if it does, we assume a jump in the opposite direction
		static int vpX=0;
		static int vpY=0;
		//Correlated Noise
		static PerlinNoise pn;
		
		static float offsetX=0, offsetY=0;
		offsetX+=pn.Noise((float)time/windStability+0.7f)*windStability*maxCloudSpeed/1000.0f;
		offsetY+=pn.Noise((float)time/windStability+1.6f)*windStability*maxCloudSpeed/1000.0f;
		
		vpX += (viewPortX-vpX%64+96)%64-32;
		vpY += (viewPortY-vpY%64+96)%64-32;
		
		wGrid=w/granularity+1;
		hGrid=h/granularity+1;
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
				if (alpha<0) alpha=0; if (alpha>maxAlpha) alpha=maxAlpha;
				alphaMap[wGrid*y+x] = alpha;
			}
	}
}

void DynamicClouds::renderOverlay(DrawableSurface *dest, const int w, const int h)
{
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
	{
		//magnify cloud map by cloud height in white (clouds)
		//TODO: (int)(cloudheight*granularity) might round unexpectedly for
			//low granularity resulting in unpainted areas/unscaled clouds.
		dest->drawAlphaMap(alphaMap,
			wGrid, hGrid,
			(int)((1.0f-cloudHeight)*.5f*w), (int)((1.0f-cloudHeight)*.5f*h),
			(int)(cloudHeight*granularity), (int)(cloudHeight*granularity),
			Color(255,255,255));
	}
}

void DynamicClouds::renderShadow(DrawableSurface *dest, const int w, const int h)
{
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
	{
		//fit cloud map in black into screen (shadows)
		dest->drawAlphaMap(alphaMap, 
			wGrid, hGrid, 
			0, 0, 
			granularity, granularity,
			Color(0,0,0));
	}
}

/*
void DynamicClouds::render(DrawableSurface *dest, const int viewPortX,
const int viewPortY, const int w, const int h, const int time)
{
	if (globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)
	{
		//tribute to the torrodial world: the viewpot must never jump by more than 31.
		//if it does, we assume a jump in the opposite direction
		static int vpX=0;
		static int vpY=0;
		//Correlated Noise
		static PerlinNoise pn;
		
		static float offsetX=0, offsetY=0;
		offsetX+=pn.Noise((float)time/windStability+0.7f)*windStability*maxCloudSpeed/1000.0f;
		offsetY+=pn.Noise((float)time/windStability+1.6f)*windStability*maxCloudSpeed/1000.0f;
		
		vpX += (viewPortX-vpX%64+96)%64-32;
		vpY += (viewPortY-vpY%64+96)%64-32;
		
		//int wGrid=w/granularity+1;
		//int hGrid=h/granularity+1;
		//std::valarray<unsigned char> alphaMap(wGrid*hGrid);
		wGrid=w/granularity+1;
		hGrid=h/granularity+1;
		alphaMap.resize(wGrid*hGrid);
		
		for (int y=0; y<hGrid; y++)
			for (int x=0; x<wGrid; x++)
				alphaMap[wGrid*y+x]=(unsigned char)std::max((unsigned int)0,std::min((unsigned int)maxAlpha,
						(unsigned int)pow((rootOfMaxAlpha*1.8f*(-.08f+pn.Noise(
						(float)(x*granularity+(vpX<<5)+offsetX)/cloudSize,
						(float)(y*granularity+(vpY<<5)+offsetY)/cloudSize,
						(float)time/cloudStability))),2)));
		//fit cloud map in black into screen (shadows)
		dest->drawAlphaMap(alphaMap, 
			wGrid, hGrid, 
			0, 0, 
			granularity, granularity,
			Color(0,0,0));
		//magnify cloud map by cloud height in white (clouds)
		//TODO: (int)(cloudheight*granularity) might round unexpectedly for
			//low granularity resulting in unpainted areas/unscaled clouds.
		dest->drawAlphaMap(alphaMap,
			wGrid, hGrid,
			(int)((1.0f-cloudHeight)*.5f*w), (int)((1.0f-cloudHeight)*.5f*h),
			(int)(cloudHeight*granularity), (int)(cloudHeight*granularity),
			Color(255,255,255));
	}
}
*/
