// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "BasePlayer.h"
#include "Stream.h"
#include <list>
#include <optional>
#include <vector>
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

	///True if any player in this game is a live network peer (BasePlayer::P_IP,
	///covering both YOG and LAN). Used to disable local-only controls such as
	///hard-pause that would desync a networked game if one client toggled them.
	inline bool hasNetworkPlayer() const
	{
		for (int n=0; n<numberOfPlayers; n++)
			if (players[n].type==BasePlayer::P_IP)
				return true;
		return false;
	}

	///Returns the ally-team number for the given team for pre-game alliances.
	///Ally team numbers are 1-based (the constructor assigns team i the value
	///i+1); 0 only appears in a malformed or hand-edited header.
	inline Uint8 getAllyTeamNumber(int teamNumber)
	{
		assert(teamNumber >= 0 && teamNumber < Team::MAX_COUNT);
		return allyTeamNumbers[teamNumber];
	}

	///Sets the ally-team number (1-based, see getAllyTeamNumber) for the given
	///team. The header is replicated to all clients, so an out-of-range write
	///here would silently corrupt shared state -- hence the assert.
	inline void setAllyTeamNumber(int teamNumber, Uint8 allyTeam)
	{
		assert(teamNumber >= 0 && teamNumber < Team::MAX_COUNT);
		allyTeamNumbers[teamNumber]=allyTeam;
	}

	///Applies the custom-game default alliance layout. All ally-team numbers
	///are first reset to the free-for-all default (team i on singleton group
	///i+1, as in reset()), then the human's team joins ally team 1 and every
	///AI team with a different color joins ally team 2. An AI sharing the
	///human's color shares its team -- alliances are per-team -- so it stays
	///on the human's ally group. With no human selection (humanColor empty)
	///the free-for-all layout is kept: a human-vs-AI split is meaningless
	///without a human. Colors are team indices, [0, Team::MAX_COUNT).
	void setDefaultAlliances(std::optional<int> humanColor, const std::vector<int>& aiColors);
	
	///Returns whether allying and de-allying are allowed mid-game
	inline bool areAllyTeamsFixed() { return allyTeamsFixed; }
	
	///Sets whether ally-teams are fixed during the game
	inline void setAllyTeamsFixed(bool fixed) { allyTeamsFixed = fixed; }
	
	///Returns the list of winning conditions. This list can be modified. Mind, though, the pecking order of winning conditions.
	///Ones first on the list are considered first.
	inline std::list<std::shared_ptr<WinningCondition> >& getWinningConditions() { return winningConditions; }
	
	///Returns the random generator seed thats being used
	inline Uint32 getRandomSeed() const { return seed; }
	
	///Sets the random generator seed to be used
	inline void setRandomSeed(Uint32 s) { seed = s; }
	
	///Returns whether the map is discovered at game start
	inline bool isMapDiscovered() const { return mapDiscovered; }
	
	///Sets whether the map is discovered at game start
	inline void setMapDiscovered(bool discovered) { mapDiscovered=discovered; }

	///Returns whether resources are allowed to grow/spread over time (custom-game rule)
	inline bool isResourceGrowthDisabled() const { return resourceGrowthDisabled; }

	///Sets whether resources are allowed to grow/spread over time (custom-game rule)
	inline void setResourceGrowthDisabled(bool disabled) { resourceGrowthDisabled=disabled; }

	///Returns the resource-scarcity tier (0=off/today's rate, 1-3=progressively slower growth)
	inline Uint8 getResourceScarcityLevel() const { return resourceScarcityLevel; }

	///Sets the resource-scarcity tier (custom-game rule)
	inline void setResourceScarcityLevel(Uint8 level) { resourceScarcityLevel=level; }

	///Returns whether buildings complete construction instantly (custom-game rule)
	inline bool isInstantConstructionEnabled() const { return instantConstruction; }

	///Sets whether buildings complete construction instantly (custom-game rule)
	inline void setInstantConstructionEnabled(bool enabled) { instantConstruction=enabled; }

	///Returns the stockpile-start tier (0=none/today's default, 1-3=progressively larger
	///starting amount seeded into each team's shared market/exchange resource pool)
	inline Uint8 getStockpileStartLevel() const { return stockpileStartLevel; }

	///Sets the stockpile-start tier (custom-game rule)
	inline void setStockpileStartLevel(Uint8 level) { stockpileStartLevel=level; }

	///Returns whether units are exempt from hunger and starvation (custom-game rule)
	inline bool isHungerDisabled() const { return hungerDisabled; }

	///Sets whether units are exempt from hunger and starvation (custom-game rule)
	inline void setHungerDisabled(bool disabled) { hungerDisabled=disabled; }
private:
	///The number of players in the game
	Sint32 numberOfPlayers;

	///The number of ticks between an order issue, and the execution of the order.
	///Used for net games to hide latency.
	Sint32 gameLatency;

	///Sets the order rate
	Uint8 orderRate;

	///Represents the basic player information in the game
	BasePlayer players[Team::MAX_COUNT];
	
	///Represents the ally team numbers
	Uint8 allyTeamNumbers[Team::MAX_COUNT];
	
	///Represents whether the ally-teams are fixed for the whole game, so no allying/unallying can take place
	bool allyTeamsFixed;
	
	///Represents the winning conditions of the game.
	std::list<std::shared_ptr<WinningCondition> > winningConditions;

	///Represents the random seed used for the game
	Uint32 seed;
	
	///Represents whether fog of war is enabled or disabled
	bool mapDiscovered;

	///Custom-game rule: resources never grow/spread (see Map::growResources)
	bool resourceGrowthDisabled;

	///Custom-game rule: 0-3 tier scaling down how often resources grow/spread
	Uint8 resourceScarcityLevel;

	///Custom-game rule: building sites complete immediately, skipping resource delivery
	bool instantConstruction;

	///Custom-game rule: 0-3 tier seeding each team's shared resource pool at game start
	Uint8 stockpileStartLevel;

	///Custom-game rule: units never grow hungry or starve
	bool hungerDisabled;
};


