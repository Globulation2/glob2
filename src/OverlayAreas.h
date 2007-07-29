/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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

#include "Game.h"
#include <vector>
#include <boost/thread/thread.hpp>


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
	
	///Compute the overlay area
	void compute(Game& game, OverlayType type, int localteam);

	///Gets the value of the overlay for a given position
	Uint16 getValue(int x, int y);
	
	///Gets the maximum value of overlay
	Uint16 getMaximum();
	
	///Returns the last computed overlay type
	OverlayType getOverlayType();
	
	///Computes the fertility overlay
	void computeFertility(Game& game, int localteam);
	
	///This forces the recomputation of the fertility map
	void forceFertilityRecompute();
	
	///Returns whether the fertility is being calculated
	bool isFertilityBeingCalculated();
protected:
	OverlayType type;
	OverlayType lasttype;
	int height;
	int width;
	std::vector<Uint16> overlay;
	Uint16 overlaymax;
	
	///Fertility only needs to be computed once
	std::vector<Uint16> fertility;
	Uint16 fertilitymax;
	int fertilityComputed;
	
	void increasePoint(int x, int y, int distance, std::vector<Uint16>& field, Uint16& max);
	void spreadPoint(int x, int y, int value, int distance, std::vector<Uint16>& field, Uint16& msx);
};


///This functor calculates the fertility in a thread safe manner
class FertilityCalculator
{
public:
	///Constructs the functor
	FertilityCalculator(std::vector<Uint16>& fertility, Uint16& fertilityMax, Game& game, int localteam, int width, int height, int& fertilityComputed);

	///Computes the fertility
	void operator()();

private:
	class position
	{
	public:
		position(int x, int y) : x(x), y(y) {}
		int x;
		int y;
	};

	void computeRessourcesGradient();
	int get_pos(int x, int y) { return x*height+y; }

	std::vector<Uint16>& fertility;
	std::vector<Uint16> gradient;
	Uint16& fertilitymax;
	Game& game;
	int localteam;
	int width;
	int height;
	int& fertilityComputed;
};

#endif
