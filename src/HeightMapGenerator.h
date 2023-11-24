/***************************************************************************
 *            HeightMapGenerator.h
 *
 *  Sun Jan  8 17:34:38 2006
 *  Copyright  2006  Leo Wandersleb
 *  Email: Leo.Wandersleb@gmx.de
 ****************************************************************************/

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
 
#ifndef __HEIGHT_MAP_GENERATOR_H
#define __HEIGHT_MAP_GENERATOR_H

#include "PerlinNoise.h"

class HeightMap /// class to generate height maps to decide where to put resources, water, sand and grass later
{
	float * _map; /// height values are always [0,1].
	unsigned int _w, _h; /// map size
	float * _stamp; /// smooth 0 to 1 gradient lookup to generate craters, islands and rivers
	unsigned int _r; /// radius of the _stamp
	PerlinNoise _pn;/// to get reproducible correlated random numbers
public:
	enum kindOfMap {SWAMP=0,ISLANDS=1,RIVER=2,CRATERS=3,RANDOM=4};
	
	HeightMap(unsigned int width, unsigned int height);
	HeightMap(unsigned int width, unsigned int height, unsigned int playerCount, unsigned int kind);
	~HeightMap();
	inline unsigned int uiLevel(unsigned int i, unsigned int scale) {
		return (unsigned int)(_map[i]*scale);
	}
	inline unsigned int uiLevel(unsigned int x, unsigned int y, unsigned int scale) {
		return (unsigned int)(_map[x%_w+(y%_h)*_w]*scale);
	}
	inline float operator() (unsigned int i) {
		return _map[i];
	}
	inline float operator() (const unsigned int x, const unsigned int y) {
		return _map[x%_w+(y%_h)*_w];
	}
	void mapOutput(char * filename); /// generates the file ~/.glob2/filename and writes the raw 0..255 values of map to it. to see it, use convert -size [width]x[height] -depth 8 gray:[filename] test.png

	void makePlain(float smoothingFactor); /// a plain perlin height field
	void makeSwamp(float smoothingFactor); /// a plain perlin height field
	void makeIslands(unsigned int count, float smoothingFactor); /// generates a 'swamp' with count hills
	void makeRiver(unsigned int maxDiameter, float smoothingFactor); /// generates a 'swamp' with a river based on a random walk. 
	void makeCraters(unsigned int craterCount, unsigned int craterRadius, float smoothingFactor); /// generates a 'swamp' with craterCount craters
private:
	void operator+(HeightMap hm){for(unsigned int i=0; i<_w*_h; i++)_map[i]+=hm(i);};
	void operator*(const float m){for(unsigned int i=0; i<_w*_h; _map[i++]*=m);};
	void operator/(const float d){for(unsigned int i=0; i<_w*_h; _map[i++]/=d);};
	void operator=(const float h) {for(unsigned int i=0; i<_w*_h; _map[i++]=h);}
	void operator=(HeightMap hm) {for(unsigned int i=0; i<_w*_h; i++)_map[i]=hm(i);}
	void init(unsigned int width, unsigned int height);
	void makeStamp(unsigned int radius); /// generates the stamp (smooth 0 to 1 gradient lookup to generate craters, islands and rivers)
	inline void lower(unsigned int coordX, unsigned int coordY); /// lower lowers the region around (coordX,coordY) to min(stamp,map)
	inline void maxRise(unsigned int coordX, unsigned int coordY); /// rise rises the region around (coordX,coordY) to max(1-stamp,map)
	inline void differenceStamp(unsigned int coordX, unsigned int coordY); /// multiplyStamp sets the region around (coordX,coordY) to (1-stamp)*map if map>0 and to 1-stamp else to bias away from other hills.
	inline void addNoise(float weight, float smoothingFactor); /// adds noise to the map: map=noise*weight+map*(1-weight)
	void stampOutput(char * filename); /// generates the file ~/.glob2/filename and writes the raw 0..255 values of stamp to it. to see it, use convert -size [width]x[height] -depth 8 gray:[filename] test.png where with==height as _stamp is always a square
	void normalize(); /// fits the values of _map to [0, 1]
};

#endif /* __HEIGHT_MAP_GENERATOR_H */
