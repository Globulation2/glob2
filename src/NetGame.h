/*
 * Globulation 2 Net during Game support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
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
	std::queue<Order *> localOrderQueue[32];
	
	UDPsocket socket;
	
	int time;
	
public:
	Sint32 checkSumsLocal[queueSize];
	Sint32 checkSumsRemote[queueSize];
};

#endif
 
