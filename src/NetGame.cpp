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

#include "NetGame.h"
#include "Order.h"
#include "GlobalContainer.h"
#include "NetDefine.h"
#include <assert.h>
#include "Marshaling.h"

NetGame::NetGame(UDPsocket socket, int numberOfPlayer, Player *players[32])
{
	// we have right number of player & valid players
	assert((numberOfPlayer>=0) && (numberOfPlayer<=32));
	assert(players);
	assert((latency+1)<queueSize);//Needed for NULL orders to mark recieved & unrecieved orders.
	assert(latency>1);

	// we initialize and copy stuff
	this->numberOfPlayer=numberOfPlayer;
	memcpy(this->players, players, numberOfPlayer*sizeof(Player *));

	// we find local player
	localPlayerNumber=-1;
	int n;
	for (n=0;n<numberOfPlayer;n++)
	{
		assert(players[n]);
		if (players[n]->type==Player::P_LOCAL)
			localPlayerNumber=n;
	}
	isWaitingForPlayer=false;

	this->socket=socket;
	
	assert(localPlayerNumber!=-1);
	
	logFile=globalContainer->logFileManager.getFile("NetGame.log");
	assert(logFile);
	
	init();
};

NetGame::~NetGame()
{
	for (int step=0; step<queueSize; step++)
		for (int eachPlayers=0; eachPlayers<numberOfPlayer; eachPlayers++)
		{
			if (playersNetQueue[eachPlayers][step].order)
			{
				delete playersNetQueue[eachPlayers][step].order;
				playersNetQueue[eachPlayers][step].order=NULL;
			}
		}
	for (int eachPlayers=0; eachPlayers<numberOfPlayer; eachPlayers++)
	{
		while(localOrderQueue[eachPlayers].size()>0)
		{
			Order *o=localOrderQueue[eachPlayers].front();
			delete o;
			localOrderQueue[eachPlayers].pop_front();
		}
	}
	
	if (socket)
	{
		// We have to prevents players to close the same socket:
		for (int eachPlayers=0; eachPlayers<numberOfPlayer; eachPlayers++)
			if (players[eachPlayers]->socket==socket)
				players[eachPlayers]->channel=-1;
		SDLNet_UDP_Close(socket);
		socket=NULL;
	}
}

void NetGame::init(void)
{
	//Notice that "playersNetQueue[each][0].order" won't be given at beginning, but "playersNetQueue[each][1].order" will.
	currentStep=0;
	int step;
	int eachPlayers;

	for (int i=0; i<queueSize; i++)
	{
		checkSumsLocal[i]=0;
		checkSumsRemote[i]=0;
	}
	
	
	for (step=0; step<queueSize; step++)// Only for initialisation safety
	{
		for (eachPlayers=0; eachPlayers<numberOfPlayer; eachPlayers++)
		{
			playersNetQueue[eachPlayers][step].order=NULL;
			playersNetQueue[eachPlayers][step].packetID=-1;
			playersNetQueue[eachPlayers][step].ackID=-1;
		}
	}
	for (step=1; step<latency; step++)//because first step will be ignored.
	{
		for (eachPlayers=0; eachPlayers<numberOfPlayer; eachPlayers++)
		{
			playersNetQueue[eachPlayers][step].order=new NullOrder();
			playersNetQueue[eachPlayers][step].packetID=step;
			playersNetQueue[eachPlayers][step].ackID=step;
		}
	}

	for (eachPlayers=0; eachPlayers<numberOfPlayer; eachPlayers++)
	{
		lastReceivedFromHim[eachPlayers]=latency-1;
		lastReceivedFromMe[eachPlayers]=latency-1;
		countDown[eachPlayers]=0;
		stayingPlayersMask[eachPlayers]=0;

		for (int player=0; player<numberOfPlayer; player++)
			lastAviableStep[eachPlayers][player]=-1;

		while(localOrderQueue[eachPlayers].size()>0)
		{
			Order *o=localOrderQueue[eachPlayers].front();
			delete o;
			localOrderQueue[eachPlayers].pop_front();
		}
	}
	
	dropState=NO_DROP_PROCESSING;
	printQueue("init::");
};


bool NetGame::isStepReady(Sint32 step)
{
	int eachPlayers;
	for (eachPlayers=0;eachPlayers<numberOfPlayer;eachPlayers++)
	{
		if (players[eachPlayers]->type!=Player::P_LOST_B)
		{
			if (smaller(lastReceivedFromHim[eachPlayers], step))
			{
				fprintf(logFile, "player %d is not ready for step %d (nrfh). \n", eachPlayers, step);
				isWaitingForPlayer=true;
				return false;
			}
			if (players[eachPlayers]->type==Player::P_IP)
				if (smaller(lastReceivedFromMe[eachPlayers], step))
				{
					fprintf(logFile, "player %d is not ready for step %d (nrfm). \n", eachPlayers, step);
					isWaitingForPlayer=true;
					return false;
				}
			
			if ( playersNetQueue[eachPlayers][step].order==NULL)
			{
				fprintf(logFile, "player %d is not ready for step %d (noor). \n", eachPlayers, step);
				isWaitingForPlayer=true;
				return false;
			}
		}
	}

	isWaitingForPlayer=false;
	return true;
};

