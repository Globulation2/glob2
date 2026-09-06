// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

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
	Uint32 getValue(int x, int y);

	///Gets the maximum value of overlay
	Uint32 getMaximum();
	
	///Returns the last computed overlay type
	OverlayType getOverlayType();
	
	///Forces recomputing the overlay next round
	void forceRecompute();
	
protected:
	OverlayType type;
	OverlayType lasttype;
	int height;
	int width;
	std::vector<Uint32> overlay;
	Uint32 overlaymax;
};

