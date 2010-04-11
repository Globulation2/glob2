/*
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

#ifndef __AI_H
#define __AI_H

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_rwops.h>
#else
#include <Types.h>
#endif

#include <boost/shared_ptr.hpp>
namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Order;
class AIImplementation;
/*
 * AI is the base class for the AI-implementations
 */
class AI
{
public:
	///TODO: Explain
	enum ImplementitionID
	{
		///Reference to AINull
		NONE=0,
		///Reference to AINumbi
		NUMBI=1,
		///Reference to AICastor
		CASTOR=2,
		///Reference to AIWarrush
		WARRUSH=3,
		///Reference to the AIEcho based AIReachToInfinity
		REACHTOINFINITY=4,
		///Reference to the AIEcho based AINicowar
		NICOWAR=5,

		SIZE,
		TOUBIB,

		EXPERIMENTAL_SIZE
	};
	static const ImplementitionID toggleAI=CASTOR;

public:
	//AI(Player *player); //TODO: remove this constructor, and choose the AI the user wants.
	AI(ImplementitionID implementitionID, Player *player);
	AI(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	~AI();
	//void init(ImplementitionID ImplementitionID, Player *player);

	AIImplementation *aiImplementation;
	ImplementitionID implementitionID;

	Player *player;

	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);

	static std::string getAIText(int id);

	boost::shared_ptr<Order> getOrder(bool paused);

//	Uint32 step;
};

#endif