int NetGame::numberStepsReady(Sint32 step)
{
	int s=step;
	int i=0;
	while(i<latency)
	{
		if (isStepReady(s))
		{
			i++;
			s=(s+1)%queueSize;
		}
		else
		{
			return i;
		}
	}
	return i;
}

int NetGame::advance()
{
	// this allow to be a bit more "TCP/IP Friendly" by waiting a bit more if
	// a packet is lost. The engine can call this function.
	int nsr=numberStepsReady(currentStep);
	int hl=latency>>1;
	if (dropState!=NO_DROP_PROCESSING)
		return 700;
	else if (nsr<hl)
	{
		return ((hl-nsr)*(hl-nsr)*4); // 20 is a pragmatically choosen number.
	}
	else
		return 0;
}

bool NetGame::smaller(int a, int b)
{
	int d=abs(a-b);
	if (d<(queueSize>>1))
	{
		// Non-warp case.
		return (a<b);
	}
	else
	{
		// Warp case.
		return (a>b);
	}
}
bool NetGame::smalleroe(int a, int b)
{
	int d=abs(a-b);
	if (d<(queueSize>>1))
	{
		// Non-warp case.
		return (a<=b);
	}
	else
	{
		// Warp case.
		return (a>=b);
	}
}

bool NetGame::nextUnrecievedStep(int currentStep, int *player, int *step)
{
	unsigned st=currentStep;
	int wfp=-1;
	int wfs=-1;
	for (int si=0; si<latency; si++)
	{
		for (int p=0; p<numberOfPlayer; p++)
			if (lastReceivedFromHim[p]==st)
			{
				wfp=p;
				wfs=st;
				si=latency;
				break;
			}
		st=(st+1)%queueSize;
	}
	if (wfp==-1)
		return false;
	
	if ((players[wfp]->type!=Player::P_IP)&&(players[wfp]->type!=Player::P_LOST_A))
		return false;
	
	//fprintf(logFile, "wfp=%d, st=%d.\n", wfp, st);
	
	*player=wfp;
	*step=st;
	
	return true;
};

bool NetGame::nextUnrecievedStep(int currentStep, int player, int *step)
{
	unsigned st=currentStep;
	int wfs=-1;
	for (int si=0; si<latency; si++)
	{
		if (lastReceivedFromHim[player]==st)
		{
			wfs=st;
			si=latency;
			break;
		}
		st=(st+1)%queueSize;
	}
	
	if ((players[player]->type!=Player::P_IP)&&(players[player]->type!=Player::P_LOST_A))
		return false;
	
	*step=st;
	
	return true;
};

Uint32 NetGame::whoMaskAreWeWaitingFor(Sint32 step)
{
	Uint32 waitingPlayersMask=0;

	for (int eachPlayers=0; eachPlayers<numberOfPlayer; eachPlayers++)
	{
		/*if (smaller(lastReceivedFromHim[eachPlayers], step))
			waitingPlayersMask |= 1<<eachPlayers;
		if (players[eachPlayers]->type==Player::P_IP)
			if (smaller(lastReceivedFromMe[eachPlayers], step))
			waitingPlayersMask |= 1<<eachPlayers;
		if ( playersNetQueue[eachPlayers][step].order==NULL)
			waitingPlayersMask |= 1<<eachPlayers;*/
		if ((countDown[eachPlayers]>COUNT_DOWN_MIN)&&(players[eachPlayers]->type==Player::P_IP)&&(eachPlayers!=localPlayerNumber))
			waitingPlayersMask |= 1<<eachPlayers;
	}
	
	//fprintf(logFile, "step=%d, waitingPlayersMask=%x(%d)\n", step, waitingPlayersMask, waitingPlayersMask);
	
	return waitingPlayersMask;
};

void NetGame::confirmNewStepRecievedFromHim(Sint32 receivedStep, Sint32 receivedFromPlayerNumber)
{
	// step-1 is reserved for non overlapping detection, can only have window size of queueSize-1
	if (receivedStep<0)
		return;
	
	//assert(receivedStep!=(currentStep-1+queueSize)%queueSize);
	
	countDown[receivedFromPlayerNumber]=0;
	
	int testStep;
	int firstStep=lastReceivedFromHim[receivedFromPlayerNumber];
	if(firstStep<receivedStep)//then there is no warping:
	{
		for(testStep=firstStep; testStep<=receivedStep; testStep++)
		{
			if (playersNetQueue[receivedFromPlayerNumber][testStep].order!=NULL)
			{
				lastReceivedFromHim[receivedFromPlayerNumber]=testStep;
			}
			else
				break; // We confirm only if ALL orders have been received, from step to lastReceivedFromHim.
		}
	}
	else//then there is warping after queueSize:
	{
		bool passed=true;

		for(testStep=firstStep; testStep<queueSize; testStep++)
		{
			if (playersNetQueue[receivedFromPlayerNumber][testStep].order!=NULL)
				lastReceivedFromHim[receivedFromPlayerNumber]=testStep;
			else
			{
				passed=false;
				break;
			}
		}

		if (passed)
			for(testStep=0; testStep<=receivedStep; testStep++)
			{
				if (playersNetQueue[receivedFromPlayerNumber][testStep].order!=NULL)
					lastReceivedFromHim[receivedFromPlayerNumber]=testStep;
				else
					break;
			}

	}
}



