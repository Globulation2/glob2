/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#include "GAG.h"
#include "Order.h"
#include "Player.h"

class NetGame
{
public:
	NetGame(UDPsocket socket, int numberOfPlayer, Player *players[32]);
	~NetGame();

	void printQueue(char *str);
	void orderHasBeenExecuted(Order *order);
	Order *getOrder(Sint32 playerNumber);
	void pushOrder(Order *order, Sint32 playerNumber);
	void step(void);
	void init(void);
	
private:
	void treatData(char *data, int size, IPaddress ip);
	bool isStepReady(Sint32 step);
	int numberStepsReady(Sint32 step);
public:
	int advance();
private:
	bool smaller(int a, int b);
	bool smalleroe(int a, int b);
	bool nextUnrecievedStep(int currentStep, int *player, int *step);
	bool nextUnrecievedStep(int currentStep, int player, int *step);
	Uint32 whoMaskAreWeWaitingFor(Sint32 step);
	void confirmNewStepRecievedFromHim(Sint32 recievedStep, Sint32 recievedFromPlayerNumber);
	void sendMyOrderThroughUDP(Order *order, Sint32 orderStep, Sint32 targetPlayer, Sint32 confirmedStep);

	Sint32 numberOfPlayer;
	Sint32 localPlayerNumber;
	Sint32 currentStep;
	Player *players[32];

	enum {queueSize=256};//256
	enum {latency=10};
	enum {lostPacketLatencyMargin=2};
	enum {MAX_GAME_PACKET_SIZE=2000};
	enum {COUNT_DOWN_MIN=16};
	enum {COUNT_DOWN_DEATH=200};

	typedef struct
	{
		Order *order;
		Sint32 packetID;
		Sint32 ackID;
	} playersNetQueueStruct;
	
	playersNetQueueStruct playersNetQueue[32][queueSize];
	bool isWaitingForPlayer;

	Uint32 lastReceivedFromMe[32];
	Uint32 lastReceivedFromHim[32];
	
	enum DropState
	{
		NO_DROP_PROCESSING=0,
		STARTING_DROPPING_PROCESS=1,
		ONE_STAY_MASK_RECIEVED=2,
		CROSS_SENDING_STAY_MASK=3,
		ALL_STAY_MASK_RECIEVED=4
	};
	
	int countDown[32];
	Uint32 stayingPlayersMask[32];
	DropState dropState;
	int lastAviableStep[32][32];
	std::list<Order *> localOrderQueue[32];
	
	UDPsocket socket;
	
	int time;

public:
	bool isNowWaiting();

private:
	Sint32 checkSumsLocal[queueSize];
	Sint32 checkSumsRemote[queueSize];
	
private:
	FILE *logFile;
};

#endif
 
