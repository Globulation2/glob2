// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#pragma once

#include "ThreadMessageQueues.h"
#include "SDL_net.h"
#include <vector>

class FertilityCalculatorThreadMessage;
class Map;

///This functor, meant to be executed in another thread, calculates the fertility of the map
class FertilityCalculatorThread : public ThreadMessageQueues<FertilityCalculatorThreadMessage>
{
public:
	///Constructs the functor
	FertilityCalculatorThread(Map& map,
	                          std::queue<std::shared_ptr<FertilityCalculatorThreadMessage> >& outgoing,
	                          std::recursive_mutex& outgoingMutex);

	///Launches the thread that computes fertility
	void operator()();

private:
	///Computes the ressources gradient
	void computeRessourcesGradient();

	///Updates the percent complete
	void updatePercentComplete(float percent);

	class position
	{
	public:
		position(int x, int y) : x(x), y(y) {}
		int x;
		int y;
	};

	int get_pos(int x, int y);

	std::vector<Uint16> fertility;
	std::vector<Uint16> gradient;
	Uint16 fertilitymax;
	Map& map;
};
