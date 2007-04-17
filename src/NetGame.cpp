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

#include <climits>
#include <Stream.h>
#include <TextStream.h>
#include "NetGame.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "NetDefine.h"
#include <assert.h>
#include "Marshaling.h"
#include "LogFileManager.h"
#include "Utilities.h"
#include "Order.h"
#include "Player.h"
#include "Team.h"
#include "Game.h"

NetGame::NetGame(UDPsocket socket, int numberOfPlayer, Player *players[32])
{
	// we have right number of player & valid players
	assert((numberOfPlayer>=0) && (numberOfPlayer<=32));
	assert(players);
	assert(defaultLatency+1<256);
	assert(defaultLatency>=1);

	// we initialize and copy stuff
	this->numberOfPlayer=numberOfPlayer;
	memcpy(this->players, players, numberOfPlayer*sizeof(Player *));

	// we find local player
	localPlayerNumber=-1;
	for (int p=0; p<numberOfPlayer; p++)
	{
		assert(players[p]);
		if (players[p]->type==Player::P_LOCAL)
			localPlayerNumber=p;
	}
	assert(localPlayerNumber!=-1);
	
	waitingForPlayerMask=0;
	ordersByPackets=2;

	this->socket=socket;
	
	logFile=globalContainer->logFileManager->getFile("NetGame.log");
	logFileEnd=globalContainer->logFileManager->getFile("NetGameEnd.log");
	
	assert(logFile);
	
	for (int pi=0; pi<32; pi++)
		for (int stepi=0; stepi<256; stepi++)
			ordersQueue[pi][stepi]=NULL;
	
	initStats();
	init();
};

NetGame::~NetGame()
{
	dumpStats();
	
	fflush(logFile);
	
	for (int pi=0; pi<32; pi++)
		for (int stepi=0; stepi<256; stepi++)
			if (ordersQueue[pi][stepi])
			{
				delete ordersQueue[pi][stepi];
				ordersQueue[pi][stepi]=NULL;
			}
	if (socket)
	{
		// We have to prevents players to close the same socket:
		for (int pi=0; pi<numberOfPlayer; pi++)
			if (players[pi]->socket==socket)
				players[pi]->channel=-1;
		SDLNet_UDP_Close(socket);
		socket=NULL;
	}
}

void NetGame::init(void)
{
	dumpStats();
	initStats();
	
	executeUStep=0;
	pushUStep=0+defaultLatency;
	myLocalWishedLatency=defaultLatency;
	for (int pi=0; pi<numberOfPlayer; pi++)
		wishedLatency[pi]=defaultLatency;
	myLocalWishedDelay=0;
	for (int pi=0; pi<numberOfPlayer; pi++)
		for (int si=0; si<256; si++)
			recentsWishedDelay[pi][si]=0;
	
	waitingForPlayerMask=0;
	
	
	for (int pi=0; pi<numberOfPlayer; pi++)
	{
		// We start with no checkSum available:
		for (int stepi=0; stepi<256; stepi++)
			gameCheckSums[pi][stepi]=0;
		
		// We free any order:
		for (int stepi=0; stepi<256; stepi++)
		{
			if (ordersQueue[pi][stepi])
				delete ordersQueue[pi][stepi];
			ordersQueue[pi][stepi]=NULL;
		}
		
		// We have to create the first unsent orders:
		for (Uint32 si=0; si<pushUStep; si++)
		{
			Order *order=new NullOrder();
			order->sender=pi;
			order->ustep=si;
			ordersQueue[pi][si]=order;
		}
		// We have to create new orders to be freed:
		for (Uint32 si=pushUStep; si<256; si++)
		{
			Order *order=new NullOrder();
			order->sender=pi;
			order->ustep=0;
			ordersQueue[pi][si]=order;
		}
		
		// We write that the first unsent orders are received:
		lastUStepReceivedFromMe[pi]=pushUStep-1;
		lastSdlTickReceivedFromHim[pi]=0;
		
		// At the beginning, no-one is late:
		countDown[pi]=0;
		// And everyone thinks that everyone is connected:
		droppingPlayersMask[pi]=0;
		
		dropStatusCommuniquedToGui[pi]=false;
		
		// And all player's ping are equals.
		for (int ri=0; ri<1024; ri++)
			recentsPingPong[pi][ri]=40*defaultLatency;
	}
	
	dropState=DS_NoDropProcessing;
};

void NetGame::initStats(void)
{
	for (int di=0; di<40; di++)
	{
		delayInsideStats[di]=0;
		wishedDelayStats[di]=0;
		maxMedianWishedDelayStats[di]=0;
		goodLatencyStats[di]=0;
	}
	for (int pi=0; pi<32; pi++)
		duplicatedPacketStats[pi]=0;
	for (int pi=0; pi<32; pi++)
		for (int ti=0; ti<1024; ti++)
			pingPongStats[pi][ti]=0;
}

void NetGame::dumpStats(void)
{
	int sum=0;
	for (int di=0; di<40; di++)
		sum+=delayInsideStats[di];
	if (sum)
	{
		fprintf(logFileEnd, "delayInsideStats: (sum=%d)\n", sum);
		for (int di=0; di<40; di++)
		{
			int value=delayInsideStats[di];
			int highIntPercent=(100*value)/sum;
			int lowIntPercent=((1000*value)/sum)%10;
			fprintf(logFileEnd, " [%4d]=%7d (%2d.%1d%%) ", di, value, highIntPercent, lowIntPercent);
			float fPercent=(float)(100.*value)/(float)sum;
			for (int i=0; i<(int)(fPercent+.5); i++)
				fprintf(logFileEnd, "*");
			fprintf(logFileEnd, "\n");
		}
		fprintf(logFileEnd, "\n");
	}
	sum=0;
	for (int di=0; di<40; di++)
		sum+=wishedDelayStats[di];
	if (sum)
	{
		fprintf(logFileEnd, "wishedDelayStats: (sum=%d)\n", sum);
		for (int di=0; di<40; di++)
		{
			int value=wishedDelayStats[di];
			int highIntPercent=(100*value)/sum;
			int lowIntPercent=((1000*value)/sum)%10;
			fprintf(logFileEnd, " [%4d]=%7d (%2d.%1d%%) ", di, value, highIntPercent, lowIntPercent);
			float fPercent=(float)(100.*value)/(float)sum;
			for (int i=0; i<(int)(fPercent+.5); i++)
				fprintf(logFileEnd, "*");
			fprintf(logFileEnd, "\n");
		}
		fprintf(logFileEnd, "\n");
	}
	sum=0;
	for (int di=0; di<40; di++)
		sum+=maxMedianWishedDelayStats[di];
	if (sum)
	{
		fprintf(logFileEnd, "maxMedianWishedDelayStats: (sum=%d)\n", sum);
		for (int di=0; di<40; di++)
		{
			int value=maxMedianWishedDelayStats[di];
			int highIntPercent=(100*value)/sum;
			int lowIntPercent=((1000*value)/sum)%10;
			fprintf(logFileEnd, " [%4d]=%7d (%2d.%1d%%) ", di, value, highIntPercent, lowIntPercent);
			float fPercent=(float)(100.*value)/(float)sum;
			for (int i=0; i<(int)(fPercent+.5); i++)
				fprintf(logFileEnd, "*");
			fprintf(logFileEnd, "\n");
		}
		fprintf(logFileEnd, "\n");
	}
	sum=0;
	for (int di=0; di<40; di++)
		sum+=goodLatencyStats[di];
	if (sum)
	{
		fprintf(logFileEnd, "goodLatencyStats: (sum=%d)\n", sum);
		for (int di=0; di<40; di++)
		{
			int value=goodLatencyStats[di];
			int highIntPercent=(100*value)/sum;
			int lowIntPercent=((1000*value)/sum)%10;
			fprintf(logFileEnd, " [%4d]=%7d (%2d.%1d%%) ", di, value, highIntPercent, lowIntPercent);
			float fPercent=(float)(100.*value)/(float)sum;
			for (int i=0; i<(int)(fPercent+.5); i++)
				fprintf(logFileEnd, "*");
			fprintf(logFileEnd, "\n");
		}
		fprintf(logFileEnd, "\n");
	}
	
	sum=0;
	for (int pi=0; pi<32; pi++)
		sum+=duplicatedPacketStats[pi];
	if (sum)
	{
		fprintf(logFileEnd, "duplicatedPacketStats: (sum=%d)\n", sum);
		for (int pi=0; pi<32; pi++)
		{
			int value=duplicatedPacketStats[pi];
			int highIntPercent=(100*value)/sum;
			int lowIntPercent=((1000*value)/sum)%10;
			fprintf(logFileEnd, " [%4d]=%7d (%2d.%1d%%) ", pi, value, highIntPercent, lowIntPercent);
			float fPercent=(float)(100.*value)/(float)sum;
			for (int i=0; i<(int)(fPercent+.5); i++)
				fprintf(logFileEnd, "*");
			fprintf(logFileEnd, "\n");
		}
		fprintf(logFileEnd, "\n");
	}
	for (int pi=0; pi<32; pi++)
	{
		sum=0;
		for (int ti=0; ti<1024; ti++)
			sum+=pingPongStats[pi][ti];
		if (sum)
		{
			fprintf(logFileEnd, "pingPongStatsShort[%d]: (sum=%d)\n", pi, sum);
			
			for (int yi=0; yi<(1024/80); yi++)
			{
				int localSum=0;
				for (int xi=0; xi<80; xi++)
					localSum+=pingPongStats[pi][xi+(yi*80)];
				
				int value=localSum;
				int highIntPercent=(100*value)/sum;
				int lowIntPercent=((1000*value)/sum)%10;
				fprintf(logFileEnd, " [%4d]=%7d (%2d.%1d%%) ", yi, value, highIntPercent, lowIntPercent);
				float fPercent=(float)(100.*value)/(float)sum;
				for (int i=0; i<(int)(fPercent+.5); i++)
					fprintf(logFileEnd, "*");
				fprintf(logFileEnd, "\n");
			}
			{
				int localSum=0;
				for (int xi=(1024-(1024%80)); xi<1024; xi++)
					localSum+=pingPongStats[pi][xi];
				int value=localSum;
				int highIntPercent=(100*value)/sum;
				int lowIntPercent=((1000*value)/sum)%10;
				fprintf(logFileEnd, " [   +]=%7d (%2d.%1d%%) ", value, highIntPercent, lowIntPercent);
				float fPercent=(float)(100.*value)/(float)sum;
				for (int i=0; i<(int)(fPercent+.5); i++)
					fprintf(logFileEnd, "*");
				fprintf(logFileEnd, "\n");
			}
			
			fprintf(logFileEnd, "\n");
		}
	}
	for (int pi=0; pi<32; pi++)
	{
		sum=0;
		for (int ti=0; ti<1024; ti++)
			sum+=pingPongStats[pi][ti];
		if (sum)
		{
			fprintf(logFileEnd, "pingPongStats[%d]: (sum=%d)\n", pi, sum);
			for (int ti=0; ti<1024; ti++)
			{
				int value=pingPongStats[pi][ti];
				int highIntPercent=(100*value)/sum;
				int lowIntPercent=((1000*value)/sum)%10;
				fprintf(logFileEnd, " [%4d]=%7d (%2d.%1d%%) ", ti, value, highIntPercent, lowIntPercent);
				float fPercent=(float)(100.*value)/(float)sum;
				for (int i=0; i<(int)(fPercent+.5); i++)
					fprintf(logFileEnd, "*");
				fprintf(logFileEnd, "\n");
			}
			fprintf(logFileEnd, "\n");
		}
	}
	
	fflush(logFileEnd);
}

