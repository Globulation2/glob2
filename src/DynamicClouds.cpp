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
#include "CloudField.h"

void DynamicClouds::compute(const int viewPortX, const int viewPortY, const int viewPortWidth,
    const int viewPortHeight, const int time, const int worldWidth, const int worldHeight)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)) return;
    // Use the same sampling lattice in both views, including partial grid cells.
    int pixelX=viewPortX*32, pixelY=viewPortY*32;
    renderOffsetX=-(pixelX%granularity); renderOffsetY=-(pixelY%granularity);
    int startX=pixelX+renderOffsetX, startY=pixelY+renderOffsetY;
    wGrid=(viewPortWidth-renderOffsetX+granularity-1)/granularity+1;
    hGrid=(viewPortHeight-renderOffsetY+granularity-1)/granularity+1;
    alphaMap.resize(wGrid*hGrid); cloudMap.resize(wGrid*hGrid);
    CloudField field(worldWidth*32,worldHeight*32,time,cloudSize,cloudStability,
        maxCloudSpeed,windStability,maxAlpha);
    for (int y=0;y<hGrid;++y) for (int x=0;x<wGrid;++x) {
        int wx=startX+x*granularity, wy=startY+y*granularity;
        alphaMap[y*wGrid+x]=field.opacity(wx,wy);
        cloudMap[y*wGrid+x]=field.opacity(wx,wy,std::max(.01f,cloudHeight));
    }
}

void DynamicClouds::render(DrawableSurface *dest, const int, const int, DynamicClouds::Layer layer)
{
    if (!(globalContainer->gfx->getOptionFlags() & GraphicContext::USEGPU)) return;
    // Magnification is sampled in world space, never around the viewport center.
    bool cloud=layer==DynamicClouds::CLOUD;
    dest->drawAlphaMap(cloud ? cloudMap : alphaMap,wGrid,hGrid,
        renderOffsetX,renderOffsetY,granularity,granularity,
        cloud ? Color(255,255,255) : Color(0,0,0));
}
