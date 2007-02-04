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
 
#ifndef _DYNAMICCLOUDS_H
#define _DYNAMICCLOUDS_H

#include "PerlinNoise.h"
#include <math.h>

namespace GAGCore
{
	class DrawableSurface;
}
using namespace GAGCore;

class DynamicClouds
{
	static const int granularity=16;
	static const unsigned char maxAlpha=130;//[0;255]
	static const float maxCloudSpeed=3;
	static const float windStability=3550;
	static const float cloudStability=1300;
	static const float cloudSize=300;
	static const float cloudHeight=1.5;//(cloud - ground) / (eyes - ground)
	float rootOfMaxAlpha;
public:
	DynamicClouds() 
	{
		rootOfMaxAlpha=sqrt((float)maxAlpha);
	}
	virtual ~DynamicClouds() { }
	void render(DrawableSurface *dest, const int viewPortX,
	const int viewPortY, const int w, const int h, const int time);
};

#endif /* _DYNAMICCLOUDS_H */
