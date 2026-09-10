// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Leo Wandersleb

#pragma once

#include "CloudField.h"
#include "Settings.h"
#include <algorithm>
#include <valarray>

namespace GAGCore
{
	class DrawableSurface;
}
using namespace GAGCore;
/**
 * DynamicClouds draws a deck of cartoon clouds over a world-anchored lattice.
 *
 * The deck is a CloudField: heaps of round lobes carried by the wind. Each
 * lattice cell holds the slope and height of the lobe standing there, uploaded
 * as one texture, and a fragment shader shades every pixel like a cel: a white
 * sunlit top, a pale flank and a grey-blue underside, with the bands cut at the
 * pixel rather than at the lattice. The flat view draws that as one quad and
 * the torus ring drapes it over its own mesh with the same shader, so the two
 * views agree pixel for pixel when the ring is flat.
 *
 * In game the deck is confined to the fog of war. compute() takes a per-cell
 * visibility mask: solid overcast where the ground is undiscovered, thinner
 * where it is explored but out of sight, and gone before it reaches ground the
 * player can see. The black fog layer is still drawn underneath at full
 * opacity, so the clouds never reveal anything. Without a mask, as on the
 * menus, the lobes drift as separate clouds over open ground.
 */
class DynamicClouds
{
	/** the horizontal and vertical distance of neighboring cloud densities.
	 * this value is set in preferences.txt: cloudPatchSize=8
	 */
	int granularity;
	/** maximum opacity 255 being opaque, 0 being invisible.
	 * this value is set in preferences.txt: cloudMaxAlpha=120
	 */
	unsigned char maxAlpha;
	/** maximum horizontal cloud speed in tenths of a pixel per frame.
	 * this value is set in preferences.txt: cloudMaxSpeed=3
	 */
	float maxCloudSpeed;
	/** 1/rate at which the wind is changing direction
	 * this value is set in preferences.txt: cloudWindStability=3550
	 */
	float windStability;
	/** 1/rate at which clouds change shape
	 * this value is set in preferences.txt: cloudStability=13000
	 */
	float cloudStability;
	/** spacing of the big lobes in pixels
	 * this value is set in preferences.txt: cloudSize=110
	 */
	float cloudSize;
	/** scale of the clouds/shadow in percent
	 * this value is set in preferences.txt: cloudHeight=150
	 */
	float cloudHeight;
	/// screen width/granularity+1
	int wGrid;
	/// screen height/granularity+1
	int hGrid;
	/// the shaded deck, wGrid*hGrid RGBA texels
	std::valarray<unsigned char> pixels;
	/// feathered cloud weight; empty unless the deck is confined to the fog of war
	std::valarray<unsigned char> fogMap;
	/// the world lattice, filled a band at a time across several frames
	std::valarray<CloudField::Sample> worldField;
	int renderOffsetX, renderOffsetY, renderCellSize;
	/// the texture and shader the 2D quad draws with, owned by one GL context
	unsigned texture, material;
	int textureW, textureH;
	void *graphicsContext;
	unsigned graphicsGeneration;
	bool materialFailed;

	CloudField field(int worldWidth, int worldHeight, int time) const
	{
		return CloudField(worldWidth * 32, worldHeight * 32, time, cloudSize, cloudStability, maxCloudSpeed,
		                  windStability);
	}
	/// Turns one sample into its texel: the slope as a normal, the height, and
	/// the opacity. weight is the fog mask, 255 for solid overcast; fog false
	/// makes a cloud over open ground instead.
	void shadeCell(const CloudField::Sample &sample, float spacing, unsigned char weight, bool fog,
	               unsigned char *rgba) const;
	void drawQuad(int x, int y, int w, int h, float red, float green, float blue, float alpha);
public:
	 ///render() distinguishes between the layers of one deck
	enum Layer {
		/// the deck itself
		CLOUD,
		/// its shadow on the ground, drawn black and displaced away from the sun
		SHADOW
	};
	///initializes DynamicClouds using the settings file (preferences.txt)
	DynamicClouds(Settings * settings)
	{
		granularity=std::max(1, settings->cloudPatchSize);
		maxAlpha=(unsigned char)settings->cloudMaxAlpha;
		maxCloudSpeed=(float)settings->cloudMaxSpeed/10.0f;
		windStability=settings->cloudWindStability;
		cloudStability=settings->cloudStability;
		cloudSize=settings->cloudSize;
		cloudHeight=(float)settings->cloudHeight/100.0f;
		wGrid=0;
		hGrid=0;
		renderOffsetX=0;
		renderOffsetY=0;
		renderCellSize=granularity;
		texture=material=0;
		textureW=textureH=0;
		graphicsContext=nullptr;
		graphicsGeneration=0;
		materialFailed=false;
	}
	virtual ~DynamicClouds();

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
	 * @param visibility 255 where the ground is undiscovered, less where it is
	 *        merely out of sight, 0 where it is in sight
	 * @param wrap true when the grid covers the whole toroidal world
	 */
	static void feather(const std::valarray<unsigned char> &visibility, int gridW, int gridH, int cellSize,
	                    bool wrap, std::valarray<unsigned char> &out);

	/**
	 * updates the deck for this viewport
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
	 * Samples a band of the cloud layer over the whole world.
	 * @param nextRow first row to fill, advanced to the first one still missing
	 * @param rows how many rows to fill this call
	 * @return true once the whole lattice holds one moment of the field
	 */
	bool sampleWorldRows(const int worldWidth, const int worldHeight, const int time, int gridW, int gridH,
	                     int cellSize, int &nextRow, int rows);
	/**
	 * Turns a completed world sample into a texture.
	 * @param out receives gridW*gridH RGBA texels, row-major from the map origin
	 * @param visibility optional mask sized gridW x gridH; getWorldGrid() reports those
	 */
	void shadeWorld(std::valarray<unsigned char> &out, int gridW, int gridH, int cellSize,
	                const std::valarray<unsigned char> *visibility = nullptr) const;
	/// dimensions the world lattice will use, so a visibility mask can be built first
	void getWorldGrid(const int worldWidth, const int worldHeight, int &gridW, int &gridH, int &cellSize, int maxGridSize = 2048) const;
	/// How far the wind has carried the deck by `time`, in world pixels. A sample
	/// taken earlier shows the deck of `time` when shifted by the difference.
	void drift(int time, float &x, float &y) const
	{
		CloudField::drift(time, maxCloudSpeed, windStability, x, y);
	}
	/**
	 * Builds the shader that turns deck texels into cel-shaded cloud. It samples
	 * texture unit 0 through the texture matrix and multiplies by the vertex
	 * colour, so the caller lights and fades the deck with glColor.
	 * @return the program, or 0 where shaders are unavailable
	 */
	static unsigned createDeckMaterial();
};