void NetGame::sendMyOrderThroughUDP(Order *order, Sint32 orderStep, Sint32 targetPlayer, Sint32 confirmedStep)
{
	if (targetPlayer==localPlayerNumber)
		return;
	
	char *data=(char *)malloc(order->getDataLength()+16);
	
	addSint32(data, orderStep, 0);
	addSint32(data, lastReceivedFromHim[targetPlayer], 4);
	addSint32(data, localPlayerNumber, 8);
	data[12]=order->getOrderType();
	data[13]=0;
	data[14]=0;
	data[15]=7;
	
	//fprintf(logFile, "sending(%d; %d; %d; %d)\n", orderStep, lastReceivedFromHim[targetPlayer], localPlayerNumber, order->getOrderType());
	
	memcpy(data+16, order->getData(), order->getDataLength());
	
	players[targetPlayer]->send(data, order->getDataLength()+16);
	
	free(data);
}

void NetGame::printQueue(char *str)
{
	/*for (int i=-latency; i<latency; i++)
		for (int eachPlayer=0; eachPlayer<numberOfPlayer; eachPlayer++)
		{
			int s=(currentStep+queueSize+i)%queueSize;
			if (playersNetQueue[eachPlayer][s].order)
				fprintf(logFile, "%s [%d] inQueue=%d, type=%d, order=%x.\n", str, s, playersNetQueue[eachPlayer][s].order->inQueue, playersNetQueue[eachPlayer][s].order->getOrderType(), (int)playersNetQueue[eachPlayer][s].order);
			else
				fprintf(logFile, "%s [%d] NULL.\n", str, s);
		}*/
}

void NetGame::orderHasBeenExecuted(Order *order)
{
	// "order" is used as a communication channel from "NetGame" to "Game" and "GameGUI".
	// The ordred in queue "playersNetQueue" are deleted when new orders comes,
	// or in the netGame destructor. The other orders which are not in the queue
	// need to be deleted after use (by "Game" or "GameGUI").
	assert(order);
	if (!order->inQueue)
	{
		fprintf(logFile, "deleting order type %d.\n", order->getOrderType());
		delete order;
	};
}

Order *NetGame::getOrder(Sint32 playerNumber)
{
	assert((playerNumber>=0) && (playerNumber<numberOfPlayer));
	printQueue("getOrder::");

	//fprintf(logFile, "acs=(%x, %x).\n", checkSumsLocal[currentStep], checkSumsRemote[currentStep]);
	if (checkSumsLocal[currentStep]&&checkSumsRemote[currentStep])
	{
		if (checkSumsLocal[currentStep]==checkSumsRemote[currentStep])
		{
			//printf("(%d)World is synchronized.   (rsc=%x, lcs=%x)\n", currentStep, checkSumsRemote[currentStep], checkSumsLocal[currentStep]);
		}
		else
		{
			printf("(%d)World is desynchronized! (rsc=%x, lcs=%x)\n", currentStep, checkSumsRemote[currentStep], checkSumsLocal[currentStep]);
			assert(false/*world desynchronization*/);
		}
	}
	checkSumsLocal[currentStep]=0;
	checkSumsRemote[currentStep]=0;
	checkSumsRemote[(currentStep+queueSize-1-latency)%queueSize]=0;// in case a checkSumPacket arrived very late!

	// do we have orders from every player now?
	if ((players[playerNumber]->quitting)&&(players[playerNumber]->type==Player::P_LOST_B))
	{
		fprintf(logFile, "new QuitedOrder() playerNumber=%d, currentStep=%d\n", playerNumber, currentStep);
		Order *order=new QuitedOrder();
		order->sender=playerNumber;
		order->inQueue=false;
		return order;
	}
	else if (isWaitingForPlayer)
	{
		Order *order=new WaitingForPlayerOrder(whoMaskAreWeWaitingFor((currentStep+1)%queueSize));
		order->sender=playerNumber;
		order->inQueue=false;
		fprintf(logFile, "new WaitingForPlayerOrder() playerNumber=%d, currentStep=%d\n", playerNumber, currentStep);
		return order;
	}
	else if (players[playerNumber]->type==Player::P_LOST_B)
	{
		fprintf(logFile, "new NullOrder() playerNumber=%d, currentStep=%d\n", playerNumber, currentStep);
		Order *order=new NullOrder();
		order->sender=playerNumber;
		order->inQueue=false;
		return order;
	}
	else
	{
		// we get the order:
		//fprintf(logFile, "playerNumber=%d, packetID=%d, ackID=%d, order=%d.\n", playerNumber, playersNetQueue[playerNumber][currentStep].packetID, playersNetQueue[playerNumber][currentStep].ackID, playersNetQueue[playerNumber][currentStep].order);
		Order *order=playersNetQueue[playerNumber][currentStep].order;
		// NetGame will free the object and step will delete its reference from list,
		// when new orders will come.
		assert(order);

		switch(order->getOrderType())
		{
			case ORDER_PLAYER_QUIT_GAME :
			{
				PlayerQuitsGameOrder *pqgo=(PlayerQuitsGameOrder *)order;
				int ap=pqgo->player;
				//players[ap]->type=Player::P_LOST_B; only when all order are cross recieved.
				fprintf(logFile, "players %d quitting\n", ap);
				players[ap]->quitting=true;
				players[ap]->quitStep=currentStep;
				int s=lastReceivedFromHim[ap];
				for (int si=0; si<=(2*latency); si++)
				{
					checkSumsLocal[s]=0;
					checkSumsRemote[s]=0;
					s=(s+1)%queueSize;
				}
				
			}
			break;
			default :
			{

			}
		}
		
		order->sender=playerNumber;
		if ((order->getOrderType()!=51)&&(order->getOrderType()!=73))
			fprintf(logFile, "(%d)getOrder, player(%d), type(%d).\n",currentStep, playerNumber,order->getOrderType());
		//if (order->getOrderType()!=73)
		//	fprintf(logFile, "(%d)go, p=%d, t=%d, l=%d\n", currentStep, playerNumber, order->getOrderType(), order->getDataLength());
		order->inQueue=true;
		return order;
	}
}

