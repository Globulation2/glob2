/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __PLAYER_H
#define __PLAYER_H

#include <assert.h>
#include <vector>

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_net.h>
#else
#include <Types.h>
#endif

#include "AI.h"

#include <string>

class Game;
class Map;
class Team;
namespace GAGCore
{
	class InputStream;
}

class BasePlayer
{
public:
 	enum PlayerType
	{
		P_NONE=0, // NOTE : we don't need any more because null player are not created
		P_LOST_DROPPING=1, // Player will be droped in any cases, but we still have to exchange orders
		P_LOST_FINAL=2, // Player is no longer considered, may be later changed to P_AI. All orders are NULLs.
		/*P_AI=3,
		P_IP=4,
		P_LOCAL=5*/
		P_IP=3,
		P_LOCAL=4,
		P_AI=5,
		// Note : P_AI + n is AI type n
	};
	
	static AI::ImplementitionID implementitionIdFromPlayerType(PlayerType type)
	{
		assert(type>=P_AI);
		return (AI::ImplementitionID)((int)type-(int)P_AI);
	}
	static PlayerType playerTypeFromImplementitionID(AI::ImplementitionID iid)
	{
		return (PlayerType)((int)iid+(int)P_AI);
	}

	enum {MAX_NAME_LENGTH = 32};

	PlayerType type;

	Sint32 number;
	Uint32 numberMask;
	std::string name;
	Sint32 teamNumber;
	Uint32 teamNumberMask;
	
	bool quitting; // We have executed the quitting order of player, but we did not freed all his orders.
	Uint32 quitUStep;
	Uint32 lastUStepToExecute;
	
	///Used to identify the player over the internet
	Uint32 playerID;

public:
	
	BasePlayer(void);
	BasePlayer(Sint32 number, const std::string& name, Sint32 teamn, PlayerType type);
	void init();
	virtual ~BasePlayer(void);
	void setNumber(Sint32 number);
	void setTeamNumber(Sint32 teamNumber);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream) const;

	Uint32 checkSum();
	
	virtual void makeItAI(AI::ImplementitionID aiType);

public:
	bool disableRecursiveDestruction;
	
public:
	FILE *logFile;
};

class Player:public BasePlayer
{
public:
	Player();
	Player(GAGCore::InputStream *stream, Team *teams[32], Sint32 versionMinor);
	Player(Sint32 number, const std::string& name, Team *team, PlayerType type);
	virtual ~Player(void);

	void setTeam(Team *team);
	void setBasePlayer(const BasePlayer *initial, Team *teams[32]);
	
	bool load(GAGCore::InputStream *stream, Team *teams[32], Sint32 versionMinor);
	void save(GAGCore::OutputStream  *stream);
	
	void makeItAI(AI::ImplementitionID aiType);
public:
	Sint32 startPositionX, startPositionY;

	// team is the basic (structural) pointer. The others are directs access.
	Team *team;
	Game *game;
	Map *map;
	
	AI *ai;

public:
	Uint32 checkSum(std::vector<Uint32> *checkSumsVector);
};

#endif
