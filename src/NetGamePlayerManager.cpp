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

#include "NetGamePlayerManager.h"
#include "FormatableString.h"
#include "YOGGame.h"

using namespace GAGCore;

NetGamePlayerManager::NetGamePlayerManager(GameHeader& gameHeader)
	: gameHeader(gameHeader)
{
	for(int x=0; x<32; ++x)
	{
		readyToStart[x] = true;
	}
	numberOfTeams = 0;
}

void NetGamePlayerManager::addPerson(Uint16 playerID, const std::string& name)
{
	int team_number = chooseTeamNumber();

	//Add the player into the first spare slot
	for(int x=0; x<32; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type == BasePlayer::P_NONE)
		{
			bp = BasePlayer(x, name, team_number, BasePlayer::P_IP);
			bp.playerID = playerID;
			if(gameHeader.getNumberOfPlayers() != 0)
				readyToStart[x] = false;
			break;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers() + 1);
}



void NetGamePlayerManager::addAIPlayer(AI::ImplementitionID type)
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
	}
}



void NetGamePlayerManager::removePerson(Uint16 playerID)
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



void NetGamePlayerManager::removePlayer(int playerNumber)
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
				name.arg(AI::getAIText(bp.type - (int)Player::P_AI)).arg(bp.number+1);
				bp.name = name;
			}

			gameHeader.getBasePlayer(x-1) = bp;
			bp = BasePlayer();
			readyToStart[x-1] = readyToStart[x];
			readyToStart[x] = true;
		}
	}
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers() - 1);
}



void NetGamePlayerManager::changeTeamNumber(int playerNumber, int newTeamNumber)
{
	//Changes the team
	gameHeader.getBasePlayer(playerNumber).teamNumber = newTeamNumber;
}



void NetGamePlayerManager::setReadyToGo(int playerID, bool isReady)
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



bool NetGamePlayerManager::isEveryoneReadyToGo()
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



void NetGamePlayerManager::setNumberOfTeams(int nnumberOfTeams)
{
	numberOfTeams = nnumberOfTeams;
}



int NetGamePlayerManager::chooseTeamNumber()
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



