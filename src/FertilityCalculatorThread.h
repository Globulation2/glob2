/*
  Copyright (C) 2007-2008 Bradley Arsenault

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

#ifndef FertilityCalculatorThread_h
#define FertilityCalculatorThread_h

#include <boost/shared_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <queue>
#include "SDL_net.h"

class FertilityCalculatorThreadMessage;
class Map;

///This functor, meant to be executed in another thread, calculates the fertility of the map
class FertilityCalculatorThread
{
public:
	///Constructs the functor
	FertilityCalculatorThread(Map& map, std::queue<boost::shared_ptr<FertilityCalculatorThreadMessage> >& outgoing, boost::recursive_mutex& outgoingMutex);

	///Launches the thread that computes fertility
	void operator()();

	///Sends this thread a message
	void sendMessage(boost::shared_ptr<FertilityCalculatorThreadMessage> message);

	///This returns whether the thread has exited
	bool hasThreadExited();

private:
	///Sends this IRC message back to the main thread
	void sendToMainThread(boost::shared_ptr<FertilityCalculatorThreadMessage> message);
	
	///Computes the resources gradient
	void computeResourcesGradient();
	
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
	
	std::queue<boost::shared_ptr<FertilityCalculatorThreadMessage> > incoming;
	std::queue<boost::shared_ptr<FertilityCalculatorThreadMessage> >& outgoing;
	boost::recursive_mutex incomingMutex;
	boost::recursive_mutex& outgoingMutex;
	bool hasExited;

	std::vector<Uint16> fertility;
	std::vector<Uint16> gradient;
	Uint16 fertilityMax;
	Map& map;
};




#endif
