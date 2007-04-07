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
 *  the Free Software Foundation; either version 2 of the License, or
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
		
		int wGrid=w/granularity+1;
		int hGrid=h/granularity+1;
		std::valarray<unsigned char> alphaMap(wGrid*hGrid);
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