Uint32 NetGame::whoMaskAreWeWaitingFor(void)
{
	Uint32 waitingPlayersMask=0;
	for (int pi=0; pi<numberOfPlayer; pi++)
	{
		int type=players[pi]->type;
		if (type==Player::P_IP && !players[pi]->quitting)
		{
			Order *order=ordersQueue[pi][executeUStep&255];
			assert(order);
			if (order->ustep!=executeUStep)
			{
				if (verbose)
					printf("Can't execute ustep=%d (hash=%d) of player=%d.\n", executeUStep, executeUStep&255, pi);
				waitingPlayersMask|=1<<pi;
			}
		}
	}
	return waitingPlayersMask;
};

Uint32 NetGame::whoMaskCountedOut(void)
{
	Uint32 countedOutMask=0;
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP && countDown[pi]>1)
			countedOutMask|=1<<pi;
	return countedOutMask;
};

Uint32 NetGame::lastUsableUStepReceivedFromHim(int player)
{
	Uint32 usi=executeUStep;
	while (ordersQueue[player][usi&255]->ustep==usi)
		usi++;
	return usi-1;
}

void NetGame::sendPushOrder(int targetPlayer)
{
	assert(players[targetPlayer]->type==Player::P_IP);
	assert(targetPlayer!=localPlayerNumber);
	
	fprintf(logFile, "sendPushOrder.\n");
	
	int totalSize=16;
	Uint32 ustep=pushUStep;
	int n;
	for (n=0; n<ordersByPackets; n++)
	{
		Order *order=ordersQueue[localPlayerNumber][ustep&255];
		assert(order);
		assert(order->ustep==ustep);
		if (order->latencyPadding)
		{
			fprintf(logFile, " skipped ustep=%d.\n", ustep);
			assert(order->getOrderType()==ORDER_NULL);
			assert(order->ustep==ustep);
			ustep--; // We skip orders which have been added to increase latency.
			order=ordersQueue[localPlayerNumber][ustep&255];
			assert(order);
			assert(order->ustep==ustep);
		}
		// if target player should not receive it, do not send any audio data. Order is only 5 bytes
		if ((order->getOrderType() == ORDER_VOICE_DATA)
			&& (( ((OrderVoiceData *)order)->recepientsMask & (1<<targetPlayer)) == 0))
			totalSize += 12+((OrderVoiceData *)order)->getStrippedDataLength();
		else
			totalSize += 12+order->getDataLength();
		if (ustep==executeUStep)
		{
			n++;
			break;
		}
		ustep--;
	}
	
	// don't send packet over MTU
	if (totalSize > MAX_GAME_PACKET_SIZE)
	{
		fprintf(stderr, "NetGame::sendPushOrder : attempting to send packet size over MTU size (%d bytes when limit is %d bytes)\n", totalSize, MAX_GAME_PACKET_SIZE);
		for (n=0; n<ordersByPackets; n++)
		{
			Order *order = ordersQueue[localPlayerNumber][(pushUStep-n)&255];
			fprintf(stderr, "Suborder %d type is %d\n", (pushUStep-n)&255, order->getOrderType());
		}
		assert(false);
	}
	Uint8 *data=(Uint8 *)malloc(totalSize);
	data[0]=MULTIPLE_ORDERS;
	data[1]=0; //pad
	data[2]=localPlayerNumber;
	data[3]=0; //pad
	addUint32(data, lastUsableUStepReceivedFromHim(targetPlayer), 4);
	addUint32(data, lastSdlTickReceivedFromHim[targetPlayer], 8);
	addUint32(data, SDL_GetTicks(), 12);
	int l=16;
	fprintf(logFile, " lastUsableUStepReceivedFromHim(%d)=%d\n", targetPlayer, lastUsableUStepReceivedFromHim(targetPlayer));
	ustep=pushUStep;
	for (int i=0; i<n; i++)
	{
		Order *order=ordersQueue[localPlayerNumber][ustep&255];
		assert(order);
		assert(order->ustep==ustep);
		if (order->latencyPadding)
		{
			assert(order->getOrderType()==ORDER_NULL);
			ustep--; // We skip orders which have been added to increase latency.
			order=ordersQueue[localPlayerNumber][ustep&255];
			assert(order);
			assert(order->ustep==ustep);
		}
		int orderSize;
		// if target player should not receive it, do not send any audio data. Order is only 5 bytes
		if ((order->getOrderType() == ORDER_VOICE_DATA)
			&& (( ((OrderVoiceData *)order)->recepientsMask & (1<<targetPlayer)) == 0))
			orderSize = ((OrderVoiceData *)order)->getStrippedDataLength();
		else
			orderSize = order->getDataLength();
		addUint32(data, order->ustep, l);
		l+=4;
		addUint32(data, order->gameCheckSum, l);
		l+=4;
		data[l++]=orderSize;
		data[l++]=order->wishedLatency;
		data[l++]=order->wishedDelay;
		data[l++]=order->getOrderType();
		fprintf(logFile, " sending, ustep=%d, orderSize=%d, wishedLatency=%d, wishedDelay=%d, orderType=%d\n",
				ustep, orderSize, order->wishedLatency, order->wishedDelay, order->getOrderType());
		memcpy(data+l, order->getData(), orderSize);
		l+=orderSize;
		ustep--;
	}
	assert(l==totalSize);
	
	fprintf(logFile, " data ");
	for (int i=0; i<totalSize; i++)
		fprintf(logFile, "[%3d]%3d ", i, data[i]);
	fprintf(logFile, "\n");
	
	fprintf(logFile, " %d orders, totalSize=%d.\n", n, totalSize);
	players[targetPlayer]->send(data, totalSize);
	
	free(data);
}

