/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __GAMEHEADER_H
#define __GAMEHEADER_H

#include "Player.h"
#include "Stream.h"

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

	///Returns the number of players in the game
	Sint32 getNumberOfPlayers() const;
	
	///Sets the number of players in the game
	void setNumberOfPlayers(Sint32 players);
	
	///Returns the games latency. This would be 0 for local games, but higher for networked games.
	Sint32 getGameLatency() const;
	
	///Sets the latency of the game.
	void setGameLatency(Sint32 latency);
	
	///Provides access to the base player. n must be between 0 and 31.
	BasePlayer& getBasePlayer(const int n);
private:
	///The number of players in the game
	Sint32 numberOfPlayers;

	///The number of ticks between an order issue, and the execution of the order.
	///Used for net games to hide latency.
	Sint32 gameLatency;

	///Represents the basic player information in the game
	BasePlayer players[32];
};


#endif
