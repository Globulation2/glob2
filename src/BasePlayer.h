/*
  Copyright (C) 2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef BasePlayer_h
#define BasePlayer_h

#include <SDL_net.h>
#include "AI.h"
#include "Team.h"
#include <string>
/**
 * Player holds the player's state like name, type, id etc.
 */
class BasePlayer
{
public:
	/**
	 * Players can be AI or human players at the local machine or connected via a network.
	 */
 	enum PlayerType
	{
		///A non existin player //NOTE : we don't need any more because null player are not created
		P_NONE=0,
		///Player will be droped in any cases, but we still have to exchange orders
		P_LOST_DROPPING=1,
		///Player is no longer taken into account, may be later changed to P_AI. All orders are NULLs.
		P_LOST_FINAL=2,
		///Player connected over a network (YOG/LAN)
		P_IP=3,
		///local Player
		P_LOCAL=4,
		///An AI. Note : P_AI + n is AI type n
		P_AI=5
	};
	//TODO: Explain
	static AI::ImplementitionID implementitionIdFromPlayerType(PlayerType type)
	{
		assert(type>=P_AI);
		return (AI::ImplementitionID)((int)type-(int)P_AI);
	}
	//TODO: Explain
	static PlayerType playerTypeFromImplementitionID(AI::ImplementitionID iid)
	{
		return (PlayerType)((int)iid+(int)P_AI);
	}
	enum {
		///Maximum length of player names
		MAX_NAME_LENGTH = 32
	};

	PlayerType type;
	//TODO: Explain
	Sint32 number;
	//TODO: Explain
	Uint32 numberMask;
	std::string name;
	Sint32 teamNumber;
	//TODO: Explain
	Uint32 teamNumberMask;
	///true if this player is to quit but still has orders to process
	bool quitting;
	//TODO: Explain
	Uint32 quitUStep;
	//TODO: Explain
	Uint32 lastUStepToExecute;
	///Used to identify the player over the internet
	Uint32 playerID;

public:

	/**
	 *
	 */
	BasePlayer(void);
	/**
      \param number
      \param name
      \param teamn
      \param type
	 */
	BasePlayer(Sint32 number, const std::string& name, Sint32 teamn, PlayerType type);
	//TODO: Explain
	void init();
	virtual ~BasePlayer(void);

	void setNumber(Sint32 number);
	void setTeamNumber(Sint32 teamNumber);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream) const;

	Uint32 checkSum();

	virtual void makeItAI(AI::ImplementitionID aiType);
	//TODO: Explain
	bool disableRecursiveDestruction;
};

#endif