void NetGame::sendWaitingForPlayerOrder(int targetPlayer)
{
	assert(players[targetPlayer]->type==Player::P_IP || (players[targetPlayer]->type==Player::P_LOST_FINAL && players[targetPlayer]->quitting));
	assert(targetPlayer!=localPlayerNumber);
	fprintf(logFile, "sendWaitingForPlayerOrder (%x)\n", waitingForPlayerMask);
	
	int totalSize=16;
	
	WaitingForPlayerOrder *wfpo=new WaitingForPlayerOrder(waitingForPlayerMask);
	totalSize+=12+wfpo->getDataLength();

	Uint32 resendUStep=lastUStepReceivedFromMe[targetPlayer]+1;
	Order *order=ordersQueue[localPlayerNumber][resendUStep&255];
	assert(order);
	if (order->ustep!=resendUStep)
	{
		order=NULL;
		fprintf(logFile, " resend not available, targetPlayer=%d, resendUStep=%d, v-wfpo\n", targetPlayer, resendUStep);
	}
	else
	{
		totalSize+=12+order->getDataLength();
		fprintf(logFile, " resendAvailable, targetPlayer=%d, resendUStep=%d, v-wfpo\n", targetPlayer, resendUStep);
	}
	
	Uint8 *data=(Uint8 *)malloc(totalSize);
	data[ 0]=MULTIPLE_ORDERS;
	data[ 1]=0; //pad
	data[ 2]=localPlayerNumber;
	data[ 3]=0; //pad
	addUint32(data, lastUsableUStepReceivedFromHim(targetPlayer), 4);
	addUint32(data, lastSdlTickReceivedFromHim[targetPlayer], 8);
	addUint32(data, SDL_GetTicks(), 12);
	fprintf(logFile, " lastUsableUStepReceivedFromHim(%d)=%d\n", targetPlayer, lastUsableUStepReceivedFromHim(targetPlayer));
	
	
	int size=wfpo->getDataLength();
	addUint32(data, executeUStep, 16);
	addUint32(data, 0, 20); // no relevant gameCheckSum available
	data[24]=size;
	data[25]=0; //wishedLatency is networkly synchrone
	data[26]=0; //wishedDelay is networkly synchrone
	data[27]=wfpo->getOrderType();
	memcpy(data+28, wfpo->getData(), size);
	int l=28+size;

	if (order)
	{
		int orderSize=order->getDataLength();
		addUint32(data, order->ustep, l);
		l+=4;
		addUint32(data, order->gameCheckSum, l);
		l+=4;
		data[l++]=orderSize;
		data[l++]=order->wishedLatency;
		data[l++]=order->wishedDelay;
		data[l++]=order->getOrderType();
		memcpy(data+l, order->getData(), orderSize);
		l+=orderSize;
	}
	
	assert(l==totalSize);
	
	fprintf(logFile, " data ");
	for (int i=0; i<totalSize; i++)
		fprintf(logFile, "[%3d]%3d ", i, data[i]);
	fprintf(logFile, "\n");
	
	fprintf(logFile, " totalSize=%d.\n", totalSize);
	players[targetPlayer]->send(data, totalSize);

	free(data);
	delete wfpo;
}

void NetGame::sendDroppingPlayersMask(int targetPlayer, bool askForReply)
{
	assert(players[targetPlayer]->type==Player::P_IP);
	assert(targetPlayer!=localPlayerNumber);
	fprintf(logFile, "sendDroppingPlayersMask\n");

	int totalSize=16;
	int n=0;
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP && (droppingPlayersMask[localPlayerNumber]&(1<<pi)))
			n++;
	int orderDataLength=8+4*n;
	totalSize+=12+orderDataLength;
	fprintf(logFile, " to player %d, myDroppingPlayersMask=%x, executeUStep=%d, n=%d\n",
			targetPlayer, droppingPlayersMask[localPlayerNumber], executeUStep, n);
	
	Uint8 *data=(Uint8 *)malloc(totalSize);
	data[ 0]=MULTIPLE_ORDERS;
	data[ 1]=0; //pad
	data[ 2]=localPlayerNumber;
	data[ 3]=0; //pad
	addUint32(data, lastUsableUStepReceivedFromHim(targetPlayer), 4);
	addUint32(data, lastSdlTickReceivedFromHim[targetPlayer], 8);
	addUint32(data, SDL_GetTicks(), 12);
	
	addUint32(data, executeUStep, 16);
	addUint32(data, 0, 20); // no relevant gameCheckSum available
	data[24]=orderDataLength;
	data[25]=myLocalWishedLatency;
	data[26]=myLocalWishedDelay;
	data[27]=ORDER_DROPPING_PLAYER;
	
	addUint32(data, droppingPlayersMask[localPlayerNumber], 28);
	data[32]=askForReply;
	data[33]=0; //pad
	data[34]=0; //pad
	data[35]=0; //pad
	
	int l=36;
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP && (droppingPlayersMask[localPlayerNumber]&(1<<pi)))
		{
			addUint32(data, lastUsableUStepReceivedFromHim(pi), l);
			fprintf(logFile, " lastUsableUStepReceivedFromHim(%2d)=%d.\n", pi, lastUsableUStepReceivedFromHim(pi));
			l+=4;
		}
	assert(l==totalSize);
	
	fprintf(logFile, " data ");
	for (int i=0; i<totalSize; i++)
		fprintf(logFile, "[%3d]%3d ", i, data[i]);
	fprintf(logFile, "\n");
	
	players[targetPlayer]->send(data, totalSize);

	free(data);
}

void NetGame::sendRequestingDeadAwayOrder(int missingPlayer, int targetPlayer, Uint32 resendingUStep)
{
	assert(players[targetPlayer]->type==Player::P_IP);
	assert(targetPlayer!=localPlayerNumber);

	fprintf(logFile, "sendRequestingDeadAwayOrder");
	
	Uint8 *data=(Uint8 *)malloc(28);
	data[ 0]=MULTIPLE_ORDERS;
	data[ 1]=0; //pad
	data[ 2]=localPlayerNumber;
	data[ 3]=0; //pad
	addUint32(data, lastUsableUStepReceivedFromHim(targetPlayer), 4);
	addUint32(data, lastSdlTickReceivedFromHim[targetPlayer], 8);
	addUint32(data, SDL_GetTicks(), 12);
	
	addUint32(data, resendingUStep, 16);
	addUint32(data, 0, 20); // no relevant gameCheckSum available
	data[20]=1;
	data[21]=myLocalWishedLatency;
	data[22]=myLocalWishedDelay;
	data[23]=ORDER_REQUESTING_AWAY;
	
	data[24]=missingPlayer;
	data[25]=0; //pad
	data[26]=0; //pad
	data[27]=0; //pad
	
	fprintf(logFile, " data ");
	for (int i=0; i<28; i++)
		fprintf(logFile, "[%3d]%3d ", i, data[i]);
	fprintf(logFile, "\n");
	
	players[targetPlayer]->send(data, 28);

	free(data);
}

void NetGame::sendDeadAwayOrder(int missingPlayer, int targetPlayer, Uint32 resendingUStep)
{
	assert(players[targetPlayer]->type==Player::P_IP);
	assert(targetPlayer!=localPlayerNumber);
	
	int totalSize=16;
	Uint32 ustep=resendingUStep;
	int nbp=0;
	for (int n=0; n<ordersByPackets; n++)
	{
		Order *order=ordersQueue[missingPlayer][ustep&255];
		if (order && order->ustep==ustep)
		{
			totalSize+=8+order->getDataLength();
			nbp++;
		}
		ustep--;
	}
	if (nbp==0)
	{
		fprintf(logFile, " Warning, player=%d requested order ustep=%d of dead away player=%d, but we don't have it.\n",
			targetPlayer, resendingUStep, missingPlayer);
		return;
	}
	
	Uint8 *data=(Uint8 *)malloc(totalSize);
	
	data[0]=MULTIPLE_ORDERS;
	data[1]=0; //pad
	data[2]=missingPlayer;
	data[3]=0; //pad
	addUint32(data, 0,  4); //no good lastUsableUStepReceivedFromHim() available
	addUint32(data, 0,  8); //no good lastSdlTickReceivedFromHim[] available
	addUint32(data, 0, 12); //no good SDL_GetTicks() available
	
	fprintf(logFile, "sendDeadAwayOrder missingPlayer=%d, targetPlayer=%d, resendingUStep=%d\n", missingPlayer, targetPlayer, resendingUStep);
	int l=16;
	ustep=resendingUStep;
	for (int n=0; n<ordersByPackets; n++)
	{
		Order *order=ordersQueue[missingPlayer][ustep&255];
		if (order && order->ustep==ustep)
		{
			int orderSize=order->getDataLength();
			addUint32(data, order->ustep, l);
			l+=4;
			addUint32(data, order->gameCheckSum, l);
			l+=4;
			data[l++]=orderSize;
			data[l++]=order->wishedLatency;
			data[l++]=order->wishedDelay;
			data[l++]=order->getOrderType();
			memcpy(data+l, order->getData(), orderSize);
			l+=orderSize;
		}
		ustep--;
	}
	assert(l==totalSize);
	
	fprintf(logFile, " data ");
	for (int i=0; i<totalSize; i++)
		fprintf(logFile, "[%3d]%3d ", i, data[i]);
	fprintf(logFile, "\n");
	
	fprintf(logFile, "%d totalSize=%d.\n", nbp, totalSize);
	players[targetPlayer]->send(data, totalSize);
	
	free(data);
}

