/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef YOGGamePlayerManager_h
#define YOGGamePlayerManager_h

#include "GameHeader.h"
#include "YOGGameServer.h"
class YOGGame;

///This class handles the players and AI's that can join, be kicked out of, disconnect, leave
///and otherwise be mangled arround with in an online game during the setup stage
class YOGGamePlayerManager
{
public:
	///Constructs the player manager
	YOGGamePlayerManager(YOGGame* game, GameHeader& gameHeader, YOGGameServer& server);
	///Adds a person to the gameHeader
	void addPerson(Uint16 playerID);
	///This is intended to add an AI to the game
	void addAIPlayer(AI::ImplementitionID type);
	///Removes a person from the gameHeader
	void removePerson(Uint16 playerID);
	///Removes a player, human or AI
	void removePlayer(int playerNumber);
	///Changes the team number of the player
	void changeTeamNumber(int playerNumber, int newTeamNumber);
	///Tells whether a particular player is ready to go
	void setReadyToGo(int playerID, bool isReady);
	///Tells whether all players are ready to go
	bool isEveryoneReadyToGo();
	///Sets the number of teams
	void setNumberOfTeams(int numberOfTeams);

private:
	///Chooses a team number that has the fewest attached players
	int chooseTeamNumber();
	///Sends an update with the new player information
	void sendPlayerInfoUpdate();

	///Represents the basic player information in the game
	GameHeader& gameHeader;
	bool readyToStart[32];
	YOGGame* game;
	int numberOfTeams;
	bool previousReadyToLaunch;
	YOGGameServer& server;

};

#endif