void NetGame::pushOrder(Order *order, Sint32 playerNumber)
{
	assert(order);
	assert((playerNumber>=0) && (playerNumber<numberOfPlayer));
	assert(players[playerNumber]->type!=Player::P_IP);//method only for local & ai.
	printQueue("pushOrder::");
	
	
	// will be used [latency] steps later
	int pushStep=(currentStep+latency)%queueSize;
	if ((order->getOrderType()!=73)&&(order->getOrderType()!=51))
		fprintf(logFile, "NetGame::pushOrder playerNumber=(%d), pushStep=(%d), getOrderType=(%d).\n", playerNumber, pushStep, order->getOrderType());
	if (playersNetQueue[playerNumber][pushStep].order)
	{
		if (((order->getOrderType()==ORDER_NULL)||(order->getOrderType()==ORDER_SUBMIT_CHECK_SUM)))
		{
			delete order;
			//fprintf(logFile, "deleting order\n");
		}
		else
		{
			localOrderQueue[playerNumber].push_back(order);
			//fprintf(logFile, "storing order\n");
		}
		
		return;
	}
	else if ((localOrderQueue[playerNumber].size()>0)&&((order->getOrderType()==ORDER_NULL)||(order->getOrderType()==ORDER_SUBMIT_CHECK_SUM)))
	{
		//fprintf(logFile, "deleting useless order\n");
		delete order;
		order=localOrderQueue[playerNumber].front();
		localOrderQueue[playerNumber].pop_front();
	}
	else if ((localOrderQueue[playerNumber].size()>0))
	{
		// We need "localOrderQueue" to be FIFO.
		localOrderQueue[playerNumber].push_back(order);
		order=localOrderQueue[playerNumber].front();
		localOrderQueue[playerNumber].pop_front();
	}
	
	int eachPlayers;
	if (localPlayerNumber==playerNumber)
	{
		for (eachPlayers=0;eachPlayers<numberOfPlayer;eachPlayers++)
		{
			if (players[eachPlayers]->type==Player::P_IP)
			{
				// then send directly
				sendMyOrderThroughUDP(order, pushStep, eachPlayers, lastReceivedFromHim[eachPlayers]);
			}
			else
			{
				// We confirm reception for every player that is local. (ai & local)
				lastReceivedFromMe[eachPlayers]=pushStep; // 2B Clean only
			}
		}
	}
	
	
	playersNetQueue[playerNumber][pushStep].order=order;
	playersNetQueue[playerNumber][pushStep].packetID=pushStep;
	playersNetQueue[playerNumber][pushStep].ackID=lastReceivedFromHim[playerNumber];
	
	// push in private queue
	confirmNewStepRecievedFromHim(pushStep, playerNumber);
	if (localPlayerNumber==playerNumber)
	{
		if (order->getOrderType()==ORDER_SUBMIT_CHECK_SUM)
		{
			checkSumsLocal[pushStep]=((SubmitCheckSumOrder*)order)->checkSumValue;
			//fprintf(logFile, "(%d)submit local cs=%x\n", pushStep, checkSumsLocal[pushStep]);
		}
		else
			checkSumsLocal[pushStep]=0;
	}
	
}

