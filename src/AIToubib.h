// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors

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
	std::shared_ptr<Order> getOrderBuildingStep(void);
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
	std::shared_ptr<Order> getOrder(void);
	
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