void NetGame::pushOrder(Order *order, int playerNumber)
{
	assert(order);
	assert((playerNumber>=0) && (playerNumber<numberOfPlayer));
	if (players[playerNumber]->type<Player::P_AI && players[playerNumber]->type!=Player::P_LOCAL)
	{
		if (verbose)
			printf("Error, can't push order of player %d, as he's type %d, neither P_AI nor P_LOCAL\n", playerNumber, players[playerNumber]->type);
		fprintf(logFile, "Error, can't push order of player %d, as he's type %d, neither P_AI nor P_LOCAL\n", playerNumber, players[playerNumber]->type);
		assert(false);
	}
	
	if (order->getOrderType()!=ORDER_NULL)
	{
		fprintf(logFile, "\n");
		fprintf(logFile, "pushOrder playerNumber=(%d), pushUStep=(%d)[%d], getOrderType=(%d).\n",
			playerNumber, pushUStep, pushUStep&255, order->getOrderType());
	}
	
	if (players[playerNumber]->quitting)
	{
		delete order;
		order=new NullOrder();
	}
	
	Order *oldOrder=ordersQueue[playerNumber][pushUStep&255];
	assert(oldOrder);
	assert(oldOrder->ustep<pushUStep);
	delete oldOrder;
	
	order->sender=playerNumber;
	order->wishedLatency=myLocalWishedLatency;
	order->wishedDelay=myLocalWishedDelay;
	order->ustep=pushUStep;
	ordersQueue[playerNumber][pushUStep&255]=order;
	if (!order->gameCheckSum && players[playerNumber]->type==Player::P_LOCAL)
	{
		if (verbose)
			printf("Warning, no gameCheckSum localy provided for player %d, at pushUStep %d\n", playerNumber, pushUStep);
		fprintf(logFile, "Warning, no gameCheckSum localy provided for player %d, at pushUStep %d\n", playerNumber, pushUStep);
	}
	gameCheckSums[playerNumber][pushUStep&255]=order->gameCheckSum;
	
	if (localPlayerNumber==playerNumber && ((pushUStep&1)==1))
	{
		for (int pi=0; pi<numberOfPlayer; pi++)
			if (players[pi]->type==Player::P_IP)
				sendPushOrder(pi);
			else
				lastUStepReceivedFromMe[pi]=pushUStep; // for AI and local player.
	}
}

Order *NetGame::getOrder(int playerNumber)
{
	assert((playerNumber>=0) && (playerNumber<numberOfPlayer));
	
	assert(players[playerNumber]->type!=Player::P_LOST_DROPPING);
	
	if (players[localPlayerNumber]->quitting)
	{
		if (playerNumber!=localPlayerNumber)
		{
			Order *order=new NullOrder();
			order->sender=playerNumber;
			order->needToBeFreedByEngine=true;
			return order;
		}
		bool canQuit=true;
		Uint32 minUStep=0xFFFFFFFF;
		for (int pi=0; pi<numberOfPlayer; pi++)
			if (players[pi]->type==Player::P_IP)
			{
				Uint32 us=lastUStepReceivedFromMe[pi];
				if (minUStep<us)
					minUStep=us;
			}
		if (minUStep<players[playerNumber]->quitUStep)
		{
			fprintf(logFile, "can't quit now while (minUStep=%d < quitUStep=%d)\n", minUStep, minUStep<players[playerNumber]->quitUStep);
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (players[pi]->type==Player::P_IP)
					fprintf(logFile, " lastUStepReceivedFromMe[%d]=%d\n", pi, lastUStepReceivedFromMe[pi]);
			canQuit=false;
		}
		Order *order;
		if (canQuit)
		{
			players[playerNumber]->type=Player::P_LOST_FINAL;
			players[playerNumber]->team->checkControllingPlayers();
			fprintf(logFile, "players[%d]->type=Player::P_LOST_FINAL, me, quited\n", playerNumber);
			order=new QuitedOrder();
			dropStatusCommuniquedToGui[playerNumber]=true;
		}
		else
		{
			order=new NullOrder();
		}
		order->sender=playerNumber;
		order->needToBeFreedByEngine=true;
		return order;
	}
	else if (waitingForPlayerMask || hadToWaitThisStep)
	{
		Order *order=new WaitingForPlayerOrder(whoMaskCountedOut());
		order->sender=playerNumber;
		order->needToBeFreedByEngine=true;
		return order;
	}
	else if (players[playerNumber]->type==Player::P_LOST_FINAL)
	{
		Order *order;
		if (dropStatusCommuniquedToGui[playerNumber])
			order=new NullOrder();
		else
		{
			order=new DeconnectedOrder();
			dropStatusCommuniquedToGui[playerNumber]=true;
		}
		order->sender=playerNumber;
		order->needToBeFreedByEngine=true;
		return order;
	}
	else if (players[playerNumber]->quitting)
	{
		Uint8 latency=pushUStep-executeUStep;
		assert(players[playerNumber]->quitUStep);
		if (executeUStep>=players[playerNumber]->quitUStep+latency)
		{
			players[playerNumber]->type=Player::P_LOST_FINAL;
			players[playerNumber]->team->checkControllingPlayers();
			fprintf(logFile, "players[%d]->type=Player::P_LOST_FINAL, quited\n", playerNumber);
			assert(executeUStep==players[playerNumber]->quitUStep+latency);
		}
		dropStatusCommuniquedToGui[playerNumber]=true;
		Order *order=new NullOrder();
		order->sender=playerNumber;
		order->needToBeFreedByEngine=true;
		return order;
	}
	else
	{
		Order *order=ordersQueue[playerNumber][executeUStep&255];
		if (!order)
		{
			fprintf(logFile, "getOrder::error::!order at playerNumber=%d, executeUStep=%d\n", playerNumber, executeUStep);
			fclose(logFile);
		}
		assert(order);
		if (order->ustep)
			assert(order->ustep==executeUStep);
		if (players[playerNumber]->type==Player::P_LOST_DROPPING && executeUStep==players[playerNumber]->lastUStepToExecute)
		{
			players[playerNumber]->type=Player::P_LOST_FINAL;
			players[playerNumber]->team->checkControllingPlayers();
			fprintf(logFile, "players[%d]->type=Player::P_LOST_FINAL, dropped, executeUStep=%d\n", playerNumber, executeUStep);
		}
		
		assert(whoMaskAreWeWaitingFor()==0);
		
		bool good = true;
		for (int pai=0; pai<numberOfPlayer && good; pai++)
			if (!players[pai]->quitting && (players[pai]->type==Player::P_IP || players[pai]->type==Player::P_LOCAL))
				for (int pbi=0; pbi<pai && good; pbi++)
					if (!players[pbi]->quitting && (players[pbi]->type==Player::P_IP || players[pbi]->type==Player::P_LOCAL))
					{
						Uint32 checkSumsA=gameCheckSums[pai][executeUStep&255];
						Uint32 checkSumsB=gameCheckSums[pbi][executeUStep&255];
						if (checkSumsA!=checkSumsB)
						{
							printf("World desynchronisation at executeUStep %d.\n", executeUStep);
							printf(" player %2d has a checkSum=%x\n", pai, checkSumsA);
							printf(" player %2d has a checkSum=%x\n", pbi, checkSumsB);
							fprintf(logFile, "World desynchronisation at executeUStep %d.\n", executeUStep);
							fprintf(logFile, " player %2d has a checkSum=%x\n", pai, checkSumsA);
							fprintf(logFile, " player %2d has a checkSum=%x\n", pbi, checkSumsB);
							
							// dumping game to text
							OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("glob2.world-desynchronization.dump.txt"));
							if (stream->isEndOfStream())
							{
								std::cerr << "Can't dump full game memory to file glob2.world-desynchronization.dump.txt" << std::endl;
							}
							else
							{
								std::cerr << "Dump full game memory" << std::endl;
								players[localPlayerNumber]->game->save(stream, false, "glob2.world-desynchronization.dump.txt");
							}
							delete stream;
							
							good = false;
						}
							//else
							//	printf("Player %d and %d both have gameCheckSum=%x at executeUStep=%d\n", pai, pbi, checkSumsA, executeUStep);
					}
					
		/* 	We still need those checksum even if we have the complete game state in text file.
			Indeed, the file glob2.world-desynchronization.dump.txt will contain the game state
			as it is when the desynchronised has been noticed, which, due to the time orders
			(and thus checksums) take to transit over the network, can be several ticks after it
			has been produced.
			We thus need to log the checksums that correspond to the time the desynchronisation
			has been produced. They are much less readable than glob2.world-desynchronization.dump.txt
			because they are faster to create. Indeed, they are a tradeoff between network game speed
			and readability of logs.
		*/
		if (!good)
		{
			FILE *logFile;
			int ci;
			std::vector<Uint32> checkSumsVectorForBuildings = checkSumsVectorsStorageForBuildings[executeUStep&255];
			std::vector<Uint32> checkSumsVectorForUnits = checkSumsVectorsStorageForUnits[executeUStep&255];
			std::vector<Uint32> checkSumsVector = checkSumsVectorsStorage[executeUStep&255];
			
			ci=0;
			logFile=globalContainer->logFileManager->getFile("ChecksumUnits.log");
			fprintf(logFile, "my checkSumsListForUnits at ustep=%d is:\n", executeUStep);
			for (std::vector<Uint32>::iterator csi=checkSumsVectorForUnits.begin(); csi!=checkSumsVectorForUnits.end(); csi++)
				fprintf(logFile, "[%3d] %d\n", ci++, (Sint32)*csi);
			fflush(logFile);
			
			ci=0;
			logFile=globalContainer->logFileManager->getFile("ChecksumBuildings.log");
			fprintf(logFile, "my checkSumsListForBuildings at ustep=%d is:\n", executeUStep);
			for (std::vector<Uint32>::iterator csi=checkSumsVectorForBuildings.begin(); csi!=checkSumsVectorForBuildings.end(); csi++)
				fprintf(logFile, "[%3d] %x\n", ci++, *csi);
			fflush(logFile);
			
			//ci=0;
			//printf("my checkSum at ustep=%d is:\n", executeUStep);
			//for (std::list<Uint32>::iterator csi=checkSumsList.begin(); csi!=checkSumsList.end(); csi++)
			//	printf("[%3d] %x\n", ci++, *csi);
			
			ci=0;
			logFile=globalContainer->logFileManager->getFile("Checksum.log");
			fprintf(logFile, "my checkSum at ustep=%d is:\n", executeUStep);
			for (std::vector<Uint32>::iterator csi=checkSumsVector.begin(); csi!=checkSumsVector.end(); csi++)
				fprintf(logFile, "[%3d] %x\n", ci++, *csi);
			fflush(logFile);
			
			dumpStats();
			
			assert(false);
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (gameCheckSums[localPlayerNumber]!=gameCheckSums[pi])
				{
					fprintf(logFile, "Player %d dropped for checksum\n", pi);
					players[pi]->type=Player::P_LOST_FINAL;
					players[pi]->team->checkControllingPlayers();
				}
		}
		
		if (order->getOrderType()==ORDER_PLAYER_QUIT_GAME)
		{
			PlayerQuitsGameOrder *pqgo=(PlayerQuitsGameOrder *)order;
			assert(pqgo->player==playerNumber);
			fprintf(logFile, "players[%d]->quitting, executeUStep=%d\n", playerNumber, executeUStep);
			players[playerNumber]->quitting=true;
			players[playerNumber]->quitUStep=executeUStep;
		}
		
		wishedLatency[playerNumber]=order->wishedLatency;
		recentsWishedDelay[playerNumber][executeUStep&255]=order->wishedDelay;
		if (players[playerNumber]->type==Player::P_IP)
		{
			fprintf(logFile, "wishedLatency[%d]=%d\n", playerNumber, order->wishedLatency);
			fprintf(logFile, "wishedDelay[%d]=%d\n", playerNumber, order->wishedDelay);
		}
		if (order->getOrderType()!=ORDER_NULL)
			fprintf(logFile, "getOrderType(p=%d)->type==%d\n", playerNumber, order->getOrderType());
		return order;
	}
}