void NetGame::treatData(char *data, int size, IPaddress ip)
{
	if (size<16)
		return;	
	if ((data[13]!=0)||(data[14]!=0)||(data[15]!=7))
		return;
	
	Order *o=Order::getOrder(data+12, size-12);
	
	if (o==NULL)
		return;
	
	// TODO : check if the ip the the right alowed player to send an order.

	int st=getSint32(data, 0);
	int sr=getSint32(data, 4);
	int pl=getSint32(data, 8);
	//assert(players[pl]->type==Player::P_IP); // TODO : CARE : this is an eays way to kill a client !
	
	if(players[pl]->type==Player::P_LOST_A)
		fprintf(logFile, "Packet recieved (%d, %d, %d, %d).\n", st, sr, pl, data[12]);
	countDown[pl]=0;
	
	if (sr>=0)
		lastReceivedFromMe[pl]=sr;
	
	int type=o->getOrderType();
	
	if ((type==ORDER_SUBMIT_CHECK_SUM)&&(players[pl]->type==Player::P_IP)/*&&(players[pl]->type!=Player::P_LOST_B)*/)
	{
		SubmitCheckSumOrder *cso=(SubmitCheckSumOrder *)o;
		
		checkSumsRemote[st]=cso->checkSumValue;
		
		//fprintf(logFile, "(%d)submit remote cs=%x\n", st, checkSumsRemote[st]);
	}
	
	if (type==ORDER_WAITING_FOR_PLAYER)
	{
		// TODO : update a list to learn who is waiting for who.
		
		/*WaitingForPlayerOrder *fwpo=(WaitingForPlayerOrder *)o;
		Uint32 mawp=fwpo->maskAwayPlayer;
		fprintf(logFile, "(%d)player %d, is waiting for mawp=%x\n", st, pl, mawp);*/
		delete o;
	}
	else if (type==ORDER_DROPPING_PLAYER)
	{
		DroppingPlayerOrder *dpo=(DroppingPlayerOrder *)o;
		int ds=dpo->dropState;
		Uint32 spm=dpo->stayingPlayersMask;
		
		if (ds==STARTING_DROPPING_PROCESS)
		{
			if (dropState<ONE_STAY_MASK_RECIEVED)
			{
				if (stayingPlayersMask[localPlayerNumber]!=spm)
				{
					fprintf(logFile, "we start a new droping process, ds=%d, spm=%x(%d)\n", ds, spm, spm);
					dropState=ONE_STAY_MASK_RECIEVED;
				
					for (int eachPlayer=0; eachPlayer<numberOfPlayer; eachPlayer++)
						stayingPlayersMask[eachPlayer]=0;
				
					stayingPlayersMask[localPlayerNumber]=spm;
					stayingPlayersMask[pl]=spm;
				}
				else
				{
					// we simply resend the the sending player what we this about StayPlayerMask
					
					Uint32 spm=stayingPlayersMask[localPlayerNumber];
					Order *order=new DroppingPlayerOrder(spm, CROSS_SENDING_STAY_MASK);
					sendMyOrderThroughUDP(order, -1, pl, lastReceivedFromHim[pl]);
					delete order;
					
					fprintf(logFile, "resending spm=%x to player %d,\n", spm, pl);
				}
			}
			else
			{
				fprintf(logFile, "Only one dropping process can be running at one time\n");
			}
			
			if (dropState<ALL_STAY_MASK_RECIEVED)
			{
				// we cross-send the first spm
				Uint32 spm=stayingPlayersMask[localPlayerNumber];
				Order *order=new DroppingPlayerOrder(spm, CROSS_SENDING_STAY_MASK);
				Uint32 pm=1;
				for (int p=0; p<numberOfPlayer; p++)
				{
					if (pm & spm)
						sendMyOrderThroughUDP(order, -1, p, lastReceivedFromHim[p]);
					pm=pm<<1;
				}
				
				delete order;
			}
			
		}
		else if (ds==CROSS_SENDING_STAY_MASK)
		{
			fprintf(logFile, "player %d send us an cross sending stay mask, ds=%d, spm=%x(%d)\n", pl, ds, spm, spm);
			
			stayingPlayersMask[pl]=spm;
			Uint32 smallerSpm=stayingPlayersMask[localPlayerNumber]&spm;
			stayingPlayersMask[localPlayerNumber]=smallerSpm;
			
			// we look if we recieved the last StayPlayerMask:
			bool allRecived=true;
			bool allSames=true;
			Uint32 pm=1;
			for (int p=0; p<numberOfPlayer; p++)
			{
				if (pm&smallerSpm)
				{
					fprintf(logFile, "%d;", p);
					if (stayingPlayersMask[p]==0)
						allRecived=false;
					if (stayingPlayersMask[p]!=smallerSpm)
						allSames=false;
				}
				pm=pm<<1;
			}
			fprintf(logFile, "\n");
			
			fprintf(logFile, "allRecived=%d, allSames=%d, smp=%x(%d), smallerSpm=%x(%d)\n", allRecived, allSames, spm, spm, smallerSpm, smallerSpm);
			if (allRecived)
			{
				static Uint32 lastSmallerMspm=0;
				static int lastTime=0;
				if (allSames)
				{
					fprintf(logFile, "we reached the ALL_STAY_MASK_RECIEVED drop State.\n");
					
					dropState=ALL_STAY_MASK_RECIEVED;
					
					// lest's remove players to drop:
					
					Uint32 pm=1;
					{
						for (int p=0; p<numberOfPlayer; ++p)
						{
							if (pm&spm)
							{
								fprintf(logFile, "Player %d stays\n", p);
							}
							else if (players[p]->type==Player::P_IP)
							{
						       fprintf(logFile, "Player %d lost\n", p); 
							   players[p]->type=Player::P_LOST_A;
							}
							
							pm=pm<<1;
						}
					}
					
					// we have to inform other players about the last step we have from him.

					{
						for (int p=0; p<numberOfPlayer; ++p)
							if (p!=localPlayerNumber)
								stayingPlayersMask[p]=0;
					}

					dropState=NO_DROP_PROCESSING;
					lastSmallerMspm=0;
				}
				else if ((lastSmallerMspm!=smallerSpm)||(time!=lastTime))
				{
					lastSmallerMspm=smallerSpm;
					lastTime=time;
					// we have to resend the most wanted StayPlayerMask:
					
					// calculation of the MostWantedStayPlayerMask:
					Uint32 mwspm=0xFFFF;
					Uint32 spm=stayingPlayersMask[localPlayerNumber];
					Uint32 pm=1;
					{
						for (int p=0; p<numberOfPlayer; ++p)
						{
							if (pm&spm)
								mwspm&=stayingPlayersMask[p];
							pm=pm<<1;
						}
					}
					
					fprintf(logFile, "World is nearing a big global disconneciton...(like in reality) %x\n", mwspm);
					
					// we copy it as the main StayPlayerMask:
					stayingPlayersMask[localPlayerNumber]=mwspm;
					
					// we send it to all players:
					Order *order=new DroppingPlayerOrder(mwspm, CROSS_SENDING_STAY_MASK);
					pm=1;
					{
						for (int p=0; p<numberOfPlayer; ++p)
						{
							if (pm&mwspm)
								sendMyOrderThroughUDP(order, -1, p, lastReceivedFromHim[p]);
							pm=pm<<1;
						}
					}
					delete order;
				
				}
			}
		}
		else
		{
			assert(false);
		}
	}
	else if (type==ORDER_REQUESTING_AWAY)
	{
		RequestingDeadAwayOrder *rdao=(RequestingDeadAwayOrder *)o;
		
		int ap=rdao->player;
		int ms=rdao->missingStep;
		int las=rdao->lastAviableStep;
		
		if ((players[ap]->type==Player::P_LOST_A)||(players[ap]->type==Player::P_LOST_B)||(players[ap]->type==Player::P_IP))
		{
			lastAviableStep[ap][pl]=las;
			fprintf(logFile, "player %d wants order %d of lost_a player %d\n", pl, ms, ap);
			
			// we do fake a send from th away-player (ap).
			Order *order=playersNetQueue[ap][ms].order;
			if (order)
			{
				fprintf(logFile, "sending an order by substitution of the rightfull player\n");
				char *data=(char *)malloc(order->getDataLength()+16);
				
				addSint32(data, ms, 0);
				addSint32(data, -1, 4);
				addSint32(data, ap, 8);
				data[12]=order->getOrderType();
				data[13]=0;
				data[14]=0;
				data[15]=7;
				
				memcpy(data+16, order->getData(), order->getDataLength());
				
				players[pl]->send(data, order->getDataLength()+16);
				
				free(data);
			}
			else if (players[ap]->type==Player::P_LOST_A)
			{
				fprintf(logFile, "no order avaible for resending a\n");
				Order *order=new NoMoreOrdersAviable(ap, lastReceivedFromHim[ap]);
				int rst=(lastReceivedFromMe[pl]+1)%queueSize;
				sendMyOrderThroughUDP(order, rst, pl, lastReceivedFromHim[pl]);
			}
			else if (players[ap]->type==Player::P_LOST_B)
			{
				fprintf(logFile, "no order avaible for resending b\n");
				Order *order=new RequestingDeadAwayOrder(ap, ms, lastReceivedFromHim[ap]);
				int rst=(lastReceivedFromMe[pl]+1)%queueSize;
				sendMyOrderThroughUDP(order, rst, pl, lastReceivedFromHim[pl]);
			}
			
			// We look if any player may have another step for a LOST_A player:
			
			bool allSame=true;
			for (int p=0; p<numberOfPlayer; p++)
				if ((players[p]->type==Player::P_IP)&&(lastAviableStep[ap][p]!=(int)lastReceivedFromHim[ap]))
						allSame=false;
			
			if (allSame)
			{
				fprintf(logFile, "the last step %d of player %d is spent, now this is a realy LOST_B player.\n", lastReceivedFromHim[ap], ap);
				players[ap]->type=Player::P_LOST_B;

				int s=lastReceivedFromHim[ap];
				for (int si=0; si<=(2*latency); si++)
				{
					checkSumsLocal[s]=0;
					checkSumsRemote[s]=0;
					s=(s+1)%queueSize;
				}
			}
		}
	}
	else
	{
		if (playersNetQueue[pl][st].order)
		{
			delete playersNetQueue[pl][st].order;
			playersNetQueue[pl][st].order=NULL;
		}
		playersNetQueue[pl][st].order=o;
		playersNetQueue[pl][st].packetID=st;
		playersNetQueue[pl][st].ackID=lastReceivedFromHim[pl];
		
		
		//confirmNewStepRecievedFromHim(sr, pl);
		confirmNewStepRecievedFromHim(st, pl);
	}

}

