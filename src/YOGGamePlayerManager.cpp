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

#include "YOGGamePlayerManager.h"
#include "FormatableString.h"
#include "YOGGame.h"

using namespace GAGCore;

YOGGamePlayerManager::YOGGamePlayerManager(YOGGame* game, GameHeader& gameHeader, YOGGameServer& server)
	: game(game), gameHeader(gameHeader), server(server)
{
	for(int x=0; x<32; ++x)
	{
		readyToStart[x] = true;
	}
	numberOfTeams = 0;
}

void YOGGamePlayerManager::addPerson(Uint16 playerID)
{
	int team_number = chooseTeamNumber();

	//Add the player into the first spare slot
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type == BasePlayer::P_NONE)
		{
			bp = BasePlayer(x, server.getPlayer(playerID)->getPlayerName().c_str(), team_number, BasePlayer::P_IP);
			bp.playerID = playerID;
			if(playerID != game->getHostPlayerID())
				readyToStart[x] = false;
			break;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers() + 1);

	sendPlayerInfoUpdate();
}



void YOGGamePlayerManager::addAIPlayer(AI::ImplementitionID type)
{
	//16 is current maximum
	if(gameHeader.getNumberOfPlayers() < 16)
	{
		int team_number = chooseTeamNumber();
		for(int x=0; x<32; ++x)
		{
			BasePlayer& bp = gameHeader.getBasePlayer(x);
			if(bp.type == BasePlayer::P_NONE)
			{
				FormatableString name("%0 %1");
				name.arg(AI::getAIText(type)).arg(x+1);
				bp = BasePlayer(x, name, team_number, Player::playerTypeFromImplementitionID(type));
				readyToStart[x] = true;
				break;
			}
		}
		gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers() + 1);
		
		sendPlayerInfoUpdate();
	}
}



void YOGGamePlayerManager::removePerson(Uint16 playerID)
{
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.playerID == playerID)
		{
			removePlayer(x);
			break;
		}
	}
}



void YOGGamePlayerManager::removePlayer(int playerNumber)
{
	//Remove the player. Any players that are after this player are moved backwards
	gameHeader.getBasePlayer(playerNumber) = BasePlayer();
	for(int x=playerNumber+1; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type != Player::P_NONE)
		{
			bp.number -= 1;
			bp.numberMask = 1u>>bp.number;
			if(bp.type >= Player::P_AI)
			{
				FormatableString name("%0 %1");
				name.arg(AI::getAIText(bp.type)).arg(bp.number+1);
				bp.name = name;
			}

			gameHeader.getBasePlayer(x-1) = bp;
			bp = BasePlayer();
			readyToStart[x-1] = readyToStart[x];
			readyToStart[x] = true;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers() - 1);

	sendPlayerInfoUpdate();
}



void YOGGamePlayerManager::changeTeamNumber(int playerNumber, int newTeamNumber)
{
	//Changes the team
	gameHeader.getBasePlayer(playerNumber).teamNumber = newTeamNumber;
	
	sendPlayerInfoUpdate();
}



void YOGGamePlayerManager::setReadyToGo(int playerID, bool isReady)
{
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.playerID == playerID)
		{
			readyToStart[x] = isReady;
			break;
		}
	}
}



bool YOGGamePlayerManager::isEveryoneReadyToGo()
{
	for(int x=0; x<32; ++x)
	{
		if(readyToStart[x] == false)
		{
			return false;
		}
	}
	return true;
}



void YOGGamePlayerManager::setNumberOfTeams(int nnumberOfTeams)
{
	numberOfTeams = nnumberOfTeams;
}



int YOGGamePlayerManager::chooseTeamNumber()
{
	//Find a spare team number to give to the player. If there aren't any, recycle a number that has the fewest number of attached players
	//Count number of players for each team
	std::vector<int> numberOfPlayersPerTeam(32, 0);
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type != BasePlayer::P_NONE)
			numberOfPlayersPerTeam[bp.teamNumber] += 1;
	}
	//Chooes a team number that has the lowest number of players attached
	int lowest_number = 10000;
	int team_number = 0;
	for(int x=0; x<numberOfTeams; ++x)
	{
		if(numberOfPlayersPerTeam[x] < lowest_number)
		{
			lowest_number = numberOfPlayersPerTeam[x];
			team_number  = x;
		}
	}
	return team_number;
}



void YOGGamePlayerManager::sendPlayerInfoUpdate()
{
	boost::shared_ptr<NetSendGamePlayerInfo> message(new NetSendGamePlayerInfo(gameHeader));
	game->routeMessage(message);
}