void NetGame::computeMyLocalWishedLatency()
{
	int pingPongMin[32];
	int pingPongMax[32];
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP)
		{
			// We compute the pingPongMax[pi] and pingPongMin[pi]:
			int min=INT_MAX;
			int max=0;
			for (int ri=0; ri<1024; ri++)
			{
				int delay=recentsPingPong[pi][ri];
				if (min>delay)
					min=delay;
				if (max<delay)
					max=delay;
			}
			pingPongMin[pi]=min;
			pingPongMax[pi]=max;
		}
	
	int maxPingPongMax=0; // max over each player.
	static int lastMaxPingPongMax=-1;
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP && maxPingPongMax<pingPongMax[pi])
			maxPingPongMax=pingPongMax[pi];
	if (maxPingPongMax!=lastMaxPingPongMax)
	{
		if (verbose)
			printf("new maxPingPongMax=%d[ms]\n", maxPingPongMax);
		fprintf(logFile, "new maxPingPongMax=%d[ms]\n", maxPingPongMax);
		lastMaxPingPongMax=maxPingPongMax;
	}
	int goodLatency=ordersByPackets+1+maxPingPongMax/80;
	if (goodLatency<1)
		goodLatency=1;
	
	if (goodLatency<40)
		goodLatencyStats[goodLatency]++;
	else
		goodLatencyStats[39]++;
	
	Uint8 latency=pushUStep-executeUStep;
	
	if (goodLatency<latency)
		myLocalWishedLatency=latency-1;
	else if (goodLatency>latency)
		myLocalWishedLatency=latency+1;
	else
		myLocalWishedLatency=latency;
	if (myLocalWishedLatency!=latency)
	{
		static int lastMyLocalWishedLatency=-1;
		if (lastMyLocalWishedLatency!=myLocalWishedLatency)
		{
			if (verbose)
				printf("myLocalWishedLatency=%d[ticks]\n", myLocalWishedLatency);
			lastMyLocalWishedLatency=myLocalWishedLatency;
		}
		fprintf(logFile, "myLocalWishedLatency=%d[ticks]\n", myLocalWishedLatency);
	}
}

