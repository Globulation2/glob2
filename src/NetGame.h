/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __NETGAME_H
#define __NETGAME_H

/*
What's in NetGame ?

NetGame is used by the engine to push and pop order for every players.
(local players, AIs and remote players)

Without latency, it could be a simple list of Orders, one for each player.

But the aim of NetGame is to hide the latency (and lost orders)
for all the rest of the engine and the program.
Then, for each player, you have a queue of order.
Even if you are not playing network games, locals players and AIs, allways have n="latency" orders in queue.
You have 0 to 2*"latency" order for remote players. (With or without gaps)
Some order may be missing, because network layer (UDP) may have lost them.
At playersNetQueue[player][currentStep] you have all orders that have to be executed this turn.
But if only one is missing, the engine must NOT execute any of them. (I mean NO order at all).
Therefore, if one ore more is missig "Order *getOrder(Sint32 playerNumber)"
will return a "WatingForPlayerOrder" for all player.
This way the engine is protected from exectutng order asynchronously.

void orderHasBeenExecuted() is only here for memory management system,
and is not conceptualy important for network and engine.

void pushOrder(Order *order, Sint32 playerNumber) has fo be called for each AI and the local player.

int advance() represents the time to wait because your computer has probably
executed mor orders than other other computer playing the same game.
This is totaly pragamtic, and it need to be ehanced. The engine has to call
it and slow the game if needed.

NetGame also have to drop player if he is definitely unreachable.
int countDown[32] is used that for.

NetGame is not TCP/IP friendly.
I'm really sorry, but I'm not yet able to design this.

The engine has to call NetGame::step() to give him time to process.
*/

#include "Header.h"
#include <SDL/SDL_net.h>

class Player;
class Order;

class NetGame
{
public:
	NetGame(UDPsocket socket, int numberOfPlayer, Player *players[32]);
	~NetGame();
	void init(void);
	
private:
	Uint32 whoMaskAreWeWaitingFor(void);
	Uint32 whoMaskCountedOut(void);
	Uint8 lastReceivedFromHim(int player);
	void sendPushOrder(int targetPlayer);
	void sendWaitingForPlayerOrder(int targetPlayer);
	void sendDroppingPlayersMask(int targetPlayer, bool askForReply);
	void sendRequestingDeadAwayOrder(int missingPlayer, int targetPlayer, Uint8 resendingStep);
	void sendDeadAwayOrder(int missingPlayer, int targetPlayer, Uint8 resendingStep);
	
public:
	void pushOrder(Order *order, int playerNumber);
	Order *getOrder(int playerNumber);
	void orderHasBeenExecuted(Order *order);

private:
	void updateDelays(int player, Uint8 receivedStep);
	void computeMyLocalWishedLatency(void);
	void treatData(Uint8 *data, int size, IPaddress ip);

public:
	void receptionStep(void);
	bool stepReadyToExecute(void);
	bool computeNumberOfStepsToEat(void); //Return false if failed
	void stepExecuted(void);
	int ticksToDelay(void);
	void setWishedDelay(int delay);
	
private:
	int numberOfPlayer;
	int localPlayerNumber;
	
	// The next step to push an order at.
	// Should points to a freed order.
	Uint8 pushStep;
	
	// The next step to be executed.
	// Should points to a valid order, unless network possible packet loss.
	// Mainly used by getOrder().
	Uint8 executeStep;
	
	// The next step to be freed.
	// Have to points to a valid order.
	Uint8 freeingStep;
	
	// This is the number of orders by packet:
	int ordersByPackets;
	
	Uint32 waitingForPlayerMask;
	bool hadToWaitThisStep;
	int numberOfStepsToEat;
	
	Player *players[32];

	static const int defaultLatency=8;//320[ms]
	static const int maxLatency=24;//960[ms]
	
	Uint8 myLocalWishedLatency; // The latency we want, but the other players don't know about it yet. (caused by network latency)
	Uint8 wishedLatency[32]; // The latency each player wants.
	Uint8 myLocalWishedDelay; // The delay we want, but the other players don't know about it yet. (caused by a too slow computer)
	Uint8 recentsWishedDelay[32][64]; // The delay each player wants. (recents)
	
	static const int MAX_GAME_PACKET_SIZE=1500;
	static const int COUNT_DOWN_DEATH=100;
	
	Order *ordersQueue[32][256];
	Uint8 lastReceivedFromMe[32];
	
	int countDown[32];
	Uint32 droppingPlayersMask[32];
	enum DropState
	{
		DS_NO_DROP_PROCESSING=0,
		DS_EXCHANGING_DROPPING_MASK=1,
		DS_EXCHANGING_ORDERS=2,
		
	};
	DropState dropState;
	Uint8 theLastExecutedStep;
	Uint8 lastExecutedStep[32];
	Uint8 lastAviableStep[32][32];
	
	// We want tu update latency automatically:
	Uint8 recentsPingPong[32][64]; // The 64 last ping+pong times. [ticks]
	Uint8 pingPongCount[32]; // The next recentDelays[][x] to write in. [ticks]
	Uint8 pingPongMin[32];
	Uint8 pingPongMax[32]; // Average-like and usable ping+pong time to a player. [ticks]
	
	Uint8 recentOrderMarginTime[32][64]; // The 64 last, time before we run out of orders. [ticks]
	Uint8 orderMarginTimeCount[32];
	Uint8 orderMarginTimeMin[32];
	Uint8 orderMarginTimeMax[32];
	
	UDPsocket socket;

	Sint32 checkSumsLocal[256];
	Sint32 checkSumsRemote[256];
	
	FILE *logFile;
};

#endif
 
