// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __PLAYER_H
#define __PLAYER_H

#include <assert.h>
#include <vector>

#include <SDL_net.h>

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

/**
 * Player extends BasePlayer by an associated AI, a Game, a Map, a Team and a startng position.
 */
class Player:public BasePlayer
{
public:
	Player();
	Player(GAGCore::InputStream *stream, Team *teams[Team::MAX_COUNT], Sint32 versionMinor);
	Player(Sint32 number, const std::string& name, Team *team, PlayerType type);
	virtual ~Player(void);

	void setTeam(Team *team);
	void setBasePlayer(const BasePlayer *initial, Team *teams[Team::MAX_COUNT]);

	bool load(GAGCore::InputStream *stream, Team *teams[Team::MAX_COUNT], Sint32 versionMinor);
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