void NetGame::treatData(Uint8 *data, int size, IPaddress ip)
{
	fprintf(logFile, "treatData\n");
	if (size<16)
	{
		fprintf(logFile, " Error, dangerous too small packet received from %s, v1\n", Utilities::stringIP(ip));
		return;
	}
	if (data[0]!=MULTIPLE_ORDERS)
	{
		fprintf(logFile, " Error, packet data[0]=%d packet received from %s, v2\n", data[0], Utilities::stringIP(ip));
		return;
	}
	if (data[1]!=0 || data[3]!=0)
	{
		fprintf(logFile, " Error, bad pad item received from %s, v3\n", Utilities::stringIP(ip));
		return;
	}
	int player=data[2];
	fprintf(logFile, " player=%d\n", player);
	if (player<0
		|| player>=32
		|| player>=numberOfPlayer
		|| (players[player]->type!=Player::P_IP
			&& players[player]->type!=Player::P_LOST_FINAL
			&& players[player]->type!=Player::P_LOST_DROPPING))
	{
		fprintf(logFile, " Error, bad player number (%d) received from %s, v4\n", player, Utilities::stringIP(ip));
		return;
	}
	Uint32 receivedUStep=getUint32(data, 4);
	if (receivedUStep)
	{
		if (lastUStepReceivedFromMe[player]>receivedUStep && players[player]->type==Player::P_IP)
		{
			fprintf(logFile, " Error, bad receivedUStep=%d while lastUStepReceivedFromMe[%d]=%d received from %s, v5\n",
				receivedUStep, player, lastUStepReceivedFromMe[player], Utilities::stringIP(ip));
			return;
		}
		lastUStepReceivedFromMe[player]=receivedUStep;
	}
	fprintf(logFile, " receivedUStep=%d\n", receivedUStep);
	Uint32 sdlTicksStart=getUint32(data, 8);
	if (sdlTicksStart)
	{
		Uint32 pingPong=SDL_GetTicks()-sdlTicksStart;
		recentsPingPong[player][receivedUStep&1023]=pingPong;
		if (pingPong<1024)
			pingPongStats[player][pingPong]++;
		else
			pingPongStats[player][1023]++;
		fprintf(logFile, " pingPong=%d\n", pingPong);
	}
	lastSdlTickReceivedFromHim[player]=getUint32(data, 12);
	
	fprintf(logFile, " data ");
	for (int i=0; i<size; i++)
		fprintf(logFile, "[%3d]%3d ", i, data[i]);
	fprintf(logFile, "\n");
	
	int l=16;
	while (l<size)
	{
		if (size-l<8)
		{
			fprintf(logFile, " Error, too small packet received from %s, v6\n", Utilities::stringIP(ip));
			return;
		}
		Uint32 orderUStep=getUint32(data, l);
		fprintf(logFile, " orderUStep=%d\n", orderUStep);
		if (orderUStep==0)
		{
			fprintf(logFile, " Error, can't treat packet order with no orderUStep, received from %s, v7\n", Utilities::stringIP(ip));
			return;
		}
		l+=4;
		Uint32 gameCheckSum=getUint32(data, l);
		l+=4;
		int orderSize=data[l++];
		Uint8 receivedWishedLatency=data[l++];
		Uint8 receivedWishedDelay=data[l++];
		Uint8 orderType=data[l++];
		fprintf(logFile, " oneOrder player=%d, orderUStep=%d, orderSize=%d, orderType=%d, receivedWishedLatency=%d, receivedWishedDelay=%d\n",
				player, orderUStep, orderSize, orderType, receivedWishedLatency, receivedWishedDelay);
	
		if (!players[player]->sameip(ip))
			if (dropState!=DS_ExchangingOrders)
				fprintf(logFile, "  Warning, late packet or bad packet from ip=%s, dropState=%d\n", Utilities::stringIP(ip), dropState);
		
		assert(players[player]->type==Player::P_NONE
			|| players[player]->type==Player::P_LOST_DROPPING
			|| players[player]->type==Player::P_LOST_FINAL
			|| players[player]->type==Player::P_IP);

		if (players[player]->type==Player::P_LOST_FINAL)
		{
			// We have enough orders from player, but he may need something more from us:
			if (players[player]->quitting)
				sendWaitingForPlayerOrder(player);
		}
		else if (orderType==ORDER_WAITING_FOR_PLAYER)
		{
			if (orderSize!=4)
			{
				fprintf(logFile, "  Error, bad size (%d) of ORDER_WAITING_FOR_PLAYER order from ip=%s\n",
					orderSize, Utilities::stringIP(ip));
				return;
			}
			if (gameCheckSum!=0)
			{
				fprintf(logFile, "  Error, non-null gameCheckSum (%d) of ORDER_WAITING_FOR_PLAYER order from ip=%s\n",
					gameCheckSum, Utilities::stringIP(ip));
				return;
			}
			Uint32 wfpm=getUint32(data, l);
			l+=4;
			fprintf(logFile, "  player %d has wfpm=%x\n", player, wfpm);
		}
		else if (orderType==ORDER_DROPPING_PLAYER)
		{
			if (orderSize<8+4)
			{
				fprintf(logFile, "  Error, too small (%d) ORDER_DROPPING_PLAYER order from ip=%s, v1\n",
					orderSize, Utilities::stringIP(ip));
				return;
			}
			if (gameCheckSum!=0)
			{
				fprintf(logFile, "  Error, non-null gameCheckSum (%d) of ORDER_DROPPING_PLAYER order from ip=%s\n",
					gameCheckSum, Utilities::stringIP(ip));
				return;
			}
			if (dropState<DS_ExchangingDroppingMasks)
			{
				dropState=DS_ExchangingDroppingMasks;
				fprintf(logFile, "  dropState=DS_ExchangingDroppingMasks, player %d request.\n", player);
				for (int p=0; p<numberOfPlayer; p++)
					droppingPlayersMask[p]=0;
			}
			bool askForReply=(data[l]!=0);
			if (data[l+1]!=0 || data[l+2]!=0 || data[l+3]!=0)
			{
				fprintf(logFile, "  Error, bad pad in ORDER_DROPPING_PLAYER order from ip=%s\n", Utilities::stringIP(ip));
				return;
			}
			l+=4;
			Uint32 dropMask=getUint32(data, l);
			l+=4;
			if (dropState==DS_ExchangingDroppingMasks)
			{
				droppingPlayersMask[player]=dropMask;
				droppingPlayersMask[localPlayerNumber]|=dropMask;
			}
			else
			{
				fprintf(logFile, "  Warning, received an ORDER_DROPPING_PLAYER from ip=%s, while dropState=%d\n", Utilities::stringIP(ip), dropState);
				return;
			}
			assert(orderUStep);
			lastExecutedUStep[player]=orderUStep-1;
			
			fprintf(logFile, "  player %d has askForReply=%d, dropMask=%x, lastExecutedStep=%d, orderSize=%d\n",
					player, askForReply, dropMask, orderUStep-1, orderSize);
			
			if (askForReply && players[player]->type==Player::P_IP)
				sendDroppingPlayersMask(player, false);
			
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (players[pi]->type==Player::P_IP && dropMask&(1<<pi))
				{
					if (orderSize<l+4)
					{
						fprintf(logFile, "  Error, too small (%d) ORDER_DROPPING_PLAYER order from ip=%s, v2\n",
							orderSize, Utilities::stringIP(ip));
						return;
					}
					lastAvailableUStep[player][pi]=getUint32(data, l);
					l+=4;
					fprintf(logFile, "  lastAvailableUStep[%d][%d]=%d.\n", player, pi, lastAvailableUStep[player][pi]);
				}
		}
		else if (orderType==ORDER_REQUESTING_AWAY)
		{
			Uint8 missingPlayer=data[l];
			if (data[l+1]!=0 || data[l+2]!=0 || data[l+3]!=0)
			{
				fprintf(logFile, "  Error, bad pad in ORDER_REQUESTING_AWAY order from ip=%s\n", Utilities::stringIP(ip));
				return;
			}
			if (gameCheckSum!=0)
			{
				fprintf(logFile, "  Error, non-null gameCheckSum (%d) of ORDER_REQUESTING_AWAY order from ip=%s\n",
					gameCheckSum, Utilities::stringIP(ip));
				return;
			}
			l+=4;
			sendDeadAwayOrder(missingPlayer, player, orderUStep);
		}
		else
		{
			if (players[player]->type==Player::P_LOST_DROPPING && dropState!=DS_ExchangingOrders)
			{
				fprintf(logFile, "  packet from a P_LOST_DROPPING player=%d, while dropState=%d, orderUStep=%d\n", player, dropState, orderUStep);
				return;
			}
			Order *order=Order::getOrder(data+l-1, orderSize+1);
			l+=orderSize;
			if (!order)
				fflush(logFile);
			assert(order);
			assert(order->getOrderType()==orderType);
			order->ustep=orderUStep;
			
			// Is this order late ?
			Order *oldOrder=ordersQueue[player][orderUStep&255];
			assert(oldOrder);
			if (oldOrder->ustep>orderUStep)
			{
				fprintf(logFile, "  Warning, late order with ustep=%d, can't replace allready stored order with ustep=%d, as hash=%d for both.\n",
					orderUStep, oldOrder->ustep, orderUStep&255);
				delete order;
				return;
			}
			else if (oldOrder->ustep==orderUStep)
			{
				fprintf(logFile, "  Warning, duplicated packet. player=%d, orderUStep=%d, v-n\n", player, orderUStep);
				duplicatedPacketStats[player]++;
				if (oldOrder->getOrderType()!=order->getOrderType())
				{
					fprintf(logFile, "  Error, oldOrderType=%d, newOrderType=%d\n", oldOrder->getOrderType(), order->getOrderType());
					fflush(logFile);
					assert(false);
				}
				delete order;
				return;
			}
			else if (order->getOrderType()==ORDER_QUITED || order->getOrderType()==ORDER_DECONNECTED)
			{
				fprintf(logFile, "  Error, player %d with ip %s sent us an %d internal order",
					player, Utilities::stringIP(ip), order->getOrderType());
				delete order;
				return;
			}
			
			// We can store it:
			delete oldOrder;
			order->sender=player;
			order->wishedLatency=receivedWishedLatency;
			order->wishedDelay=receivedWishedDelay;
			order->gameCheckSum=gameCheckSum;
			ordersQueue[player][orderUStep&255]=order;
			// Let's store checkSum:
			gameCheckSums[player][orderUStep&255]=gameCheckSum;
			fprintf(logFile, "  orderUStep=%d, hash=%d\n", orderUStep, orderUStep&255);
			fprintf(logFile, "  orderType=%d\n", orderType);
			fprintf(logFile, "  gameCheckSum=%x\n", order->gameCheckSum);
			
			if (players[player]->type==Player::P_LOST_DROPPING && dropState==DS_ExchangingOrders)
				lastAvailableUStep[localPlayerNumber][player]=lastUsableUStepReceivedFromHim(player);
		}
		assert(l<=size);
	}
	
	countDown[player]=0;
}

