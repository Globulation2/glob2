// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#pragma once

#include "Settings.h"
#include <algorithm>
#include <valarray>

namespace GAGCore
{
	class DrawableSurface;
}
using namespace GAGCore;
/**
 * DynamicClouds provides 3D (x,y,t) cloud generation based on a fast correlated
 * noise function.
 *
 * A cell of the field yields two opacities rather than one: a translucent body
 * and, sampled a little towards the sun, a brighter core. Drawing them as two
 * passes is what gives a flat alpha map the look of depth.
 *
 * In game the deck is confined to the fog of war. compute() takes a per-cell
 * visibility mask and fades the clouds out before they reach ground the player
 * can see, so they never obscure anything; the black fog layer is still drawn
 * underneath at full opacity, so they never reveal anything either.
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
	/** depth above which there is cloud at all, derived from
	 * preferences.txt: cloudCoverage=76
	 */
	float threshold;
	/// screen width/granularity+1
	int wGrid;
	/// screen height/granularity+1
	int hGrid;
	///cloud density
	std::valarray<unsigned char> cloudMap;
	/// opacity of the sunlit core, drawn over cloudMap
	std::valarray<unsigned char> coreMap;
	/// feathered cloud weight; empty unless the deck is confined to the fog of war
	std::valarray<unsigned char> fogMap;
	/// scratch for the raw field, kept so it is not reallocated every frame
	std::valarray<float> depthMap;
    int renderOffsetX, renderOffsetY, renderCellSize;

	/// how many cells away the sunlit core is sampled at this cell size
	static int sunStep(int cellSize) { return std::max(1, 48/std::max(1, cellSize)); }
	/// turns depths into a body and a core opacity, weighted by fog when confined
	void shade(int gridW, int gridH, int cellSize, const std::valarray<unsigned char> *fog,
	           std::valarray<unsigned char> &body, std::valarray<unsigned char> &core) const;
public:
	 ///render() distinguishes between the layers of one deck
	enum Layer {
		/// the body of the deck, rendered in the grey of a shaded cloud
		CLOUD,
		/// the sunlit core, rendered white over CLOUD
		CLOUD_CORE,
		/// gets rendered black.
		SHADOW
	};
	///initializes DynamicClouds using the settings file (preferences.txt)
	DynamicClouds(Settings * settings)
	{
		granularity=std::max(1, settings->cloudPatchSize);
		maxAlpha=(unsigned char)settings->cloudMaxAlpha;
		maxCloudSpeed=settings->cloudMaxSpeed;
		windStability=settings->cloudWindStability;
		cloudStability=settings->cloudStability;
		cloudSize=settings->cloudSize;
		cloudHeight=(float)settings->cloudHeight/100.0f;
		//cloudCoverage is the share of the sky under cloud. The field is far from
		//uniform, so the useful thresholds sit in a narrow band around its median.
		threshold=0.80f-0.0055f*(float)settings->cloudCoverage;
		wGrid=0;
		hGrid=0;
		renderOffsetX=0;
		renderOffsetY=0;
		renderCellSize=granularity;
	}
	virtual ~DynamicClouds() { }

	/**
	 * Fixes the lattice this viewport will use and reports what a visibility mask
	 * has to cover: gridW x gridH cells, the first one at world pixel
	 * (originX, originY), spaced cellSize apart. compute() repeats this for the
	 * same arguments, so a caller that needs the dimensions first can ask here.
	 */
	void prepare(const int viewPortX, const int viewPortY, const int viewPortWidth, const int viewPortHeight,
	             int &gridW, int &gridH, int &originX, int &originY, int &cellSize);

	/**
	 * Erodes and blurs a raw visibility mask, so that the deck reaches full
	 * strength inside the fog and has faded out before it touches ground in sight,
	 * instead of ending on a case boundary.
	 * @param visibility 255 where the ground is hidden, 0 where it is in sight
	 * @param wrap true when the grid covers the whole toroidal world
	 */
	static void feather(const std::valarray<unsigned char> &visibility, int gridW, int gridH, bool wrap,
	                    std::valarray<unsigned char> &out);

	/**
	 * updates cloudMap and coreMap
	 * @param viewPortX x-coordinate of the viewport
	 * @param viewPortY y-coordinate of the viewport
	 * @param time time
	 * @param visibility optional mask sized as prepare() reported; when given,
	 *        clouds only appear where it says the ground is hidden
	 */
	void compute(const int viewPortX, const int viewPortY, const int viewPortWidth, const int viewPortHeight, const int time,
	             const int worldWidth, const int worldHeight,
	             const std::valarray<unsigned char> *visibility = nullptr);
	void render(DrawableSurface *dest, const int viewPortWidth, const int viewPortHeight, Layer layer);
	/**
	 * Samples the cloud layer over the whole world at a coarse lattice.
	 * @param out receives gridW*gridH luminance/alpha pairs, row-major from the map origin
	 * @param visibility optional feathered mask sized gridW x gridH; call
	 *        getWorldGrid() first to learn those dimensions
	 */
	void computeWorld(const int worldWidth, const int worldHeight, const int time, std::valarray<unsigned char> &out, int &gridW, int &gridH,
	                  int maxGridSize = 2048, const std::valarray<unsigned char> *visibility = nullptr) const;
	/// dimensions computeWorld() will use, so a visibility mask can be built first
	void getWorldGrid(const int worldWidth, const int worldHeight, int &gridW, int &gridH, int &cellSize, int maxGridSize = 2048) const;
};
