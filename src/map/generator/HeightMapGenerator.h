// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Leo Wandersleb
 
#pragma once

#include "PerlinNoise.h"
#include <CooperativeTask.h>

class HeightMap /// class to generate heightmaps to decide where to put resources, water, sand and grass later
{
    bool lowerValid = false;
    unsigned lowerX = 0, lowerY = 0;
	float * _map; /// height values are always [0,1].
	unsigned int _w, _h; /// map size
	float * _stamp; /// smooth 0 to 1 gradient lookup to generate craters, islands and rivers
	unsigned int _r; /// radius of the _stamp
	PerlinNoise _pn;/// to get reproducible correlated random numbers
public:
	enum kindOfMap {SWAMP=0,ISLANDS=1,RIVER=2,CRATERS=3,RANDOM=4};
	
	HeightMap(unsigned int width, unsigned int height);
	~HeightMap();
    HeightMap(const HeightMap&) = delete;
    HeightMap& operator=(const HeightMap&) = delete;
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

    // Tasks borrow this height map. Keep it alive and run at most one task per
    // instance. Cancellation leaves partial heights; start a new generation
    // before reading them. The editor publishes only a completed map.
	void makePlain(float smoothingFactor); /// a plain perlin height field
    GAGCore::CooperativeTask makePlainTask(float smoothingFactor);
	void makeSwamp(float smoothingFactor); /// a plain perlin height field
    GAGCore::CooperativeTask makeSwampTask(float smoothingFactor);
	void makeIslands(unsigned int count, float smoothingFactor); /// generates a 'swamp' with count hills
    GAGCore::CooperativeTask makeIslandsTask(unsigned int count, float smoothingFactor);
	void makeRiver(unsigned int maxDiameter, float smoothingFactor); /// generates a 'swamp' with a river based on a random walk. 
    GAGCore::CooperativeTask makeRiverTask(unsigned int maxDiameter, float smoothingFactor);
	void makeCraters(unsigned int craterCount, unsigned int craterRadius, float smoothingFactor); /// generates a 'swamp' with craterCount craters
    GAGCore::CooperativeTask makeCratersTask(unsigned int craterCount, unsigned int craterRadius, float smoothingFactor);
private:
    GAGCore::CooperativeTask fillTask(float value);
	void init(unsigned int width, unsigned int height);
    GAGCore::CooperativeTask makeStampTask(unsigned int radius);
    GAGCore::CooperativeTask lowerTask(unsigned int coordX, unsigned int coordY);
    GAGCore::CooperativeTask differenceStampTask(unsigned int coordX, unsigned int coordY);
    GAGCore::CooperativeTask addNoiseTask(float weight, float smoothingFactor);
	void stampOutput(char * filename); /// generates the file ~/.glob2/filename and writes the raw 0..255 values of stamp to it. to see it, use convert -size [width]x[height] -depth 8 gray:[filename] test.png where with==height as _stamp is always a square
    GAGCore::CooperativeTask normalizeTask();
};