void NetGame::receptionStep(void)
{ 
	if (socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_GAME_PACKET_SIZE);
		assert(packet);
		while (SDLNet_UDP_Recv(socket, packet)==1)
			treatData(packet->data, packet->len, packet->address);
		SDLNet_FreePacket(packet);
	}
}

bool NetGame::stepReadyToExecute(void)
{
	receptionStep();
	
	if (dropState<DS_ExchangingOrders)
	{
		bool someoneToDrop=false;
		for (int pi=0; pi<numberOfPlayer; pi++)
			if (players[pi]->type==Player::P_IP && !players[pi]->quitting && countDown[pi]++>countDownMax)
				someoneToDrop=true;
		if (someoneToDrop)
		{
			if (dropState<DS_ExchangingDroppingMasks)
			{
				dropState=DS_ExchangingDroppingMasks;
				fprintf(logFile, "dropState=DS_ExchangingDroppingMasks, I choosed.\n");
				for (int p=0; p<numberOfPlayer; p++)
					droppingPlayersMask[p]=0;
			}
			Uint32 myDroppingPlayersMask=0;
			for (int pi=numberOfPlayer; pi<32; pi++)
				myDroppingPlayersMask|=1<<pi;
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (countDown[pi]>countDownMax)
					myDroppingPlayersMask|=1<<pi;
			assert(myDroppingPlayersMask);
			droppingPlayersMask[localPlayerNumber]|=myDroppingPlayersMask;
		}
	}
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP && !players[pi]->quitting && countDown[pi]>=countDownMax)
			fprintf(logFile, "countDown[%d]=%d\n", pi, countDown[pi]);
	
	if (dropState)
	{
		if (dropState==DS_ExchangingDroppingMasks)
		{
			Uint32 myDroppingPlayersMask=droppingPlayersMask[localPlayerNumber];
			for (int pi=0; pi<numberOfPlayer; pi++)
				if ((myDroppingPlayersMask&(1<<pi))==0 && players[pi]->type==Player::P_IP && !players[pi]->quitting)
					myDroppingPlayersMask|=droppingPlayersMask[pi];
			droppingPlayersMask[localPlayerNumber]|=myDroppingPlayersMask;
			bool iAgreeWithEveryone=true;
			for (int pi=0; pi<numberOfPlayer; pi++)
				if ((myDroppingPlayersMask&(1<<pi))==0 && players[pi]->type==Player::P_IP && !players[pi]->quitting)
				{
					if (myDroppingPlayersMask!=droppingPlayersMask[pi])
					{
						iAgreeWithEveryone=false;
						sendDroppingPlayersMask(pi, true);
					}
					else
						sendDroppingPlayersMask(pi, false); // Only to tell player p that we are alive.
				}
			if (iAgreeWithEveryone)
			{
				fprintf(logFile, "iAgreeWithEveryone, myDroppingPlayersMask=%x.\n", myDroppingPlayersMask);
				for (int pi=0; pi<numberOfPlayer; pi++)
					if (players[pi]->type==Player::P_IP && !players[pi]->quitting && (myDroppingPlayersMask&(1<<pi)))
					{
						players[pi]->type=Player::P_LOST_DROPPING;
						players[pi]->team->checkControllingPlayers();
					}
				dropState=DS_ExchangingOrders;
			}
		}
		if (dropState==DS_ExchangingOrders)
		{
			lastExecutedUStep[localPlayerNumber]=executeUStep-1;
			for (int pi=0; pi<numberOfPlayer; pi++)
				lastAvailableUStep[localPlayerNumber][pi]=lastUsableUStepReceivedFromHim(pi);
			int n=0;
			for (int pi=0; pi<numberOfPlayer; pi++)
				if ((players[pi]->type==Player::P_IP && !players[pi]->quitting) || players[pi]->type==Player::P_LOCAL)
					n++;
			fprintf(logFile, " n=%d\n", n);
			assert(n);
			Uint32 maxLastExecutedUStep=0; // What is the last order executed by any player ? All players will have to reach this ustep too.
			for (int pi=0; pi<numberOfPlayer; pi++)
				if ((players[pi]->type==Player::P_IP && !players[pi]->quitting) || players[pi]->type==Player::P_LOCAL)
				{
					Uint32 les=lastExecutedUStep[pi];
					if (maxLastExecutedUStep<les)
						maxLastExecutedUStep=les;
				}
			fprintf(logFile, " maxLastExecutedUStep=%d.\n", maxLastExecutedUStep);
			bool upToDatePlayer[32]; // Which players has all needed orders ?
			for (int pi=0; pi<32; pi++)
				upToDatePlayer[pi]=false;
			for (int pi=0; pi<numberOfPlayer; pi++)
				if ((players[pi]->type==Player::P_IP && !players[pi]->quitting) || players[pi]->type==Player::P_LOCAL)
					if (lastExecutedUStep[pi]==maxLastExecutedUStep)
					{
						upToDatePlayer[pi]=true;
						fprintf(logFile, " player %d is upToDate (leus=%d)\n", pi, lastExecutedUStep[pi]);
					}
					else
						fprintf(logFile, " player %d is not upToDate (leus=%d)\n", pi, lastExecutedUStep[pi]);
			
			// Has everyone all orders ?
			bool hasAllOrder[32][32];
			for (int pai=0; pai<32; pai++)
				for (int pbi=0; pbi<32; pbi++)
					hasAllOrder[pai][pbi]=true;
			for (int pai=0; pai<numberOfPlayer; pai++)
				if ((players[pai]->type==Player::P_IP && !players[pai]->quitting) || players[pai]->type==Player::P_LOCAL)
					for (int pbi=0; pbi<numberOfPlayer; pbi++)
						if (players[pbi]->type==Player::P_LOST_DROPPING)
						{
							if (lastAvailableUStep[pai][pbi]<maxLastExecutedUStep)
								hasAllOrder[pai][pbi]=false;
							fprintf(logFile, " lastAvailableUStep[%d][%d]=%d\n", pai, pbi, lastAvailableUStep[pai][pbi]);
							fprintf(logFile, " hasAllOrder[%d][%d]=%d\n", pai, pbi, hasAllOrder[pai][pbi]);
						}
			bool sentPacketToPlayer[32];
			for (int pi=0; pi<32; pi++)
				sentPacketToPlayer[pi]=false;
			for (int pbi=0; pbi<numberOfPlayer; pbi++)
				if (players[pbi]->type==Player::P_LOST_DROPPING)
				{
					Uint32 resendingUStep=lastAvailableUStep[localPlayerNumber][pbi]+1;
					for (int pai=0; pai<numberOfPlayer; pai++)
						if ((players[pai]->type==Player::P_IP && !players[pai]->quitting) || players[pai]->type==Player::P_LOCAL)
							if (lastAvailableUStep[pai][pbi]>=resendingUStep)
							{
								fprintf(logFile, " we request player=%d to send us order ustep=%d of player=%d\n", pai, resendingUStep, pbi);
								sendRequestingDeadAwayOrder(pbi, pai, resendingUStep);
								sentPacketToPlayer[pai]=true;
								resendingUStep++;
								if (resendingUStep>=maxLastExecutedUStep)
									break;
							}
				}
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (!sentPacketToPlayer[pi] && players[pi]->type==Player::P_IP && !players[pi]->quitting)
					sendDroppingPlayersMask(pi, false); // Only to tell player p that we are alive.
					
			bool iHaveAll=true;
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (players[pi]->type==Player::P_LOST_DROPPING)
					if (!hasAllOrder[localPlayerNumber][pi])
					{
						iHaveAll=false;
						break;
					}
			if (iHaveAll)
			{
				for (int pi=0; pi<numberOfPlayer; pi++)
					if (players[pi]->type==Player::P_LOST_DROPPING)
						players[pi]->lastUStepToExecute=maxLastExecutedUStep;
				assert(maxLastExecutedUStep>=executeUStep-1);
				if (maxLastExecutedUStep==executeUStep-1)
					for (int pi=0; pi<numberOfPlayer; pi++)
						if (players[pi]->type==Player::P_LOST_DROPPING)
						{
							players[pi]->type=Player::P_LOST_FINAL;
							players[pi]->team->checkControllingPlayers();
						}
				fprintf(logFile, " we have all needed orders.\n");
				dropState=DS_NoDropProcessing;
			}
		}
		hadToWaitThisStep=true;
		return false;
	}
	else
	{
		waitingForPlayerMask=whoMaskAreWeWaitingFor();
		if (waitingForPlayerMask)
			fprintf(logFile, "waitingForPlayerMask=%x\n", waitingForPlayerMask);
		if (waitingForPlayerMask==0)
		{
			computeMyLocalWishedLatency();
			bool success=computeNumberOfStepsToEat();
			hadToWaitThisStep=!success;
			return success;
		}
		else
		{
			hadToWaitThisStep=true;
			// We have to tell the others that we are still here, but we are waiting for someone:
			for (int p=0; p<numberOfPlayer; p++)
				if (players[p]->type==Player::P_IP)
					sendWaitingForPlayerOrder(p);
			return false;
		}
	}
}