void NetGame::step(void)
{
	time++;
	printQueue("step::");
	
	if (socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_GAME_PACKET_SIZE);
		assert(packet);
		
		while (SDLNet_UDP_Recv(socket, packet)==1)
		{
			//fprintf(logFile, "Packet recieved.\n");
			//fprintf(logFile, "packet=%d\n", (int)packet);
			//fprintf(logFile, "packet->channel=%d\n", packet->channel);
			//fprintf(logFile, "packet->len=%d\n", packet->len);
			//fprintf(logFile, "packet->maxlen=%d\n", packet->maxlen);
			//fprintf(logFile, "packet->status=%d\n", packet->status);
			//fprintf(logFile, "packet->address=%x,%d\n", packet->address.host, packet->address.port);
			
			//fprintf(logFile, "packet->data(%d)=%s\n", packet->data[0], packet->data);
			
			treatData((char *)(packet->data), packet->len, packet->address);
		}

		SDLNet_FreePacket(packet);
	}
	
	// TODO: receive UDP packets and push them.
	int eachPlayer;
	int nextStep;

	nextStep=(currentStep+1)%queueSize;
	fprintf(logFile, "nextStep=%d, dropState=%d\n", nextStep, dropState);
	
	for (eachPlayer=0; eachPlayer<numberOfPlayer; eachPlayer++)
	{
		if ((players[eachPlayer]->quitting)&&(players[eachPlayer]->type!=Player::P_LOST_B))
		{ 
			fprintf(logFile, "player=%d quitStep=%d, lastReceivedFromMe=%d .\n", eachPlayer, players[eachPlayer]->quitStep, lastReceivedFromMe[eachPlayer]);
			if (!smaller(lastReceivedFromMe[eachPlayer], players[eachPlayer]->quitStep))
			{
				players[eachPlayer]->type=Player::P_LOST_B;
				fprintf(logFile, "player=%d is now P_LOST_B because %d !< %d\n", eachPlayer, lastReceivedFromMe[eachPlayer], players[eachPlayer]->quitStep);
			}
		}
	}
	
	if (isStepReady(nextStep))
	{
		for (eachPlayer=0; eachPlayer<numberOfPlayer; eachPlayer++)
		{
			int s=(currentStep+queueSize-latency-1)%queueSize; // the easy solution
			//int s=(currentStep+latency+1)%queueSize; // the memory saver solution
			if (playersNetQueue[eachPlayer][s].order)
			{
				assert(playersNetQueue[eachPlayer][s].order->inQueue);
				delete playersNetQueue[eachPlayer][s].order;
				playersNetQueue[eachPlayer][s].order=NULL;
			}
		}
		currentStep=nextStep;
	}
	else
	{
		// We have to send our orders again if anyone hasen't received it:
		if(dropState==NO_DROP_PROCESSING)
		{
			for (eachPlayer=0; eachPlayer<numberOfPlayer; eachPlayer++)
			{
				if (players[eachPlayer]->type==BasePlayer::P_IP)
				{
					int rst=(lastReceivedFromMe[eachPlayer]+1)%queueSize;
					int stepToTest=(nextStep/*+lostPacketLatencyMargin*/)%queueSize;
					assert(lostPacketLatencyMargin<(int)queueSize);
					
					//fprintf(logFile, "(%d): %d<?<%d\n", eachPlayer, lastReceivedFromMe[eachPlayer], stepToTest);
					
					if (smaller(lastReceivedFromMe[eachPlayer], stepToTest))
					{
						//fprintf(logFile, "resend to player %d, order %d, type %d.\n", eachPlayer, rst, playersNetQueue[localPlayerNumber][rst].order->getOrderType());
						// then re-send directly
						
						sendMyOrderThroughUDP(playersNetQueue[localPlayerNumber][rst].order, rst, eachPlayer, lastReceivedFromHim[eachPlayer]);
					}
					else
					{
						
						Uint32 wfpm=whoMaskAreWeWaitingFor(currentStep);
						Order *order=new WaitingForPlayerOrder(wfpm);
						sendMyOrderThroughUDP(order, rst, eachPlayer, lastReceivedFromHim[eachPlayer]);
						delete order;
					}
				}
			}
		}

		
		Order *order=NULL;
		int ap, s;
		for (ap=0; ap<numberOfPlayer; ap++)
		{
			if (players[ap]->type==BasePlayer::P_LOST_A)
			{
				if (nextUnrecievedStep(currentStep, ap, &s))
				{
					Sint32 missingStep=(s+1)%queueSize;
					order=new RequestingDeadAwayOrder(ap, missingStep, lastReceivedFromHim[ap]);
					fprintf(logFile, "requesting order %d from player %d.\n", missingStep, ap);
					break;
				}
			}
		}
		if (order)
		{
			for (int rp=0; rp<numberOfPlayer; rp++)
			{
				if (players[rp]->type==BasePlayer::P_IP)
				{
					fprintf(logFile, "requesting order %d from player %d, request player %d\n", s, ap, rp);
					int rst=(lastReceivedFromMe[rp]+1)%queueSize;
					sendMyOrderThroughUDP(order, rst, rp, lastReceivedFromHim[rp]);
				}
			}
			delete order;
		}
		// we look for any usefull drop:
		
		for (int p=0; p<numberOfPlayer; p++)
		{
			countDown[p]++;
			if (countDown[p]%10==0)
				fprintf(logFile, "countDown[%d]=%d, type=%d\n", p, countDown[p], players[p]->type);
			int s;
			if (nextUnrecievedStep(currentStep, p, &s))
			{
				if (((players[p]->type==BasePlayer::P_IP)||(players[p]->type==BasePlayer::P_LOST_A))&&(countDown[p]>COUNT_DOWN_DEATH))
				{
					Uint32 stayMask=0; // the mask of staying players

					stayMask=1<<localPlayerNumber;

					for (int eachPlayer=0; eachPlayer<numberOfPlayer; ++eachPlayer)
							if (((players[eachPlayer]->type==BasePlayer::P_IP)||(players[eachPlayer]->type==BasePlayer::P_LOST_A))&&(eachPlayer!=p))
								stayMask|=1<<eachPlayer;


					if (stayingPlayersMask[localPlayerNumber])
					{
						stayMask&=stayingPlayersMask[localPlayerNumber];
						stayingPlayersMask[localPlayerNumber]=stayMask;
					}


					int nbipp=0; // Number Of IP players
					Uint32 pm=1;

					for (int eachPlayer=0; eachPlayer<numberOfPlayer; ++eachPlayer)
					{
						fprintf(logFile, "players[%d]->type=%d, pm=%x(%d), stayMask=%x(%d)\n", eachPlayer, players[eachPlayer]->type, pm, pm, stayMask, stayMask);
						if (((players[eachPlayer]->type==BasePlayer::P_IP)||(players[eachPlayer]->type==BasePlayer::P_LOST_A))&&((pm & stayMask)!=0))
						{
							fprintf(logFile, "m: player %d stay.\n", eachPlayer);
							nbipp++;
						}
						pm=pm<<1;
					}

					fprintf(logFile, "ordering new drop, p=%d, stayMask=%x(%d), nbipp=%d.\n", p, stayMask, stayMask, nbipp);

					if (nbipp>0)
					{
						Order *order=new DroppingPlayerOrder(stayMask, STARTING_DROPPING_PROCESS);

						for (int eachPlayer=0; eachPlayer<numberOfPlayer; eachPlayer++)
							if ((players[eachPlayer]->type==BasePlayer::P_IP)&&(eachPlayer!=p))
								sendMyOrderThroughUDP(order, -1, eachPlayer, lastReceivedFromHim[eachPlayer]);

						delete order;

						if (dropState==NO_DROP_PROCESSING)
						{
							{
								for (int eachPlayer=0; eachPlayer<numberOfPlayer; eachPlayer++)
									stayingPlayersMask[eachPlayer]=0;
							}

							dropState=ONE_STAY_MASK_RECIEVED;

							{
								for (int pl=0; pl<numberOfPlayer; pl++)
									for (int q=0; q<numberOfPlayer; q++)
										lastAviableStep[pl][q]=-1;
							}

							stayingPlayersMask[localPlayerNumber]=stayMask;
						}
						else
						{

							Uint32 nspm;// New staying player mask
							nspm=stayingPlayersMask[localPlayerNumber] & stayMask;
							stayingPlayersMask[localPlayerNumber]=nspm;
							Uint32 pm=1;
							int nbipp=0; //Number of IP players

							for (int pl=0; pl<numberOfPlayer; pl++)
							{
								if (pm&nspm)
									nbipp++;
								pm=pm<<1;
							}


							if (nbipp<2)
							{
								fprintf(logFile, "We finally drop all IP player\n");

								for (int pl=0; pl<numberOfPlayer; pl++)
									if ((players[pl]->type==Player::P_LOST_A)||(players[pl]->type==Player::P_IP))
										players[pl]->type=Player::P_LOST_B;

								for (int si=0; si<queueSize; si++)
								{
									checkSumsLocal[si]=0;
									checkSumsRemote[si]=0;
									}


								dropState=NO_DROP_PROCESSING;
							}

						}
						//fprintf(logFile, "Player %d have to die, because step %d is still not here. (sm=%x)\n", p, s, stayMask);
					}
					else if (nbipp==0)
					{
						fprintf(logFile, "We drop last IP player %d\n", p);

						players[p]->type=Player::P_LOST_B;

						dropState=NO_DROP_PROCESSING;
						for (int i=0; i<queueSize; i++)
						{
							checkSumsLocal[i]=0;
							checkSumsRemote[i]=0;
						}
					}
				}
			}
		}
		//else if (dropState==ONE_STAY_MASK_RECIEVED)
		{

		}
	}

}

bool NetGame::isNowWaiting()
{
	return isWaitingForPlayer;
}
