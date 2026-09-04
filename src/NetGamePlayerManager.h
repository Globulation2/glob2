// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef NetGamePlayerManager_h
#define NetGamePlayerManager_h

#include "GameHeader.h"
#include "NetReteamingInformation.h"
class YOGServerGame;

///This class handles the players and AI's that can join, be kicked out of, disconnect, leave
///and otherwise be mangled arround with in an online game during the setup stage
class NetGamePlayerManager
{
public:
	///Constructs the player manager
	NetGamePlayerManager(GameHeader& gameHeader);
	///Adds a person to the gameHeader
	void addPerson(Uint16 playerID, const std::string& name);
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
	///Returns true if a particular player is ready to go
	bool isReadyToGo(int playerID);
	///Sets the number of teams
	void setNumberOfTeams(int numberOfTeams);
	///Sets the reteaming information. Reteaming is when you reload a YOG save
	///game in YOG, and if the same players join, they are automatically set to
	///the team they where in the save game
	void setReteamingInformation(const NetReteamingInformation& information);
	///This returns the reteaming information
	const NetReteamingInformation& getReteamingInformation() const;
private:
	///Chooses a team number that has the fewest attached players
	int chooseTeamNumber();

	///Represents the basic player information in the game
	GameHeader& gameHeader;
	bool readyToStart[Team::MAX_COUNT];
	int numberOfTeams;
	bool previousReadyToLaunch;
	NetReteamingInformation reteamInfo;
};

#endif

