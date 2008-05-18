/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "BasePlayer.h"

class Game;
class Map;
class Team;
namespace GAGCore
{
	class InputStream;
}



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
