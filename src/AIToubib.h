/*
  This file is part of Globulation 2, a free software real-time strategy game
  http://www.globulation2.org
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __AI_TOUBIB_H
#define __AI_TOUBIB_H

#include "AIImplementation.h"

class Game;
class Map;
class Order;
class Player;
class Team;
class Building;

class AIToubib : public AIImplementation
{
protected:
	// Internal members
	Uint32 now;
	
protected:
	// Internal functions

	//! Initialization (avoid duplicate code)
	void init(Player *player);
	
	//! Create a building if possible
	boost::shared_ptr<Order> getOrderBuildingStep(void);
	//! Compute internal stats used by other parts of the code
	void computeMyStatsStep(void);
	
public:
	AIToubib(Player *player);
	AIToubib(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	virtual ~AIToubib();
	
	Player *player;
	Team *team;
	Game *game;
	Map *map;
	
	//! Load AI saved from a stream
	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	//! Save AI to a save stream
	void save(GAGCore::OutputStream *stream);
	
	//! return a new order in response to last events
	boost::shared_ptr<Order> getOrder(void);
	
private:
	/*
	// All constants parameterizing AIToubib	
	static const Uint8 MAX_NB_PROJECTS = 10;
	static const Uint8 NB_HISTORY_STATES = 5;
	
	// Put here all the state variables
	typedef struct {
		// TODO
		
	} AIState;
	
	class AIProject {
		// TODO
	};
	
	// Put here all the history variables
	
	//! Circular buffer to store state's history
	AIState history[NB_HISTORY_STATES];
	Uint8 currentStateIndex;
	
	//std::list<int>::iterator pi;
	
	//std::list<int> myList;
	//std::list<int, std::allocator<int> > t1;
	std::priority_queue<
		int, 
		std::vector<
		int, 
		std::allocator<int> >,
		std::less<int> > pq;*/
	/*std::priority_queue< AIProject,
		std::list< AIProject, std::allocator<AIProject> >,
		std::less<AIProject> > pq;
	//AIProject projects[];
	*/
	/*
	//! Initialization (avoid duplicate code)
	void init(Player *player);
	
	//! evaluate the current state 
	void evalState();
	*/
};

#endif
