/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

/*
conventions:
-default timing unit is a tick. 1[t]=40[ms]
-all iterators take a "i" suffix. ("pi"=player iterator, "si"=step iterator, "di"=delay iterator, "ri"=recent iterator)

assertions:
-No game will last longer than 5.4 years (2^32*40[ms]), or user will take time to save it before :P

order's steps tricks:
-"usteps" are Uint32 uniques tick identifiers.
-(ustep==0) means no order. (exception for the very first unsent order, which nobody cares).
-Lowers bits of usteps are used as a hash to store orders in "ordersQueue".
-All orders are kept in memory trough "ordersQueue" until another mor recent order has the same hash.
-computeNumberOfStepsToEat() should compute "numberOfStepsToEat==1",
	except when latency is increased (numberOfStepsToEat==0), or decreased (numberOfStepsToEat==2).

*/

#include "Header.h"
#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_net.h>
#else
#include <Types.h>
#endif
#include <vector>

class Player;
class Order;

class NetGame
{
public:
	NetGame(UDPsocket socket, int numberOfPlayer, Player *players[32]);
	~NetGame();
	void init(void);
	void dumpStats();
	void initStats();
	static const bool verbose = false;
	
private:
	Uint32 whoMaskAreWeWaitingFor(void); // Uses executeUStep
	Uint32 whoMaskCountedOut(void); // Players who has been late for more than 1 tick. Used to avoid flicking of the "away player" message.
	Uint32 lastUsableUStepReceivedFromHim(int player);
	void sendPushOrder(int targetPlayer); // Uses pushUStep
	void sendWaitingForPlayerOrder(int targetPlayer);
	void sendDroppingPlayersMask(int targetPlayer, bool askForReply);
	void sendRequestingDeadAwayOrder(int missingPlayer, int targetPlayer, Uint32 resendingUStep);
	void sendDeadAwayOrder(int missingPlayer, int targetPlayer, Uint32 resendingUStep);
	
public:
	void pushOrder(Order *order, int playerNumber);
	Order *getOrder(int playerNumber);

private:
	void computeMyLocalWishedLatency(void);
	void treatData(Uint8 *data, int size, IPaddress ip);

public:
	void receptionStep(void);
	bool stepReadyToExecute(void);
	bool computeNumberOfStepsToEat(void); //Return false if failed
	void stepExecuted(void);
	int ticksToDelayInside(void);
	void setLeftTicks(int leftTicks);
	std::vector<Uint32> *getCheckSumsVectorsStorage();
	std::vector<Uint32> *getCheckSumsVectorsStorageForBuildings();
	std::vector<Uint32> *getCheckSumsVectorsStorageForUnits();
	
private:
	int numberOfPlayer;
	int localPlayerNumber;
	
	// The next step to push an order at.
	// Should points to a freed order.
	Uint32 pushUStep;
	
	// The next step to be executed.
	// Should points to a valid order, unless network possible packet loss.
	// Mainly used by getOrder().
	Uint32 executeUStep;
	
	// This is the number of orders by packet:
	int ordersByPackets;
	
	Uint32 waitingForPlayerMask;
	bool hadToWaitThisStep;
	int numberOfStepsToEat;
	
	Player *players[32];

	static const int defaultLatency=8; //8[t]x40[ms/t]=320[ms]
	static const int maxLatency=24; //24[t]x40[ms/t]=960[ms]
	
	Uint8 myLocalWishedLatency; // The latency we want, but the other players don't know it yet. (caused by network latency)
	Uint8 wishedLatency[32]; // The latency each player wants.
	int myLocalWishedDelay; // The delay we want, but the other players don't know about it yet. (caused by a too slow computer)
	Uint8 recentsWishedDelay[32][256]; // The delay each player wants. (recents)
	
	// Max packet size to send for NetGame. Set to the size of the internet MTU minus IP (20 bytes) and UDP (8 bytes) headers
	static const int MAX_GAME_PACKET_SIZE = 1500 - 20 - 8;
	static const int countDownMax=400; //400[t]x40[ms/t]=16'000[ms]
	
	Order *ordersQueue[32][256];
	Uint32 lastUStepReceivedFromMe[32];
	Uint32 lastSdlTickReceivedFromHim[32];
	
	int countDown[32];
	Uint32 droppingPlayersMask[32];
	enum DropState
	{
		DS_NoDropProcessing=0,
		DS_ExchangingDroppingMasks=1,
		DS_ExchangingOrders=2
	};
	bool dropStatusCommuniquedToGui[32];
	
	DropState dropState;
	Uint8 theLastExecutedStep;
	Uint32 lastExecutedUStep[32];
	Uint32 lastAvailableUStep[32][32];
	
	// We want tu update latency automatically:
	int recentsPingPong[32][1024]; // [player][id++] The 1024 last ping+pong times. [ms]
	
	UDPsocket socket;

	Sint32 gameCheckSums[32][256];
	std::vector<Uint32> checkSumsVectorsStorage[256];
	std::vector<Uint32> checkSumsVectorsStorageForBuildings[256];
	std::vector<Uint32> checkSumsVectorsStorageForUnits[256];
	
	FILE *logFile;
	FILE *logFileEnd;
protected:
	int delayInsideStats[40];
	int wishedDelayStats[40];
	int maxMedianWishedDelayStats[40];
	int goodLatencyStats[40];
	int duplicatedPacketStats[32];
	
	int pingPongStats[32][1024]; //[player][ms]
};

#endif
 
