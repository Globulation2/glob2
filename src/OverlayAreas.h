// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef OverlayAreas_h
#define OverlayAreas_h

#include <vector>
#include "Types.h"

class Game;

///This class is used to compute overlay areas, a tool to visualize concentrations
///of, for example, starving units. Note that these may be computed in another
///thread (Fertility), so use the computeFinished to find out
class OverlayArea
{
public:
	enum OverlayType
	{
		None,
		Starving,
		Damage,
		Defence,
		Fertility,
	};

	///Construct the overlay area
	OverlayArea();
	
	~OverlayArea();
	
	///Compute the overlay area
	void compute(Game& game, OverlayType type, int localteam);

	///Gets the value of the overlay for a given position
	Uint16 getValue(int x, int y);
	
	///Gets the maximum value of overlay
	Uint16 getMaximum();
	
	///Returns the last computed overlay type
	OverlayType getOverlayType();
	
	///Forces recomputing the overlay next round
	void forceRecompute();
	
protected:
	OverlayType type;
	OverlayType lasttype;
	int height;
	int width;
	std::vector<Uint16> overlay;
	Uint16 overlaymax;
	
	void increasePoint(int x, int y, int distance, std::vector<Uint16>& field, Uint16& max);
	void spreadPoint(int x, int y, int value, int distance, std::vector<Uint16>& field, Uint16& msx);
};

#endif
