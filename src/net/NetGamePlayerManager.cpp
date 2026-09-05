// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "NetGamePlayerManager.h"
#include "FormatableString.h"
#include "Player.h"
#include "AINames.h"

using namespace GAGCore;

NetGamePlayerManager::NetGamePlayerManager(GameHeader& gameHeader)
	: gameHeader(gameHeader)
{
	for(int x=0; x<Team::MAX_COUNT; ++x)
	{
		readyToStart[x] = true;
	}
	numberOfTeams = 0;
}

void NetGamePlayerManager::addPerson(YOGPlayerID playerID, const std::string& name)
{
	int team_number;
	if(reteamInfo.doesPlayerHaveTeam(name))
	{
		team_number = reteamInfo.getPlayersTeam(name);
	}
	else
	{
		team_number = chooseTeamNumber();
	}


	//Add the player into the first spare slot
	for(int x=0; x<Team::MAX_COUNT; ++x)
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
		for(int x=0; x<Team::MAX_COUNT; ++x)
		{
			BasePlayer& bp = gameHeader.getBasePlayer(x);
			if(bp.type == BasePlayer::P_NONE)
			{
				FormatableString name("%0 %1");
				name.arg(AINames::getAIText(type)).arg(x+1);
				bp = BasePlayer(x, name, team_number, Player::playerTypeFromImplementitionID(type));
				readyToStart[x] = true;
				break;
			}
		}
		gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers() + 1);
	}
}



void NetGamePlayerManager::removePerson(YOGPlayerID playerID)
{
	for(int x=0; x<Team::MAX_COUNT; ++x)
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
	for(int x=playerNumber+1; x<Team::MAX_COUNT; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type != Player::P_NONE)
		{
			bp.setNumber(bp.number - 1);
			if(bp.type >= Player::P_AI)
			{
				FormatableString name("%0 %1");
				name.arg(AINames::getAIText(bp.type - (int)Player::P_AI)).arg(bp.number+1);
				bp.name = name;
			}

			gameHeader.getBasePlayer(x-1) = bp;
			bp = BasePlayer();
			readyToStart[x-1] = readyToStart[x];
			readyToStart[x] = true;
		}
	}
	readyToStart[playerNumber] = true;
	gameHeader.setNumberOfPlayers(gameHeader.getNumberOfPlayers() - 1);
}



void NetGamePlayerManager::changeTeamNumber(int playerNumber, int newTeamNumber)
{
	//Changes the team
	gameHeader.getBasePlayer(playerNumber).teamNumber = newTeamNumber;
}



void NetGamePlayerManager::setReadyToGo(YOGPlayerID playerID, bool isReady)
{
	for(int x=0; x<Team::MAX_COUNT; ++x)
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
	for(int x=0; x<Team::MAX_COUNT; ++x)
	{
		if(readyToStart[x] == false)
		{
			return false;
		}
	}
	return true;
}



bool NetGamePlayerManager::isReadyToGo(YOGPlayerID playerID)
{
	for(int x=0; x<Team::MAX_COUNT; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.playerID == playerID)
		{
			return readyToStart[x];
		}
	}
	return false;//to satisfy -Wall
}



void NetGamePlayerManager::setNumberOfTeams(int nnumberOfTeams)
{
	numberOfTeams = nnumberOfTeams;
}



void NetGamePlayerManager::setReteamingInformation(const NetReteamingInformation& information)
{
	reteamInfo = information;
}



const NetReteamingInformation& NetGamePlayerManager::getReteamingInformation() const
{
	return reteamInfo;
}



int NetGamePlayerManager::chooseTeamNumber()
{
	//Find a spare team number to give to the player. If there aren't any, recycle a number that has the fewest number of attached players
	//Count number of players for each team
	std::vector<int> numberOfPlayersPerTeam(Team::MAX_COUNT, 0);
	for(int x=0; x<Team::MAX_COUNT; ++x)
	{
		BasePlayer& bp = gameHeader.getBasePlayer(x);
		if(bp.type != BasePlayer::P_NONE)
			numberOfPlayersPerTeam[bp.teamNumber] += 1;
	}
	//Chooes a team number that has the lowest number of players attached
	int lowest_number = TEAM_PLAYERCOUNT_INFINITY;
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


