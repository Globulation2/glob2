/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

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
	void compute(Game& game, OverlayType type, int localTeam);

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
	OverlayType lastType;
	int height;
	int width;
	std::vector<Uint16> overlay;
	Uint16 overlayMax;
	
	void increasePoint(int x, int y, int distance, std::vector<Uint16>& field, Uint16& max);
	void spreadPoint(int x, int y, int value, int distance, std::vector<Uint16>& field, Uint16& msx);
};

#endif