bool NetGame::computeNumberOfStepsToEat(void)
{
	Uint8 targetLatency=0;// The gobaly-choosen-latency, we have to reach.
	int n=0;
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP || players[pi]->type==Player::P_LOCAL)
		{
			Uint8 l=wishedLatency[pi];
			if (l)
			{
				n++;
				if (targetLatency<l)
					targetLatency=l;
			}
		}
	Uint8 latency=pushUStep-executeUStep;
	if (n==0)
		targetLatency=latency;
	else if (latency!=targetLatency)
		fprintf(logFile, "computeNumberOfStepsToEat executeUStep=%d, wishedLatency=(%d, %d), latency=%d, targetLatency=%d.\n",
				executeUStep, wishedLatency[0], wishedLatency[1], latency, targetLatency);
	numberOfStepsToEat=1;
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->quitting && (players[pi]->type==Player::P_IP || players[pi]->type==Player::P_LOCAL))
		{
			fprintf(logFile, " player %d quitting, no latency changing available\n", pi);
			return true;
		}
	if (targetLatency>latency)
	{
		bool allNullOrders=true;
		for (int pi=0; pi<numberOfPlayer; pi++)
			if (players[pi]->type==Player::P_IP || players[pi]->type==Player::P_LOCAL)
			{
				Order *order=ordersQueue[pi][executeUStep&255];
				assert(order);
				assert(order->ustep==executeUStep);
				Uint8 type=order->getOrderType();
				if (type!=ORDER_NULL)
				{
					allNullOrders=false;
					break;
				}
			}
		if (allNullOrders)
		{
			numberOfStepsToEat=0;
			 // We simply execute all thoses useless orders twice.
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (players[pi]->type==Player::P_IP || players[pi]->type==Player::P_LOCAL)
				{
					Order *order=ordersQueue[pi][executeUStep&255];
					if (order)
						delete order;
					ordersQueue[pi][executeUStep&255]=NULL;
					NullOrder *nullOrder=new NullOrder();
					nullOrder->sender=pi;
					nullOrder->wishedLatency=0;
					nullOrder->latencyPadding=true;
					nullOrder->ustep=executeUStep;
					ordersQueue[pi][executeUStep&255]=nullOrder;
				}
		}
	}
	else if (targetLatency<latency)
	{
		bool allNullOrders=true;
		for (int pi=0; pi<numberOfPlayer; pi++)
			if (players[pi]->type==Player::P_IP || players[pi]->type==Player::P_LOCAL)
			{
				Order *order=ordersQueue[pi][(executeUStep+1)&255];
				assert(order);
				Uint8 type=order->getOrderType();
				if (order->ustep!=executeUStep+1)
				{
					fprintf(logFile, " step not ready.\n");
					return false;
				}
				if (type!=ORDER_NULL)
				{
					allNullOrders=false;
					break;
				}
			}
		if (allNullOrders)
			numberOfStepsToEat=2; // We simply skip all thoses useless orders !
	}
	if (latency!=targetLatency)
		fprintf(logFile, " numberOfStepsToEat=%d, waitingForPlayerMask=%x\n", numberOfStepsToEat, waitingForPlayerMask);
	return true;
}

void NetGame::stepExecuted(void)
{
	if (!hadToWaitThisStep)
	{
		assert(numberOfStepsToEat>=0);
		assert(numberOfStepsToEat<=2);
		for (int i=0; i<numberOfStepsToEat; i++)
		{
			// OK, we have executed the "executeUStep"s orders, next we will get the next:
			executeUStep++;
			for (int pi=0; pi<numberOfPlayer; pi++)
				if (ordersQueue[pi][executeUStep&255]==NULL)
				{
					fprintf(logFile, "ordersQueue[%d][%d]!=NULL\n", pi, executeUStep&255);
					fflush(logFile);
					assert(false);
				}
		}
		
		// We currently don't change latency, then we simply also increment "pushUStep".
		// "pushUStep" allways increments on synchrone steps (when everything goes right). Latency changings
		pushUStep++;

		fprintf(logFile, "\n");
		fprintf(logFile, "executeUStep=%d [%d]\n", executeUStep, executeUStep&255);
		fprintf(logFile, "pushUStep=%d [%d]\n", pushUStep, pushUStep&255);

		// let's checkout for anything wrong...

		for (int pi=0; pi<numberOfPlayer; pi++)
			for (int si=0; si<256; si++)
				assert(ordersQueue[pi][si]);
	}
}

int NetGame::ticksToDelayInside(void)
{
	//We look for the slowest player, and get his median delay:
	int maxMedianWishedDelay=0;
	for (int pi=0; pi<numberOfPlayer; pi++)
		if (players[pi]->type==Player::P_IP || players[pi]->type==Player::P_LOCAL)
		{
			int medianWishedDelay;
			for (medianWishedDelay=0; medianWishedDelay<40; medianWishedDelay++)
			{
				int underCount=0;
				for (int si=0; si<256; si++)
					if (recentsWishedDelay[pi][si]<=medianWishedDelay)
						underCount++;
				if (underCount>=128)
					break;
			}
			if (maxMedianWishedDelay<medianWishedDelay)
				maxMedianWishedDelay=medianWishedDelay;
	}
	static int lastMaxMedianWishedDelay=-1;
	if (lastMaxMedianWishedDelay!=maxMedianWishedDelay)
	{
		fprintf(logFile, "new maxMedianWishedDelay=%d\n", maxMedianWishedDelay);
		lastMaxMedianWishedDelay=maxMedianWishedDelay;
	}
	if (maxMedianWishedDelay>=40)
	{
		fprintf(logFile, "Error, maxMedianWishedDelay out of bounds.\n");
		maxMedianWishedDelay=39;
	}
	maxMedianWishedDelayStats[maxMedianWishedDelay]++;
	
	int delayInside=maxMedianWishedDelay-myLocalWishedDelay;
	if (delayInside<0)
		delayInside=0;
	assert(delayInside<40);
	delayInsideStats[delayInside]++;
	return delayInside;
}

void NetGame::setLeftTicks(int leftTicks)
{
	myLocalWishedDelay=-leftTicks;
	if (myLocalWishedDelay<0)
		myLocalWishedDelay=0;
	else if (myLocalWishedDelay>39)
		myLocalWishedDelay=39;
	wishedDelayStats[myLocalWishedDelay]++;
}

std::vector<Uint32> *NetGame::getCheckSumsVectorsStorage()
{
	std::vector<Uint32> *checkSumsVectorStorage=&checkSumsVectorsStorage[pushUStep&255];
	checkSumsVectorStorage->clear();
	return checkSumsVectorStorage;
}

std::vector<Uint32> *NetGame::getCheckSumsVectorsStorageForBuildings()
{
	std::vector<Uint32> *checkSumsVectorStorageForBuildings=&checkSumsVectorsStorageForBuildings[pushUStep&255];
	checkSumsVectorStorageForBuildings->clear();
	return checkSumsVectorStorageForBuildings;
}

std::vector<Uint32> *NetGame::getCheckSumsVectorsStorageForUnits()
{
	std::vector<Uint32> *checkSumsVectorStorageForUnits=&checkSumsVectorsStorageForUnits[pushUStep&255];
	checkSumsVectorStorageForUnits->clear();
	return checkSumsVectorStorageForUnits;
}
