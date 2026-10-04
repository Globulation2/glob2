// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <vector>
#include "Types.h"
#include <GraphicContext.h>

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
	Uint32 getValue(int x, int y) const;

	///Gets the maximum value of overlay
	Uint32 getMaximum() const;
	
	///Returns the last computed overlay type
	OverlayType getOverlayType() const;

	///The colour an overlay is drawn in (its alpha scales with the value)
	static GAGCore::Color colorOf(OverlayType type)
	{
		switch (type)
		{
			case Starving: return GAGCore::Color(192, 0, 0);
			case Damage: return GAGCore::Color(192, 0, 0);
			case Defence: return GAGCore::Color(0, 0, 192);
			case Fertility: return GAGCore::Color(0, 192, 128);
			default: return GAGCore::Color();
		}
	}
	
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

