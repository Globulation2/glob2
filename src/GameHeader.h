/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __GAMEHEADER_H
#define __GAMEHEADER_H

#include "BasePlayer.h"
#include "Stream.h"
#include <list>
#include "WinningConditions.h"
#include <assert.h>

///This is the game header. It is dynamic, and can change from game to game, even
///if the map doesn't. It holds all configurable information for a game, from team
///alliances to customized victory conditions
class GameHeader
{
public:
	///Gives default values to all entries
	GameHeader();

	///Resets the GameHeader to a "blank" state with default values
	void reset();	

	///Loads game header information from the stream
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	
	///Saves game header information to the stream
	void save(GAGCore::OutputStream *stream) const;

	///This loads the game header information from the stream, excluding BasePlayer information
	bool loadWithoutPlayerInfo(GAGCore::InputStream *stream, Sint32 versionMinor);

	///This saves all game header information to the stream, but excluding the BasePlayer information
	void saveWithoutPlayerInfo(GAGCore::OutputStream *stream) const;

	///This loads only the player information from the game header from the stream
	bool loadPlayerInfo(GAGCore::InputStream *stream, Sint32 versionMinor);

	///This saves only the player information from the game header from the stream
	void savePlayerInfo(GAGCore::OutputStream *stream) const;

	///Returns the number of players in the game
	inline Sint32 getNumberOfPlayers() const { return numberOfPlayers; }
	
	///Sets the number of players in the game
	inline void setNumberOfPlayers(Sint32 players) { numberOfPlayers=players; }
	
	///Returns the games latency. This would be 0 for local games, but higher for networked games.
	inline Sint32 getGameLatency() const { return gameLatency; }
	
	///Sets the latency of the game.
	inline void setGameLatency(Sint32 latency) { gameLatency = latency; }
	
	///Returns the order rate. 1 means an order is sent across the net for every frame,
	///2 sends at every second frame, 3 at every 3'rd and so on
	inline Uint8 getOrderRate() const { return orderRate; }
	
	///Sets the order frame rate
	inline void setOrderRate(Uint8 orderRate) { this->orderRate = orderRate; }
	
	///Provides access to the base player. n must be between 0 and 31.
	inline BasePlayer& getBasePlayer(const int n)
	{
		assert(n<Team::MAX_COUNT && n>=0);
		return players[n];
	}
	
	///Provides access to the base player. n must be between 0 and 31.
	inline const BasePlayer& getBasePlayer(const int n) const
	{
		assert(n<Team::MAX_COUNT && n>=0);
		return players[n];
	}
	
	///Returns the ally-team number for the given team for pre-game alliances
	inline Uint8 getAllyTeamNumber(int teamNumber) { return allyTeamNumbers[teamNumber]; }
	
	///Sets the ally-team number for the given team
	inline void setAllyTeamNumber(int teamNumber, Uint8 allyTeam) { allyTeamNumbers[teamNumber]=allyTeam; }
	
	///Returns whether allying and de-allying are allowed mid-game
	inline bool areAllyTeamsFixed() { return allyTeamsFixed; }
	
	///Sets whether ally-teams are fixed during the game
	inline void setAllyTeamsFixed(bool fixed) { allyTeamsFixed = fixed; }
	
	///Returns the list of winning conditions. This list can be modified. Mind, though, the pecking order of winning conditions.
	///Ones first on the list are considered first.
	inline std::list<boost::shared_ptr<WinningCondition> >& getWinningConditions() { return winningConditions; }
	
	///Returns the random generator seed thats being used
	inline Uint32 getRandomSeed() const { return seed; }
	
	///Sets the random generator seed to be used
	inline void setRandomSeed(Uint32 s) { seed = s; }
	
	///Returns whether the map is discovered at game start
	inline bool isMapDiscovered() const { return mapDiscovered; }
	
	///Sets whether the map is discovered at game start
	inline void setMapDiscovered(bool discovered) { mapDiscovered=discovered; }
private:
	///The number of players in the game
	Sint32 numberOfPlayers;

	///The number of ticks between an order issue, and the execution of the order.
	///Used for net games to hide latency.
	Sint32 gameLatency;

	///Sets the order rate
	Uint8 orderRate;

	///Represents the basic player information in the game
	BasePlayer * players;
	
	///Represents the ally team numbers
	Uint8 * allyTeamNumbers;
	
	///Represents whether the ally-teams are fixed for the whole game, so no allying/unallying can take place
	bool allyTeamsFixed;
	
	///Represents the winning conditions of the game.
	std::list<boost::shared_ptr<WinningCondition> > winningConditions;

	///Represents the random seed used for the game
	Uint32 seed;
	
	///Represents whether fog of war is enabled or disabled
	bool mapDiscovered;
};


#endif
