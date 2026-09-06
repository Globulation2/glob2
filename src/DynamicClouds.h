// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#pragma once

#include "PerlinNoise.h"
#include "Settings.h"
#include <math.h>
#include <valarray>

namespace GAGCore
{
	class DrawableSurface;
}
using namespace GAGCore;
/**
 * DynamicClouds provides 3D (x,y,t) cloud generation based on a fast correlated
 * noise function.
 */
class DynamicClouds
{
	/** the horizontal and vertical distance of neighboring cloud densities.
	 * this value is set in preferences.txt: cloudPatchSize=16
	 */
	int granularity;
	/** maximum opacity 255 being opaque, 0 being invisible.
	 * this value is set in preferences.txt: cloudMaxAlpha=120
	 */
	unsigned char maxAlpha;
	/** maximum horizontal cloud speed in ca pixels per frame.
	 * this value is set in preferences.txt: cloudMaxSpeed=3
	 */
	float maxCloudSpeed;
	/** 1/rate at which the wind is changing direction
	 * this value is set in preferences.txt: cloudWindStability=3550
	 */
	float windStability;
	/** 1/rate at which clouds change shape
	 * this value is set in preferences.txt: cloudStability=1300
	 */
	float cloudStability;
	/** average length of clouds in pixels
	 * this value is set in preferences.txt: cloudSize=300
	 */
	float cloudSize;
	/** scale of the clouds/shadow in percent
	 * this value is set in preferences.txt: cloudHeight=150
	 */
	float cloudHeight;
	/**
	 * helper variable (sqrt(maxAlpha))
	 */
	float rootOfMaxAlpha;
	/// screen width/granularity+1
	int wGrid;
	/// screen height/granularity+1
	int hGrid;
	///cloud/shadow density
	std::valarray<unsigned char> alphaMap;
public:
	 ///render() distinguishes between CLOUD and SHADOW
	enum Layer {
		/// gets rendered white and scaled by cloudHeight
		CLOUD,
		/// gets rendered black.
		SHADOW
	};
	///initializes DynamicClouds using the settings file (preferences.txt)
	DynamicClouds(Settings * settings)
	{
		granularity=settings->cloudPatchSize;
		maxAlpha=(unsigned char)settings->cloudMaxAlpha;
		rootOfMaxAlpha=sqrt((float)maxAlpha);
		maxCloudSpeed=settings->cloudMaxSpeed;
		windStability=settings->cloudWindStability;
		cloudStability=settings->cloudStability;
		cloudSize=settings->cloudSize;
		cloudHeight=(float)settings->cloudHeight/100.0f;
	}
	virtual ~DynamicClouds() { }
	/**
	 * updates alphaMap
	 * @param viewPortX x-coordinate of the viewport
	 * @param viewPortY y-coordinate of the viewport
	 * @param w width of the alphaMap
	 * @param h height of the alphaMap
	 * @param time time
	 */
	void compute(const int viewPortX, const int viewPortY, const int viewPortWdth, const int viewPortHeght, const int time);
	void render(DrawableSurface *dest, const int viewPortWidth, const int viewPortHeight, Layer layer);
};

