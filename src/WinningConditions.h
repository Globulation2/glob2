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

#ifndef WinningConditions_h
#define WinningConditions_h

#include "boost/shared_ptr.hpp"
#include "SDL_net.h"
#include <list>


class Game;

namespace GAGCore
{
	class OutputStream;
	class InputStream;
};

///These are the type of winning conditions there are
enum WinningConditionType
{
	WCUnknown,
	WCDeath,
	WCAllies,
	WCPrestige,
	WCScript,
	WCOpponentsDefeated,
};

///This represents a generic winning condition. Each condition may specify which teams have won,
///which have lost, or possibly, both
class WinningCondition
{
public:
	// These two methods in WinningConditionScript depend on SGSL.cpp,
	// but they aren't needed for server.
#ifndef YOG_SERVER_ONLY
	///Returns true if the particular player has won according to this winning condition
	virtual bool hasTeamWon(int team, Game* game)=0;
	///Returns true if the particular player has lost according to this winning condition
	virtual bool hasTeamLost(int team, Game* game)=0;
#endif  // !YOG_SERVER_ONLY
	///Returns the winning condition type
	virtual WinningConditionType getType() const=0;
	///This will encode the data in this winning condition to a stream. All derived class must start by saving a Uint8 from getType()
	virtual void encodeData(GAGCore::OutputStream* stream) const = 0;
	///This will decode data. It is important that, unlike encodeData, this must ignore the initial Uint8
	virtual void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)=0;
	
	///This will reconstruct a winning condition from serialized data
	static boost::shared_ptr<WinningCondition> getWinningCondition(GAGCore::InputStream* stream, Uint32 versionMinor);
	///This will set the given list to the default set of winning conditions, in their default order
	static std::list<boost::shared_ptr<WinningCondition> > getDefaultWinningConditions();
	
};

///A team has lost if its dead.
class WinningConditionDeath : public WinningCondition
{
public:
	bool hasTeamWon(int team, Game* game);
	bool hasTeamLost(int team, Game* game);
	WinningConditionType getType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
};

///A team has won if one of its allies has won
class WinningConditionAllies : public WinningCondition
{
public:
	bool hasTeamWon(int team, Game* game);
	bool hasTeamLost(int team, Game* game);
	WinningConditionType getType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
};

///A team has won if the prestige limit is reached and its above the prestige amount
///and a team has lost if the prestige limit is reached and its below the prestige amount
class WinningConditionPrestige : public WinningCondition
{
public:
	bool hasTeamWon(int team, Game* game);
	bool hasTeamLost(int team, Game* game);
	WinningConditionType getType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
};

///A team has won if the script says it has won, and lost if the script says it has lost
class WinningConditionScript : public WinningCondition
{
public:
#ifndef YOG_SERVER_ONLY
	bool hasTeamWon(int team, Game* game);
	bool hasTeamLost(int team, Game* game);
#endif  // !YOG_SERVER_ONLY
	WinningConditionType getType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
};

///A team has won if all enemies have lost
class WinningConditionOpponentsDefeated : public WinningCondition
{
public:
	bool hasTeamWon(int team, Game* game);
	bool hasTeamLost(int team, Game* game);
	WinningConditionType getType() const;
	void encodeData(GAGCore::OutputStream* stream) const;
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
};



#endif
