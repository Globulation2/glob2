/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

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

#include "GAG.h"
#include "Order.h"
#include "Player.h"

class NetGame
{
public:
	NetGame(UDPsocket socket, int numberOfPlayer, Player *players[32]);
	~NetGame();

	Order *getOrder(Sint32 playerNumber);
	void orderHasBeenExecuted(Order *order);
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
	std::queue<Order *> localOrderQueue[32];
	
	UDPsocket socket;
	
	int time;

public:
	bool isNowWaiting();

private:
	Sint32 checkSumsLocal[queueSize];
	Sint32 checkSumsRemote[queueSize];
};

#endif
 
