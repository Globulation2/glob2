// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#ifndef FertilityCalculatorThread_h
#define FertilityCalculatorThread_h

#include <memory>
#include <mutex>
#include <queue>
#include "SDL_net.h"

class FertilityCalculatorThreadMessage;
class Map;

///This functor, meant to be executed in another thread, calculates the fertility of the map
class FertilityCalculatorThread
{
public:
	///Constructs the functor
	FertilityCalculatorThread(Map& map, std::queue<std::shared_ptr<FertilityCalculatorThreadMessage> >& outgoing, std::recursive_mutex& outgoingMutex);

	///Launches the thread that computes fertility
	void operator()();

	///Sends this thread a message
	void sendMessage(std::shared_ptr<FertilityCalculatorThreadMessage> message);

	///This returns whether the thread has exited
	bool hasThreadExited();

private:
	///Sends this IRC message back to the main thread
	void sendToMainThread(std::shared_ptr<FertilityCalculatorThreadMessage> message);
	
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
	
	std::queue<std::shared_ptr<FertilityCalculatorThreadMessage> > incoming;
	std::queue<std::shared_ptr<FertilityCalculatorThreadMessage> >& outgoing;
	std::recursive_mutex incomingMutex;
	std::recursive_mutex& outgoingMutex;
	bool hasExited;

	std::vector<Uint16> fertility;
	std::vector<Uint16> gradient;
	Uint16 fertilitymax;
	Map& map;
};




#endif
