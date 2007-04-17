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

#include "MultiplayersJoin.h"
#include <GAG.h>
#include "NetDefine.h"
#include "Marshaling.h"
#include "Utilities.h"
#include "LogFileManager.h"
#include "GlobalContainer.h"
#include "NetConsts.h"

// If you don't have SDL_net 1.2.5 some features won't be available.
#ifndef INADDR_BROADCAST
#define INADDR_BROADCAST (0x7F000001)
#endif

MultiplayersJoin::MultiplayersJoin(bool shareOnYog)
:MultiplayersCrossConnectable()
{
	assert((int)BROADCAST_REQUEST==(int)YMT_BROADCAST_REQUEST);
	assert((int)BROADCAST_RESPONSE_LAN==(int)YMT_BROADCAST_RESPONSE_LAN);
	assert((int)BROADCAST_RESPONSE_YOG==(int)YMT_BROADCAST_RESPONSE_YOG);
	
	yogGameInfo=NULL;
	downloadStream=NULL;
	logFile=globalContainer->logFileManager->getFile("MultiplayersJoin.log");
	logFileDownload=globalContainer->logFileManager->getFile("MultiplayersJoinDownload.log");
	fprintf(logFile, "INADDR_BROADCAST=%s\n", Utilities::stringIP(INADDR_BROADCAST));
	assert(logFile);
	duplicatePacketFile=0;
	init(shareOnYog);
}

MultiplayersJoin::~MultiplayersJoin()
{
	if (destroyNet)
	{
		if (socket)
		{
			if (channel!=-1)
			{
				if (waitingState!=WS_TYPING_SERVER_NAME)
					send(CLIENT_QUIT_NEW_GAME);
				SDLNet_UDP_Unbind(socket, channel);
				channel=-1;
				fprintf(logFile, "~MultiplayersJoin::Socket unbinded channel=%d\n", channel);
			}
			
			for (int j=0; j<sessionInfo.numberOfPlayer; j++)
				if (sessionInfo.players[j].type==BasePlayer::P_IP)
					sessionInfo.players[j].unbind();
			
			SDLNet_UDP_Close(socket);
			socket=NULL;
			fprintf(logFile, "Socket closed.\n");
		}
	}
	
	if (duplicatePacketFile)
		fprintf(logFile, " duplicatePacketFile=%d\n", duplicatePacketFile);
	
	closeDownload();
}

void MultiplayersJoin::init(bool shareOnYog)
{
	waitingState=WS_TYPING_SERVER_NAME;
	waitingTimeout=0;
	waitingTimeoutSize=0;
	waitingTOTL=0;

	startGameTimeCounter=0;

	serverName[0]=0;
	playerName[0]=0;
	serverNickName[0]=0;
	
	isServer=false;
	serverIP.host=0;
	serverIP.port=0;
	ipFromNAT=false;
	
	kicked=false;
	
	if (shareOnYog)
		broadcastState=BS_DISABLE_YOG;
	else
		broadcastState=BS_ENABLE_LAN;
	broadcastTimeout=0;
	listHasChanged=false;
	
	this->shareOnYog=shareOnYog;
	
	if (yogGameInfo)
	{
		delete yogGameInfo;
		yogGameInfo=NULL;
	}
	
	closeDownload();
	
	logFile=globalContainer->logFileManager->getFile("MultiplayersJoin.log");
	assert(logFile);
	
	fprintf(logFile, "new MultiplayersJoin\n");
	fprintf(logFileDownload, "new MultiplayersJoin\n");
	
	if (duplicatePacketFile)
		fprintf(logFile, "duplicatePacketFile=%d\n", duplicatePacketFile);
	duplicatePacketFile=0;
	
	if (socket)
		SDLNet_UDP_Close(socket);
	socket=NULL;
	
	socket=SDLNet_UDP_Open(GAME_JOINER_PORT_1);
	if (!socket)
		socket=SDLNet_UDP_Open(GAME_JOINER_PORT_2);
	if (!socket)
		socket=SDLNet_UDP_Open(GAME_JOINER_PORT_3);
	if (!socket)
		socket=SDLNet_UDP_Open(ANY_PORT);
	if (socket)
	{
		IPaddress *localAddress=SDLNet_UDP_GetPeerAddress(socket, -1);
		fprintf(logFile, "Socket opened at ip(%s)\n", Utilities::stringIP(*localAddress));
		localPort=localAddress->port;
	}
	else
	{
		fprintf(logFile, "failed to open a socket.\n");
		broadcastState=BS_BAD;
		localPort=0;
	}
	fprintf(logFile, "broadcastState=%d\n", broadcastState);
}

void MultiplayersJoin::closeDownload()
{
	if (downloadStream)
	{
		fprintf(logFile, " download not finished.\n");
		SDL_RWclose(downloadStream);
		assert(filename.length() > 0);
		globalContainer->fileManager->remove(filename);
		filename.erase();
		downloadStream=NULL;
	}
	for (int i=0; i<PACKET_SLOTS; i++)
	{
		packetSlot[i].index=0;
		packetSlot[i].received=false;
		packetSlot[i].brandwidth=0;
	}
	unreceivedIndex=0;
	endOfFileIndex=0xFFFFFFFF;
	totalReceived=0;
	duplicatePacketFile=0;
	startDownloadTimeout=SHORT_NETWORK_TIMEOUT;
	brandwidth=0;
	receivedCounter=0;
}

void MultiplayersJoin::dataPresenceRecieved(Uint8 *data, int size, IPaddress ip)
{
	if (size!=12)
	{
		fprintf(logFile, "Bad size for a Presence packet recieved!\n");
		waitingState=WS_WAITING_FOR_PRESENCE;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}
	
	Uint8 serverNetProtocolVersion=data[4];
	Uint32 serverConfigCheckSum=getUint32(data, 8);
	fprintf(logFile, "dataPresenceRecieved (serverNetProtocolVersion=%d) (serverConfigCheckSum=%08x)\n",
			serverNetProtocolVersion, serverConfigCheckSum);
	
	if (serverNetProtocolVersion!=NET_PROTOCOL_VERSION || serverConfigCheckSum!=globalContainer->getConfigCheckSum())
	{
		fprintf(logFile, " bad serverNetProtocolVersion!=%d or serverConfigCheckSum!=%08x\n",
				NET_PROTOCOL_VERSION, globalContainer->getConfigCheckSum());
		if (shareOnYog)
		{
			if (serverNetProtocolVersion != NET_PROTOCOL_VERSION)
				yog->unjoinGame(false, Toolkit::getStringTable()->getString("[Can't join game, wrong game version]"));
			else if (serverConfigCheckSum != globalContainer->getConfigCheckSum())
				yog->unjoinGame(false, Toolkit::getStringTable()->getString("[Can't join game, missmatching game parameters]"));
			
		}
		waitingState=WS_TYPING_SERVER_NAME;
		waitingTimeout=0;
		waitingTimeoutSize=0;
		waitingTOTL=0;
		return;
	}
	
	waitingState=WS_WAITING_FOR_SESSION_INFO;
	waitingTimeout=0; //Timeout at one to (not-re) send. (hack but nice)
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	
	if (shareOnYog)
		yog->connectedToGameHost();
}

void MultiplayersJoin::dataSessionInfoRecieved(Uint8 *data, int size, IPaddress ip)
{
	if (size<12+2+10) // 10=min of getDataLength(true)
	{
		fprintf(logFile, "Bad size (%d) for a dataSessionInfoRecieved!\n", size);
		return;
	}
	
	int pn=getSint32(data, 4);
	myPlayerNumber=pn;
	Uint32 serverFileCheckSum=getUint32(data, 8);

	if ((pn<0)||(pn>=32))
	{
		fprintf(logFile, "Warning: bad dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);
		waitingTimeout=0;
		return;
	}
	fprintf(logFile, "dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);
	
	int serverNickNameSize=Utilities::strmlen((char *)data+12, 32);
	if (serverNickName[0])
	{
		if (strncmp(serverNickName, (char *)data+12, serverNickNameSize)==0)
		{
			fprintf(logFile, " same serverNickName=(%s)\n", serverNickName);
		}
		else
		{
			fprintf(logFile, " Warning: old serverNickName=(%s), new serverNickName=(%s)\n", serverNickName, data+4);
			return;
		}
	}
	else
	{
		memcpy(serverNickName, data+12, serverNickNameSize);
		fprintf(logFile, " serverNickName=(%s)\n", serverNickName);
	}

	unCrossConnectSessionInfo();

	if (!sessionInfo.setData(data+12+serverNickNameSize, size-12-serverNickNameSize, true))
	{
		fprintf(logFile, " Bad content, or bad size for a sessionInfo packet recieved!\n");
		return;
	}
	
	fprintf(logFile, " sessionInfo.numberOfPlayer=%d, numberOfTeam=%d, ipFromNAT=%d\n", sessionInfo.numberOfPlayer, sessionInfo.numberOfTeam, ipFromNAT);
	
	if (ipFromNAT || !shareOnYog)
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			sessionInfo.players[j].waitForNatResolution=false;
	else
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			sessionInfo.players[j].waitForNatResolution=sessionInfo.players[j].ipFromNAT;
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].type==BasePlayer::P_IP)
			if (j!=myPlayerNumber && sessionInfo.players[j].localhostip())
				sessionInfo.players[j].waitForNatResolution=true;
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		sessionInfo.players[j].ipFirewallClean=false;
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		fprintf(logFile, " player=(%d) ip=(%s) waitForNatResolution=(%d), ipFromNAT=(%d)\n", j, Utilities::stringIP(sessionInfo.players[j].ip), sessionInfo.players[j].waitForNatResolution, sessionInfo.players[j].ipFromNAT); 
	
	if (localPort)
	{
		fprintf(logFile, " I set my own ip to localhost, localPort=%d \n", SDL_SwapBE16(localPort));
		sessionInfo.players[myPlayerNumber].ip.host=SDL_SwapBE32(0x7F000001);
		sessionInfo.players[myPlayerNumber].ip.port=localPort;
	}
	
	validSessionInfo=true;
	waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
	waitingTimeout=0; //Timeout at one to (not-re) send. (hack but nice)
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	
	//do we need to download the file from host ? :
	if (downloadStream && (filename == sessionInfo.getFileName()))
	{
		fprintf(logFile, " we are already downloading the map.\n");
		fprintf(logFileDownload, " we are already downloading the map.\n");
	}
	else
	{
		fprintf(logFile, " we may need to download, we don't have a random map.\n");
		filename = sessionInfo.getFileName();
		
		assert(filename.length() > 0);
		fprintf(logFile, " filename=%s.\n", filename.c_str());
		SDL_RWops *stream=globalContainer->fileManager->open(filename, "rb");
		if (stream)
		{
			SDL_RWclose(stream);
			fprintf(logFile, " we have the file.\n");
			Uint32 myFileCheckSum=globalContainer->fileManager->checksum(filename);
			if (serverFileCheckSum==myFileCheckSum)
			{
				fprintf(logFile, "  we don't need to download, we have the correct file! checksum (%x)\n", serverFileCheckSum);
				filename.erase();
			}
			else
			{
				fprintf(logFile, "  we need to download, we have an outdated file! (l=)%x!=%x(=r)\n", myFileCheckSum, serverFileCheckSum);
			}
		}
		else
			fprintf(logFile, " we need to download, we don't have the file!\n");
		
		if (filename.length() > 0)
		{
			if (downloadStream)
			{
				if (duplicatePacketFile)
					fprintf(logFile, " duplicatePacketFile=%d\n", duplicatePacketFile);
				duplicatePacketFile=0;
				SDL_RWclose(downloadStream);
				downloadStream=NULL;
			}
			for (int i=0; i<PACKET_SLOTS; i++)
			{
				packetSlot[i].index=0;
				packetSlot[i].received=false;
				packetSlot[i].brandwidth=0;
			}
			unreceivedIndex=0;
			endOfFileIndex=0xFFFFFFFF;
			totalReceived=0;
			duplicatePacketFile=0;
			brandwidth=1;
			receivedCounter=0;
			
			// save as compressed stream
			downloadStream = globalContainer->fileManager->open(filename + ".gz", "wb");
			
			fprintf(logFile, " downloadStream=%p to %s.gz\n", downloadStream, filename.c_str());
			fprintf(logFileDownload, " downloadStream=%p to %s.gz\n", downloadStream, filename.c_str());
		}
	}
	
	if (startDownloadTimeout>2)
		startDownloadTimeout=2;
}

void MultiplayersJoin::dataFileRecieved(Uint8 *data, int size, IPaddress ip)
{
	if (data[1]==1)
	{
		if (size!=8)
		{
			fprintf(logFile, "Warning! bad dataFileRecieved packet size (%d) from (%s)!\n", size, Utilities::stringIP(ip));
			fprintf(logFileDownload, "Warning! bad dataFileRecieved packet size (%d) from (%s)!\n", size, Utilities::stringIP(ip));
		}
		else
		{
			endOfFileIndex=getUint32(data, 4);
			fprintf(logFileDownload, " received endOfFileIndex=%d\n", endOfFileIndex);
		}
		return;
	}
	if (downloadStream==NULL)
	{
		fprintf(logFile, " no more data file wanted. endOfFileIndex=%d, unreceivedIndex=%d\n", endOfFileIndex, unreceivedIndex);
		fprintf(logFileDownload, " no more data file wanted. endOfFileIndex=%d, unreceivedIndex=%d\n", endOfFileIndex, unreceivedIndex);
		
		Uint8 data[8];
		memset(data, 0, 8);
		data[0]=NEW_PLAYER_WANTS_FILE;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		addUint32(data, endOfFileIndex, 4);
		send(data, 8);
		return;
	}
	
	if (size<=12)
	{
		fprintf(logFile, "Warning! bad dataFileRecieved packet size (%d) from (%s)!\n", size, Utilities::stringIP(ip));
		fprintf(logFileDownload, "Warning! bad dataFileRecieved packet size (%d) from (%s)!\n", size, Utilities::stringIP(ip));
	}
	int brandwidth=getSint32(data, 4);
	if (this->brandwidth>brandwidth)
	{
		this->brandwidth=brandwidth;
		fprintf(logFileDownload, "new brandwidth=%d\n", brandwidth);
	}
	Uint32 writingIndex=getUint32(data, 8);
	int writingSize=size-12;
	assert(writingSize>0);
	fprintf(logFileDownload, " received data. size=%d, writingIndex=%d (%dk), writingSize=%d\n", size, writingIndex, writingIndex/1024, writingSize);
	totalReceived++;
	
	Uint32 minIndex=(Uint32)-1;
	int mini=-1;
	for (int i=0; i<PACKET_SLOTS; i++)
		if (packetSlot[i].index<minIndex)
		{
			minIndex=packetSlot[i].index;
			mini=i;
			if (minIndex==0)
				break;
		}
	assert(mini!=-1);
	assert(mini<PACKET_SLOTS);
	
	if (minIndex==writingIndex)
		duplicatePacketFile++;
	packetSlot[mini].received=true;
	packetSlot[mini].index=writingIndex;
	packetSlot[mini].brandwidth=brandwidth;
	receivedCounter++;
	
	bool hit=true;
	bool anyHit=false;
	while (hit)
	{
		hit=false;
		for (int i=0; i<PACKET_SLOTS; i++)
			if (packetSlot[i].received)
			{
				Uint32 index=packetSlot[i].index;
				if (unreceivedIndex==index)
				{
					unreceivedIndex=index+1024;
					hit=true;
					anyHit=true;
				}
			}
	}
	if (anyHit)
		fprintf(logFileDownload, "new unreceivedIndex=%d (%dk)\n", unreceivedIndex, unreceivedIndex/1024);
	
	SDL_RWseek(downloadStream, writingIndex, SEEK_SET);
	SDL_RWwrite(downloadStream, data+12, writingSize, 1);
	
	if (startDownloadTimeout>16)
		startDownloadTimeout=16;
	
	if (unreceivedIndex>=endOfFileIndex)
	{
		if (duplicatePacketFile)
			fprintf(logFileDownload, " duplicatePacketFile=%d\n", duplicatePacketFile);
		duplicatePacketFile=0;
		fprintf(logFileDownload, "download's file closed\n");
		SDL_RWclose(downloadStream);
		
		// uncompress
		if (Toolkit::getFileManager()->gunzip(filename + ".gz", filename) == false)
			fprintf(logFileDownload, "error decompressing file %s.gz to %s\n", filename.c_str(), filename.c_str());
		else
			fprintf(logFileDownload, "Map file %s.gz successfully decompressed to %s\n", filename.c_str(), filename.c_str());
		
		downloadStream=NULL;
		
		// We have to tell the server that we finished correctly the download:
		Uint8 data[8];
		memset(data, 0, 8);
		data[0]=NEW_PLAYER_WANTS_FILE;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		addUint32(data, endOfFileIndex, 4);
		send(data, 8);
	}
	
	waitingTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersJoin::checkSumConfirmationRecieved(Uint8 *data, int size, IPaddress ip)
{
	fprintf(logFile, "checkSumConfirmationRecieved\n");

	if (size!=8)
	{
		fprintf(logFile, "Bad size for a checksum confirmation packet recieved!\n");
		waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}

	Uint32 rsc=getUint32(data, 4);
	Uint32 lsc=sessionInfo.checkSum();

	if (rsc!=lsc)
	{
		fprintf(logFile, "Bad checksum confirmation packet value recieved!\n");
		waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
		waitingTimeout=0; //Timeout at one to (not-re) send. (hack but nice)
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}

	fprintf(logFile, "Checksum confirmation packet recieved and valid!\n");

	waitingState=WS_OK;
	waitingTimeout=SHORT_NETWORK_TIMEOUT;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersJoin::unCrossConnectSessionInfo()
{
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
	{
		sessionInfo.players[j].unbind();
		sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
		crossPacketRecieved[j]=0;
	}
}

void MultiplayersJoin::startCrossConnections(void)
{
	fprintf(logFile, "OK, we can start cross connections.\n");

	waitingTimeout=0;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	if (waitingState<WS_CROSS_CONNECTING)
	{
		waitingState=WS_CROSS_CONNECTING;
	}
	MultiplayersCrossConnectable::tryCrossConnections();
	checkAllCrossConnected();
}

void MultiplayersJoin::crossConnectionFirstMessage(Uint8 *data, int size, IPaddress ip)
{
	fprintf(logFile, "crossConnectionFirstMessage\n");

	if (size!=8)
	{
		fprintf(logFile, " Bad size for a crossConnectionFirstMessage packet recieved!\n");
		return;
	}

	Sint32 p=data[4];

	if ((p>=0)&&(p<sessionInfo.numberOfPlayer))
	{
		if (sessionInfo.players[p].waitForNatResolution)
		{
			fprintf(logFile, " player p=%d, with old nat ip(%s), has been solved by the new ip(%s)!", p, Utilities::stringIP(sessionInfo.players[p].ip), Utilities::stringIP(ip));
			
			sessionInfo.players[p].setip(ip); // TODO: This is a security question. Can we avoid to thrust any packet from anyone.
			sessionInfo.players[p].ipFirewallClean=true;
			
			fprintf(logFile, " (this NAT is solved)\n");
			sessionInfo.players[p].waitForNatResolution=false;

			if (waitingTimeout>4 && waitingState==WS_CROSS_CONNECTING_START_CONFIRMED)
				waitingTimeout=2;
		}
		else if (sessionInfo.players[p].ip.host==ip.host)
		{
			if (sessionInfo.players[p].ip.port!=ip.port)
			{
				fprintf(logFile, " changing port for firewall tolerance for player p=(%d) from ip=(%s) to ip=(%s)\n", p, Utilities::stringIP(sessionInfo.players[p].ip), Utilities::stringIP(ip));
				sessionInfo.players[p].setip(ip);
				sessionInfo.players[p].ipFirewallClean=true;
			}
		}
		else if (sessionInfo.players[p].ip.port!=ip.port)
		{
			fprintf(logFile, " warning, a different ip recevied! p=(%d) current ip=(%s), received ip=(%s)\n", p, Utilities::stringIP(sessionInfo.players[p].ip), Utilities::stringIP(ip));
			return;
		}
		
		if (sessionInfo.players[p].channel==-1)
			if (!sessionInfo.players[p].bind(socket))
			{
				fprintf(logFile, " Player %d with ip %s is not bindable!\n", p, Utilities::stringIP(sessionInfo.players[p].ip));
				sessionInfo.players[p].netState=BasePlayer::PNS_BAD;
			}
		
		if (sessionInfo.players[p].channel!=-1)
		{
			if (crossPacketRecieved[p]<1)
				crossPacketRecieved[p]=1;
			fprintf(logFile, " crossConnectionFirstMessage packet recieved (%d) from ip=(%s)\n", p, Utilities::stringIP(ip));

			Uint8 data[8];
			data[0]=PLAYER_CROSS_CONNECTION_SECOND_MESSAGE;
			data[1]=0;
			data[2]=0;
			data[3]=0;
			data[4]=myPlayerNumber;
			data[5]=0;
			data[6]=0;
			data[7]=0;
			sessionInfo.players[p].send(data, 8);
		}
		else
		{
			fprintf(logFile, " player %d is not binded! (crossPacketRecieved[%d]=%d).\n", p, p, crossPacketRecieved[p]);
		}
	}
	else
		fprintf(logFile, " Dangerous crossConnectionFirstMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoin::checkAllCrossConnected()
{
	bool allCrossConnected=true;
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].type==BasePlayer::P_IP)
			if (crossPacketRecieved[j]<2)
			{
				allCrossConnected=false;
				break;
			}
	if (allCrossConnected)
	{
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			if (sessionInfo.players[j].type==BasePlayer::P_IP)
				if ((sessionInfo.players[j].netState!=BasePlayer::PNS_SENDING_FIRST_PACKET)&&(sessionInfo.players[j].netState!=BasePlayer::PNS_HOST))
				{
					allCrossConnected=false;
					break;
				}
		
	}
	if (allCrossConnected)
	{
		if (waitingState<=WS_CROSS_CONNECTING_ACHIEVED)
		{
			waitingState=WS_CROSS_CONNECTING_ACHIEVED;
			if (waitingTimeout>0)
			{
				waitingTimeout-=waitingTimeoutSize;
				assert(waitingTimeoutSize);
				waitingTOTL++;
			}
			fprintf(logFile, "All players are cross connected to me !! (wt=%d) (wts=%d) (wtotl=%d) (ws=%d)\n", waitingTimeout, waitingTimeoutSize, waitingTOTL, waitingState);
		}
	}
}

void MultiplayersJoin::crossConnectionSecondMessage(Uint8 *data, int size, IPaddress ip)
{
	fprintf(logFile, "crossConnectionSecondMessage\n");

	if (size!=8)
	{
		fprintf(logFile, "Bad size for a crossConnectionSecondMessage packet recieved!\n");
		return;
	}

	Sint32 p=data[4];
	if ((p>=0)&&(p<32))
	{
		crossPacketRecieved[p]=2;
		fprintf(logFile, "crossConnectionSecondMessage packet recieved (player=%d)\n", p);
		checkAllCrossConnected();
	}
	else
		fprintf(logFile, "Dangerous crossConnectionSecondMessage packet recieved (player=%d)!\n", p);

}

void MultiplayersJoin::stillCrossConnectingConfirmation(Uint8 *data, int size, IPaddress ip)
{
	if (waitingState==WS_CROSS_CONNECTING_START_CONFIRMED)
	{
		fprintf(logFile, "server(%s) has recieved our stillCrossConnecting state. size=(%d)\n", Utilities::stringIP(ip), size);
		if (waitingState==WS_CROSS_CONNECTING_START_CONFIRMED)
		{
			waitingTimeout=SHORT_NETWORK_TIMEOUT;
			waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		}
		waitingTOTL=DEFAULT_NETWORK_TOTL;
		
		if (shareOnYog)
		{
			if (size<8 || (size-8)%10)
			{
				fprintf(logFile, "Warning: ip(%s) sent us a stillCrossConnectingConfirmation with a bas size! (%d)\n", Utilities::stringIP(ip), size);
				return;
			}
			int n=(size-8)/10;
			int l=8;
			for (int i=0; i<n; i++)
			{
				Uint32 uid=getUint32(data, l);
				l+=4;
				IPaddress ip;
				ip.host=getUint32(data, l);
				l+=4;
				ip.port=getUint16(data, l);
				l+=2;
				char *userName=yog->userNameFromUID(uid);
				if (userName)
				{
					fprintf(logFile, " userName=(%s), proposed to ip=(%s)\n", userName, Utilities::stringIP(ip));
					for (int j=0; j<sessionInfo.numberOfPlayer; j++)
					{
						//fprintf(logFile, "  type[%d]=%d\n", j, sessionInfo.players[j].type);
						if (sessionInfo.players[j].type==BasePlayer::P_IP && strncmp(userName, sessionInfo.players[j].name, 32)==0)
						{
							fprintf(logFile, "   player (%d) (%s) old ip=(%s)\n", j, userName, Utilities::stringIP(sessionInfo.players[j].ip));
							if (((ipFromNAT && !sessionInfo.players[j].ipFromNAT) || sessionInfo.players[j].waitForNatResolution)
								&& !sessionInfo.players[j].ipFirewallClean)
							{
								if (sessionInfo.players[j].sameip(ip))
									fprintf(logFile, "   player (%d) (%s) already switched to ip=(%s)\n", j, userName, Utilities::stringIP(ip));
								else
								{
									fprintf(logFile, "   player (%d) (%s) switched to ip=(%s)\n", j, userName, Utilities::stringIP(ip));
									sessionInfo.players[j].setip(ip);
								}
							}
							else
								fprintf(logFile, "   player (%d) (%s) not switched to ip=(%s)\n", j, userName, Utilities::stringIP(ip));
						}
					}
				}
			}
			assert(l==size);
		}
	}
	else
		fprintf(logFile, "Warning: ip(%s) sent us a stillCrossConnectingConfirmation while in a bad state!.\n", Utilities::stringIP(ip));
}

void MultiplayersJoin::crossConnectionsAchievedConfirmation(IPaddress ip)
{
	if (waitingState>=WS_CROSS_CONNECTING_ACHIEVED)
	{
		fprintf(logFile, "server(%s) has recieved our crossConnection achieved state.\n", Utilities::stringIP(ip));
		waitingState=WS_CROSS_CONNECTING_SERVER_HEARD;
		waitingTimeout=SHORT_NETWORK_TIMEOUT;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL;
	}
	else
		fprintf(logFile, "Warning: ip(%s) sent us a crossConnection achieved state while in a bad state!.\n", Utilities::stringIP(ip));
}

void MultiplayersJoin::serverAskForBeginning(Uint8 *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		fprintf(logFile, "Warning: ip(%s) sent us a bad serverAskForBeginning!.\n", Utilities::stringIP(ip));
		return;
	}

	if (waitingState>=WS_CROSS_CONNECTING_ACHIEVED)
	{
		waitingState=WS_SERVER_START_GAME;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL+1;

		startGameTimeCounter=data[4];

		fprintf(logFile, "Server(%s) asked for game start, timecounter=%d\n", Utilities::stringIP(ip), startGameTimeCounter);
	}
	else
		fprintf(logFile, "Warning: ip(%s) sent us a serverAskForBeginning!.\n", Utilities::stringIP(ip));

}

void MultiplayersJoin::serverBroadcastResponse(Uint8 *data, int size, IPaddress ip)
{
	int v=data[0];
	if (size!=4+64+32)
	{
		fprintf(logFile, "Warning, bad size for a gameHostBroadcastResponse (size=%d, v=%d).\n", size, v);
		return;
	}
	LANHost lanhost;
	memcpy(lanhost.gameName, data+4, 64);
	memcpy(lanhost.serverNickName, data+4+64, 32);
	
	fprintf(logFile, "broadcastState=%d.\n", broadcastState);
	fprintf(logFile, "received broadcast response v=(%d), gameName=(%s), serverNickName=(%s).\n", v, lanhost.gameName, lanhost.serverNickName);
	
	if ((broadcastState==BS_ENABLE_LAN && v==BROADCAST_RESPONSE_LAN)
		|| (broadcastState==BS_ENABLE_YOG && v==BROADCAST_RESPONSE_YOG))
	{
		lanhost.ip=ip;
		lanhost.timeout=2*DEFAULT_NETWORK_TIMEOUT;
		//We ckeck if this host is already in the list:
		bool already=false;
		for (std::list<LANHost>::iterator it=lanHosts.begin(); it!=lanHosts.end(); ++it)
			if (strncmp(lanhost.gameName, it->gameName, 64)==0)
			{
				already=true;
				it->timeout=2*DEFAULT_NETWORK_TIMEOUT;
				break;
			}
		if (!already)
		{
			fprintf(logFile, "added (%s) lanHosts\n", Utilities::stringIP(lanhost.ip));
			lanHosts.push_front(lanhost);
			listHasChanged=true;
		}
	}
	else
		fprintf(logFile, "bad state to remember broadcasting (broadcastState=%d, c=%d)\n", broadcastState, v);
}

void MultiplayersJoin::serverBroadcastStopHosting(Uint8 *data, int size, IPaddress ip)
{
	if (size!=4)
	{
		fprintf(logFile, "Warning, bad size for a serverBroadcastStopHosting (size=%d).\n", size);
		return;
	}
	
	if (broadcastState==BS_ENABLE_LAN)
	{
		for (std::list<LANHost>::iterator it=lanHosts.begin(); it!=lanHosts.end(); ++it)
			if (ip.host==it->ip.host && ip.port==it->ip.port)
			{
				fprintf(logFile, "removed (%s) lanHosts\n", Utilities::stringIP(it->ip));
				lanHosts.erase(it);
				listHasChanged=true;
				break;
			}
	}
	else
		fprintf(logFile, "bad state to remember serverBroadcastStopHosting (broadcastState=%d)\n", broadcastState);
}

void MultiplayersJoin::joinerBroadcastRR(Uint8 *data, int size, IPaddress ip, const char *rrName)
{
	if (size>5+32 || size<5+2)
	{
		fprintf(logFile, "Warning, bad size for a %s (%d) from ip=(%s)\n", rrName, size, Utilities::stringIP(ip));
		return;
	}
	
	char name[32];
	memcpy(name, data+5, size-5);
	name[size-6]=0;
	
	if (waitingState>=WS_WAITING_FOR_CHECKSUM_CONFIRMATION)
	{
		int j=data[4];
		if (strncmp(sessionInfo.players[j].name, name, 32)==0 && !sessionInfo.players[j].sameip(ip))
		{
			sessionInfo.players[j].waitForNatResolution=false;
			sessionInfo.players[j].ipFromNAT=true;
			sessionInfo.players[j].setip(ip);
			fprintf(logFile, "%s, The player (%d) (%s) has a new ip(%s)\n", rrName, j, name, Utilities::stringIP(ip));
		}
	}
	else
		fprintf(logFile, "Warning, %s packet received while in a bad state. (ws=%d).\n", rrName, waitingState);
}

void MultiplayersJoin::joinerBroadcastRequest(Uint8 *data, int size, IPaddress ip)
{
	if (size>5+32 || size<5+2)
	{
		fprintf(logFile, "Warning, bad size for a joinerBroadcastRequest (%d) from ip=(%s)\n", size, Utilities::stringIP(ip));
		return;
	}
	joinerBroadcastRR(data, size, ip, "joinerBroadcastRequest");
	
	int l=Utilities::strmlen(playerName, 32);
	UDPpacket *packet=SDLNet_AllocPacket(5+l);

	assert(packet);

	packet->channel=channel;
	packet->address=ip;
	packet->len=5+l;
	packet->data[0]=BROADCAST_RESPONSE_JOINER;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	packet->data[4]=myPlayerNumber;
	memcpy(packet->data+5, playerName, l);
	
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
		fprintf(logFile, "send suceeded to send joinerBroadcastRequest packet to ip=(%s). name=(%s), l=(%d)\n", Utilities::stringIP(ip), playerName, l);
	else
		fprintf(logFile, "send failed to send joinerBroadcastRequest packet to ip=(%s).\n", Utilities::stringIP(ip));
	SDLNet_FreePacket(packet);
}

void MultiplayersJoin::joinerBroadcastResponse(Uint8 *data, int size, IPaddress ip)
{
	if (size>5+32 || size<5+2)
	{
		fprintf(logFile, "Warning, bad size for a joinerBroadcastResponse (%d) from ip=(%s)\n", size, Utilities::stringIP(ip));
		return;
	}
	joinerBroadcastRR(data, size, ip, "joinerBroadcastResponse");
}

void MultiplayersJoin::treatData(Uint8 *data, int size, IPaddress ip)
{
	if (size<=0)
	{
		fprintf(logFile, "Bad zero size packet recieved from ip=(%s)\n", Utilities::stringIP(ip));
		return;
	}
	if (data[0]!=FULL_FILE_DATA)
		fprintf(logFile, "\nMultiplayersJoin::treatData (%d)\n", data[0]);
	if ((data[2]!=0)||(data[3]!=0))
	{
		fprintf(logFile, "Bad packet recieved (%d,%d,%d,%d), size=(%d), ip=(%s)\n", data[0], data[1], data[2], data[3], size, Utilities::stringIP(ip));
		return;
	}
	switch (data[0])
	{
		case FULL_FILE_DATA:
			dataFileRecieved(data, size, ip);
		break;
	
		case SERVER_FIREWALL_EXPOSED:
			fprintf(logFile, "water received from ip(%s)!\n", Utilities::stringIP(ip));
			if (waitingState>=WS_WAITING_FOR_PRESENCE)
			{
				waitingTimeout=0;
				waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
				waitingTOTL=DEFAULT_NETWORK_TOTL;
				yog->connectedToGameHost();
			}
		break;
		
		case SERVER_PRESENCE :
			dataPresenceRecieved(data, size, ip);
		break;
		
		case DATA_SESSION_INFO :
			dataSessionInfoRecieved(data, size, ip);
		break;

		case SERVER_SEND_CHECKSUM_RECEPTION :
			checkSumConfirmationRecieved(data, size, ip);
		break;

		case SERVER_KICKED_YOU :
			if (waitingState<WS_SERVER_START_GAME)
			{
				//TODO : show an explanation pannel for the joiner.
				fprintf(logFile, "Server kicked you.\n");
				myPlayerNumber=-1;
				waitingState=WS_TYPING_SERVER_NAME;
				kicked=true;
			}
		break;
		case SERVER_QUIT_NEW_GAME :
			if (waitingState<WS_SERVER_START_GAME)
			{
				//TODO : show an explanation pannel for the joiner.
				fprintf(logFile, "Server has quit.\n");
				myPlayerNumber=-1;
				waitingState=WS_TYPING_SERVER_NAME;
			}
		break;

		case PLAYERS_CAN_START_CROSS_CONNECTIONS :
			startCrossConnections();
		break;

		case PLAYER_CROSS_CONNECTION_FIRST_MESSAGE :
			crossConnectionFirstMessage(data, size, ip);
		break;

		case PLAYER_CROSS_CONNECTION_SECOND_MESSAGE :
			crossConnectionSecondMessage(data, size, ip);
		break;

		case SERVER_CONFIRM_CLIENT_STILL_CROSS_CONNECTING :
			stillCrossConnectingConfirmation(data, size, ip);
		break;

		case SERVER_HEARD_CROSS_CONNECTION_CONFIRMATION :
			crossConnectionsAchievedConfirmation(ip);
		break;

		case SERVER_ASK_FOR_GAME_BEGINNING :
			serverAskForBeginning(data, size, ip);
		break;
		
		case ORDER_TEXT_MESSAGE_CONFIRMATION:
			confirmedMessage(data, size, ip);
		break;
		
		case ORDER_TEXT_MESSAGE:
			receivedMessage(data, size, ip);
		break;
		
		case BROADCAST_REQUEST:
			joinerBroadcastRequest(data, size, ip);
		break;
		
		case BROADCAST_LAN_GAME_HOSTING:
			if (!shareOnYog)
			{
				if (data[1])
					sendBroadcastRequest(ip);
				else
					serverBroadcastStopHosting(data, size, ip);
			}
		break;
		
		case BROADCAST_RESPONSE_LAN:
		case BROADCAST_RESPONSE_YOG:
			serverBroadcastResponse(data, size, ip);
		break;

		case BROADCAST_RESPONSE_JOINER:
			joinerBroadcastResponse(data, size, ip);
		break;
		
		default:
			fprintf(logFile, "Unknow kind of packet(%d) recieved from ip(%s)!\n", data[0], Utilities::stringIP(ip));
	}
}

void MultiplayersJoin::onTimer(Uint32 tick)
{
	if (shareOnYog)
		yog->step(); // YOG cares about firewall and NATipFromNAT
	
	sendingTime();
	
	if ((broadcastState==BS_ENABLE_LAN) || (broadcastState==BS_ENABLE_YOG))
	{
		std::list<LANHost>::iterator it;
		for (it=lanHosts.begin(); it!=lanHosts.end(); ++it)
			it->timeout--;
	}
	for (std::list<LANHost>::iterator it=lanHosts.begin(); it!=lanHosts.end(); ++it)
		if (it->timeout<0)
		{
			fprintf(logFile, "removed (%s) lanHosts\n", Utilities::stringIP(it->ip));
			std::list<LANHost>::iterator ittemp=it;
			it=lanHosts.erase(ittemp);
			listHasChanged=true;
		}
	if (broadcastState==BS_ENABLE_YOG && waitingState==WS_WAITING_FOR_PRESENCE)
		for (std::list<LANHost>::iterator it=lanHosts.begin(); it!=lanHosts.end(); ++it)
			if (strncmp(it->serverNickName, serverNickName, 32)==0)
			{
				if (serverIP.host!=it->ip.host)
				{
					assert(socket);
					if ((socket)&&(channel!=-1))
					{
						fprintf(logFile, "MultiplayersJoin:NAT:Unbinding socket. channel=%d\n", channel);
						// In improbable case that the target ip really hosted a game,
						// we send him a quit game message
						send(CLIENT_QUIT_NEW_GAME);
						SDLNet_UDP_Unbind(socket, channel);
						channel=-1;
					}

					serverIP=it->ip;
					if (serverIP.port!=SDL_SwapBE16(GAME_SERVER_PORT))
						fprintf(logFile, "Warning, the server port is not the standard port! Should not happen! (%d)\n", serverIP.port);
					fprintf(logFile, "Found a local game with same serverNickName=(%s).\n", serverNickName);
					const char *s=SDLNet_ResolveIP(&serverIP);
					if (s)
						strncpy(serverName, s, 256);
					else
						Utilities::stringIP(serverName, 256, serverIP.host);
					serverName[255]=0;
					
					fprintf(logFile, "Trying NAT. serverIP.host=(%s)(%s)\n", Utilities::stringIP(serverIP), serverName);
					
					assert(channel==-1);
					channel=SDLNet_UDP_Bind(socket, -1, &serverIP);
					if (channel != -1)
					{
						fprintf(logFile, "MultiplayersJoin:NAT:suceeded to bind socket (socket=%p) (channel=%d)\n", socket, channel);
						fprintf(logFile, "MultiplayersJoin:NAT:serverIP=%s\n", Utilities::stringIP(serverIP));
						
						waitingState=WS_WAITING_FOR_PRESENCE;
						waitingTimeout=0;
						waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
					}
					else
					{
						fprintf(logFile, "MultiplayersJoin:NAT:failed to bind socket\n");
						waitingState=WS_TYPING_SERVER_NAME;
					}
				}
				if (!ipFromNAT)
				{
					ipFromNAT=true;
					fprintf(logFile, "We now know that the game host is on the same LAN.\n");
					// Now, the "ipFromNAT" players are no more "waitForNatResolution".
					if (waitingState>=WS_WAITING_FOR_CHECKSUM_CONFIRMATION)
						for (int j=0; j<sessionInfo.numberOfPlayer; j++)
							if (sessionInfo.players[j].type==BasePlayer::P_IP)
								if (sessionInfo.players[j].ipFromNAT)
									sessionInfo.players[j].waitForNatResolution=false;
				}
			}
			else
			{
				fprintf(logFile, "MultiplayersJoin:NAT: (%s)!=(%s)\n", it->serverNickName, serverNickName);
			}
	
	if (socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_PACKET_SIZE);
		assert(packet);

		while (SDLNet_UDP_Recv(socket, packet)==1)
			treatData(packet->data, packet->len, packet->address);
		
		SDLNet_FreePacket(packet);
	}

	if (waitingState==WS_SERVER_START_GAME)
		if (--startGameTimeCounter<0)
		{
			fprintf(logFile, "Lets quit this screen and start game!\n");
		}
}

char *MultiplayersJoin::getStatusString()
{
	const char *s="internal error";
	switch (waitingState)
	{
		case WS_BAD:
			s=Toolkit::getStringTable()->getString("[bad error in connection system]");
		break;
		case WS_TYPING_SERVER_NAME:
			s=Toolkit::getStringTable()->getString("[not connected]");
		break;
		case WS_WAITING_FOR_PRESENCE:
			s=Toolkit::getStringTable()->getString("[presence request sent]");
		break;
		case WS_WAITING_FOR_SESSION_INFO:
			s=Toolkit::getStringTable()->getString("[session request sent]");
		break;
		case WS_WAITING_FOR_CHECKSUM_CONFIRMATION:
			s=Toolkit::getStringTable()->getString("[checksum sent]");
		break;
		case WS_OK:
			s=Toolkit::getStringTable()->getString("[connected to server]");
		break;
		case WS_CROSS_CONNECTING:
		case WS_CROSS_CONNECTING_START_CONFIRMED:
			s=Toolkit::getStringTable()->getString("[connecting to all players]");
		break;
		case WS_CROSS_CONNECTING_ACHIEVED:
		case WS_CROSS_CONNECTING_SERVER_HEARD:
		case WS_SERVER_START_GAME:
			s=Toolkit::getStringTable()->getString("[connected to all players]");
		break;
		default:
			assert(false);
		break;
	}
	size_t l=strlen(s)+1;
	char *t=new char[l];
	strncpy(t, s, l);
	return t;
}

void MultiplayersJoin::sendingTime()
{
	MultiplayersCrossConnectable::sendingTime();
	
	if (waitingState>WS_WAITING_FOR_SESSION_INFO)
	{
		if (broadcastState==BS_ENABLE_LAN)
			broadcastState=BS_JOINED_LAN;
		else if (broadcastState==BS_ENABLE_YOG)
			broadcastState=BS_JOINED_YOG;
	}
	else
	{
		if (broadcastState==BS_JOINED_LAN)
			broadcastState=BS_ENABLE_LAN;
		else if (broadcastState==BS_JOINED_YOG)
			broadcastState=BS_ENABLE_YOG;
	}
	
	if (socket && (--broadcastTimeout<0))
	{
		if ((broadcastState==BS_ENABLE_LAN || broadcastState==BS_ENABLE_YOG) 
			&& (waitingState>=WS_WAITING_FOR_PRESENCE || !shareOnYog))
			sendBroadcastRequest(GAME_SERVER_PORT);
		
		bool needLocalBroadcasting=false;
		
		if ((broadcastState==BS_ENABLE_YOG || broadcastState==BS_JOINED_YOG)
			&& (waitingState>=WS_WAITING_FOR_CHECKSUM_CONFIRMATION))
		{
			sendBroadcastRequest(GAME_JOINER_PORT_1);
			SDL_Delay(10);
			sendBroadcastRequest(GAME_JOINER_PORT_2);
			SDL_Delay(10);
			sendBroadcastRequest(GAME_JOINER_PORT_3);
		}
		
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			if (sessionInfo.players[j].type==BasePlayer::P_IP)
				if (j!=myPlayerNumber && sessionInfo.players[j].localhostip() && !sessionInfo.players[j].waitForNatResolution)
					needLocalBroadcasting=true;
		
		if (needLocalBroadcasting)
		{
			// We ask the host's joiner for his ip:
			IPaddress ip;
			ip.host=serverIP.host;
			ip.port=SDL_SwapBE16(GAME_JOINER_PORT_1);
			sendBroadcastRequest(ip);
			SDL_Delay(10);
			ip.port=SDL_SwapBE16(GAME_JOINER_PORT_2);
			sendBroadcastRequest(ip);
			SDL_Delay(10);
			ip.port=SDL_SwapBE16(GAME_JOINER_PORT_3);
			sendBroadcastRequest(ip);
		}
	}
	
	if ((waitingState!=WS_TYPING_SERVER_NAME) && downloadStream)
	{
		startDownloadTimeout--;
		if (startDownloadTimeout<0 || receivedCounter>=brandwidth)
		{
			//Then we have to send a feed-back.
			fprintf(logFileDownload, "startDownloadTimeout=%d, receivedCounter=%d, brandwidth=%d, unreceivedIndex=%d (%dk)\n", startDownloadTimeout, receivedCounter, brandwidth, unreceivedIndex, unreceivedIndex/1024);
			
			//We have to compute the unfragmented segments:
			Uint32 receivedBegin[8];
			Uint32 receivedEnd[8];
			memset(receivedBegin, 0, 8*sizeof(Uint32));
			memset(receivedEnd, 0, 8*sizeof(Uint32));
			Uint32 alreadyCountedIndex=unreceivedIndex;
			int ixend=0;
			fprintf(logFileDownload, " minIndex=(");
			for (int ix=0; ix<8; ix++)
			{
				Uint32 minIndex=(Uint32)-1;
				int mini=-1;
				for (int i=0; i<PACKET_SLOTS; i++)
					if (packetSlot[i].received)
					{
						Uint32 index=packetSlot[i].index;
						if (index>alreadyCountedIndex && index<minIndex)
						{
							minIndex=index;
							mini=i;
						}
					}
				if (mini==-1)
				{
					// No more fragmentation!
					break;
				}
				else
				{
					receivedBegin[ix]=minIndex;
					bool hit=true;
					while(hit)
					{
						hit=false;
						for (int i=0; i<PACKET_SLOTS; i++)
							if (packetSlot[i].received)
							{
								Uint32 index=packetSlot[i].index;
								if (index>alreadyCountedIndex && index==minIndex)
								{
									minIndex+=1024;
									hit=true;
								}
							}
					}
					assert(minIndex-1024>alreadyCountedIndex);
					receivedEnd[ix]=minIndex-1024;
					alreadyCountedIndex=minIndex;
					ixend=ix;
				}
			}
			fprintf(logFileDownload, ")\n");
			
			// Let's create the packet:
			int size=8+8*ixend;

			VARARRAY(Uint8,data,size);

			memset(data, 0, size);
			data[0]=NEW_PLAYER_WANTS_FILE;
			data[1]=(endOfFileIndex==0xFFFFFFFF);
			data[2]=0;
			data[3]=0;
			addUint32(data, unreceivedIndex, 4);
			fprintf(logFileDownload, " ixend=%d\n", ixend);
			fprintf(logFileDownload, " received=(");
			for (int ix=0; ix<ixend; ix++)
			{
				addUint32(data, receivedBegin[ix], 8+ix*8);
				addUint32(data, receivedEnd[ix], 12+ix*8);
				fprintf(logFileDownload, "(%d to %d)+", receivedBegin[ix]/1024, receivedEnd[ix]/1024);
			}
			fprintf(logFileDownload, ")\n");
			bool success=send(data, size);
			assert(success);
			
			receivedCounter=0;
			// We compute the new brandwidth, which is the most recent brandwidth:
			int maxi=-1;
			Uint32 maxIndex=0;
			for (int i=0; i<PACKET_SLOTS; i++)
				if (packetSlot[i].received)
				{
					Uint32 index=packetSlot[i].index;
					if (index>maxIndex)
					{
						maxIndex=index;
						maxi=i;
					}
				}
			if (maxi!=-1)
				brandwidth=packetSlot[maxi].brandwidth;
			startDownloadTimeout=16;
		}
	}

	if ((waitingState!=WS_TYPING_SERVER_NAME)&&(--waitingTimeout<0))
	{
		// OK, we are connecting, but a timeout occured.
		
		if (--waitingTOTL<0)
		{
			fprintf(logFile, "Last TOTL spent, server has left\n");
			waitingState=WS_TYPING_SERVER_NAME;
			if (shareOnYog)
				yog->unjoinGame(false, Toolkit::getStringTable()->getString("[Can't join game, timeout]"));
			if (broadcastState==BS_ENABLE_YOG)
				broadcastState=BS_DISABLE_YOG;
			fprintf(logFile, "disabling NAT detection too. bs=(%d)\n", broadcastState);
		}
		else
			fprintf(logFile, "TOTL %d\n", waitingTOTL);
			
		if (waitingState>=WS_CROSS_CONNECTING_START_CONFIRMED && waitingState<WS_SERVER_START_GAME)
			MultiplayersCrossConnectable::tryCrossConnections();
			
		switch (waitingState)
		{
		case WS_TYPING_SERVER_NAME:
		{
			// Nothing to send to none.
		}
		break;

		case WS_WAITING_FOR_PRESENCE:
		{
			if (!sendPresenceRequest())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_WAITING_FOR_SESSION_INFO:
		{
			if (!sendSessionInfoRequest())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_WAITING_FOR_CHECKSUM_CONFIRMATION:
		{
			if (!sendSessionInfoConfirmation())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_OK:
		{
			fprintf(logFile, "Im ok, but I want the server to know I m still here!\n");
			if (!sendSessionInfoConfirmation())
				waitingState=WS_TYPING_SERVER_NAME;
		}
		break;

		case WS_CROSS_CONNECTING:
		{
			fprintf(logFile, "We tell the server that we heard about cross connection start.\n");
			if (!send(PLAYERS_CONFIRM_START_CROSS_CONNECTIONS))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
			else
			{
				// The server has to reply if he wants another confimation.
				// But for us, it's all right.
				waitingState=WS_CROSS_CONNECTING_START_CONFIRMED;
			}
		}
		break;

		case WS_CROSS_CONNECTING_START_CONFIRMED:
		{
			fprintf(logFile, "We try cross connecting aggain:\n");
			// we have to inform the server that we are still alive:
			if (!send(PLAYERS_STILL_CROSS_CONNECTING))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
			checkAllCrossConnected();
		}
		break;

		case WS_CROSS_CONNECTING_ACHIEVED:
		{
			fprintf(logFile, "We are cross connected.\n");
			if (!send(PLAYERS_CROSS_CONNECTIONS_ACHIEVED))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		case WS_CROSS_CONNECTING_SERVER_HEARD:
		{
			fprintf(logFile, "Im fully cross connected and server confirmed!\n");
			if (!send(PLAYERS_CROSS_CONNECTIONS_ACHIEVED))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		case WS_SERVER_START_GAME :
		{
			fprintf(logFile, "Starting game within %d seconds.\n", (int)(startGameTimeCounter/20));
			if (!send(PLAYER_CONFIRM_GAME_BEGINNING, startGameTimeCounter))
			{
				send(CLIENT_QUIT_NEW_GAME);
				waitingState=WS_TYPING_SERVER_NAME;
			}
		}
		break;

		default:
			fprintf(logFile, "Im in a bad state %d!\n", waitingState);

		}

		waitingTimeout=waitingTimeoutSize;
		assert(waitingTimeoutSize);
	}
}

bool MultiplayersJoin::sendPresenceRequest()
{
	assert(BasePlayer::MAX_NAME_LENGTH==32);
	UDPpacket *packet=SDLNet_AllocPacket(44);

	assert(packet);

	packet->channel=-1;
	packet->address=serverIP;
	packet->len=44;
	packet->data[0]=NEW_PLAYER_WANTS_PRESENCE;
	packet->data[1]=NET_PROTOCOL_VERSION;
	packet->data[2]=0;
	packet->data[3]=0;
	addUint32(packet->data, globalContainer->getConfigCheckSum(), 4);
	addSint32(packet->data, (Sint32)ipFromNAT, 8);
	strncpy((char *)(packet->data+12), playerName, 32); //TODO: use uid if over YOG.

	int nbsent=SDLNet_UDP_Send(socket, -1, packet);
	if (nbsent==1)
		fprintf(logFile, "succeded to send presence request packet to host=(%s)(%s) channel=%d ipFromNAT=%d\n", Utilities::stringIP(serverIP), serverName, channel, ipFromNAT);
	else
	{
		fprintf(logFile, "failed to send presence request packet to host=(%s)(%s) channel=%d ipFromNAT=%d, nbsent=%d\n", Utilities::stringIP(serverIP), serverName, channel, ipFromNAT, nbsent);
		waitingState=WS_TYPING_SERVER_NAME;
		SDLNet_FreePacket(packet);
		return false;
	}

	SDLNet_FreePacket(packet);

	waitingState=WS_WAITING_FOR_PRESENCE;
	waitingTimeout=SHORT_NETWORK_TIMEOUT;
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	return true;
}

bool MultiplayersJoin::sendSessionInfoRequest()
{
	UDPpacket *packet=SDLNet_AllocPacket(10);

	assert(packet);

	packet->channel=-1;
	packet->address=serverIP;
	packet->len=10;
	packet->data[0]=NEW_PLAYER_WANTS_SESSION_INFO;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;

	Uint32 netHost=SDL_SwapBE32(serverIP.host);
	Uint16 netPort=SDL_SwapBE16(serverIP.port);
	fprintf(logFile, "sendSessionInfoRequest() serverIP=%s\n", Utilities::stringIP(serverIP));
	addUint32(packet->data, netHost, 4);
	addUint16(packet->data, netPort, 8);

	if (SDLNet_UDP_Send(socket, -1, packet)==1)
	{
		fprintf(logFile, "succeded to send session request packet\n");
	}
	else
	{
		fprintf(logFile, "failed to send session request packet\n");
		waitingState=WS_TYPING_SERVER_NAME;
		SDLNet_FreePacket(packet);
		return false;
	}

	SDLNet_FreePacket(packet);

	waitingState=WS_WAITING_FOR_SESSION_INFO;
	waitingTimeout=LONG_NETWORK_TIMEOUT;
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	return true;
}

bool MultiplayersJoin::sendSessionInfoConfirmation()
{

	UDPpacket *packet=SDLNet_AllocPacket(8);

	assert(packet);

	packet->channel=-1;
	packet->address=serverIP;
	packet->len=8;
	packet->data[0]=NEW_PLAYER_SEND_CHECKSUM_CONFIRMATION;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	Uint32 cs=sessionInfo.checkSum();
	fprintf(logFile, "cs=%x.\n", cs);
	addUint32(packet->data, cs, 4);

	if (SDLNet_UDP_Send(socket, -1, packet)==1)
		fprintf(logFile, "suceeded to send confirmation packet\n");
	else
	{
		fprintf(logFile, "failed to send confirmation packet\n");
		SDLNet_FreePacket(packet);
		return false;
	}

	SDLNet_FreePacket(packet);

	waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
	waitingTimeout=LONG_NETWORK_TIMEOUT;
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	
	return true;
}

bool MultiplayersJoin::send(Uint8 *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);

	assert(packet);

	packet->channel=-1;
	packet->address=serverIP;
	packet->len=size;
	memcpy(packet->data, data, size);
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
	{
		fprintf(logFile, "send suceeded to send packet\n");
	}
	else
	{
		fprintf(logFile, "send failed to send packet (channel=%d)\n", channel);
		SDLNet_FreePacket(packet);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}

bool MultiplayersJoin::send(const int v)
{
	UDPpacket *packet=SDLNet_AllocPacket(4);

	assert(packet);

	packet->channel=-1;
	packet->address=serverIP;
	packet->len=4;
	packet->data[0]=v;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
		fprintf(logFile, "send suceeded to send packet v=%d\n", v);
	else
	{
		fprintf(logFile, "send failed to send packet (v=%d) (channel=%d)\n", v, channel);
		SDLNet_FreePacket(packet);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}
bool MultiplayersJoin::send(const int u, const int v)
{
	UDPpacket *packet=SDLNet_AllocPacket(8);

	assert(packet);

	packet->channel=-1;
	packet->address=serverIP;
	packet->len=8;
	packet->data[0]=u;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	packet->data[4]=v;
	packet->data[5]=0;
	packet->data[6]=0;
	packet->data[7]=0;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
		fprintf(logFile, "send suceeded to send packet v=%d\n", v);
	else
	{
		fprintf(logFile, "send failed to send packet (v=%d) (channel=%d)\n", v, channel);
		SDLNet_FreePacket(packet);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}

void MultiplayersJoin::sendBroadcastRequest(IPaddress ip)
{
	int l=Utilities::strmlen(playerName, 32);
	UDPpacket *packet=SDLNet_AllocPacket(5+l);

	assert(packet);

	packet->channel=-1;
	packet->address=ip;
	packet->len=5+l;
	packet->data[0]=BROADCAST_REQUEST;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	packet->data[4]=myPlayerNumber;
	memcpy(packet->data+5, playerName, l);
	
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
	{
		fprintf(logFile, "Succeded to send a BROADCAST_REQUEST packet to (%s)\n", Utilities::stringIP(packet->address));
		broadcastTimeout=DEFAULT_NETWORK_TIMEOUT;
	}
	else
	{
		broadcastState=BS_BAD;
		fprintf(logFile, "Failed to send a BROADCAST_REQUEST packet to (%s)\n", Utilities::stringIP(packet->address));
	}
	SDLNet_FreePacket(packet);
}

void MultiplayersJoin::sendBroadcastRequest(Uint16 port)
{
	IPaddress ip;
	ip.host=INADDR_BROADCAST;
	ip.port=SDL_SwapBE16(port);
	sendBroadcastRequest(ip);
}

bool MultiplayersJoin::tryConnection(bool isHostToo)
{
	quitThisGame();
	fprintf(logFile, "\ntryConnection(%d)\n", isHostToo);

	if (!socket)
	{
		fprintf(logFile, "socket  is NULL!\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}
	
	if (!shareOnYog)
	{
		if (SDLNet_ResolveHost(&serverIP, serverName, GAME_SERVER_PORT)==0)
		{
			fprintf(logFile, "serverName=(%s)\n", serverName);
			fprintf(logFile, "found serverIP=%s\n", Utilities::stringIP(serverIP));
		}
		else
		{
			fprintf(logFile, "failed to find adresse (serverName=%s)\n", serverName);
			waitingState=WS_TYPING_SERVER_NAME;
			return false;
		}
	}

	assert(channel==-1);
	channel=SDLNet_UDP_Bind(socket, -1, &serverIP);

	if (channel != -1)
	{
		fprintf(logFile, "suceeded to bind socket (socket=%p) (channel=%d)\n", socket, channel);
		fprintf(logFile, "serverIP=(%s)\n", Utilities::stringIP(serverIP));
	}
	else
	{
		fprintf(logFile, "failed to bind socket\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}
	
	if (shareOnYog)
	{
		yog->setJoinGameSocket(socket);
		waitingTOTL=DEFAULT_NETWORK_TOTL+1; //because the first try is lost if there is a firewall or NAT.
		if (!localPort)
			localPort=findLocalPort(socket);
	}
	else
		waitingTOTL=DEFAULT_NETWORK_TOTL-1;
	
	IPaddress *localAddress=SDLNet_UDP_GetPeerAddress(socket, -1);
	fprintf(logFile, "Socket opened at port(%s)\n", Utilities::stringIP(*localAddress));
	
	if (isHostToo)
		ipFromNAT=true;
	if (shareOnYog)
	{
		if (broadcastState==BS_DISABLE_YOG)
			broadcastState=BS_ENABLE_YOG;
		fprintf(logFile, "enabling NAT detection too. bs=(%d)\n", broadcastState);
	}
	else
		fprintf(logFile, "not enabling NAT detection. shareOnYog=%d, ipFromNAT=%d, broadcastState=%d\n", shareOnYog, ipFromNAT, broadcastState);
	
	waitingState=WS_WAITING_FOR_PRESENCE;
	waitingTimeout=3; // We have to wait for a broadcast respons *before* we send our first presenseRequest.
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	return true;
}

void MultiplayersJoin::quitThisGame() 
{
	fprintf(logFile, "\nquitThisGame() (this=%p)(socket=%p).\n", this, socket);
	unCrossConnectSessionInfo();
	if (shareOnYog)
		yog->unjoinGame(false);
	
	if (waitingState>WS_TYPING_SERVER_NAME)
		send(CLIENT_QUIT_NEW_GAME);
	waitingState=WS_TYPING_SERVER_NAME;
	if (broadcastState==BS_ENABLE_YOG)
		broadcastState=BS_DISABLE_YOG;
		
	ipFromNAT=false;
	if ((socket)&&(channel!=-1))
	{
		fprintf(logFile, "Unbinding socket. channel=%d\n", channel);
		SDLNet_UDP_Unbind(socket, channel);
		channel=-1;
	}
	fprintf(logFile, "disabling NAT detection too. bs=(%d)\n", broadcastState);
}

bool MultiplayersJoin::tryConnection(YOG::GameInfo *yogGameInfo)
{
	assert(yogGameInfo);
	if (this->yogGameInfo)
		delete this->yogGameInfo;
	this->yogGameInfo=yogGameInfo;
	
	const char *s=SDLNet_ResolveIP(&yogGameInfo->hostip);
	if (s)
		strncpy(serverName, s, 256);
	else
		Utilities::stringIP(serverName, 256, yogGameInfo->hostip.host);
	serverName[255]=0;
	
	serverIP=yogGameInfo->hostip;
	if (verbose)
		printf("tryConnection::serverName=%s\n", serverName);
	//TODO: is the serverName string usefull ? If it is, the port needs to be passed too !
	
	strncpy(playerName, globalContainer->getUsername().c_str(), 32);
	playerName[31]=0;
	strncpy(serverNickName, yogGameInfo->userName, 32);
	serverNickName[31]=0;
	return tryConnection(false);
}

Uint16 MultiplayersJoin::findLocalPort(UDPsocket socket)
{
	Uint16 localPort=0;
	for (int tempPort=GAME_JOINER_FIND_LOCAL_PORT_BASE; tempPort<GAME_JOINER_FIND_LOCAL_PORT_BASE+10010; tempPort+=1001) // we try 10 ports, strategically separated
	{
		// First, we create a temporaty local server (tempServer):
		UDPsocket tempSocket;
		tempSocket=SDLNet_UDP_Open(tempPort);
		if (tempSocket)
			fprintf(logFile, "findLocalPort:: Socket opened at port %d.\n", tempPort);
		else
		{
			fprintf(logFile, "findLocalPort:: failed to open a socket.\n");
			continue; // We try to open another port
		}

		// Second, we send a packet to this tempServer with loopback adress:
		{
			UDPpacket *packet=SDLNet_AllocPacket(4);

			assert(packet);

			packet->channel=-1;
			IPaddress localAdress;
			localAdress.host=SDL_SwapBE32(0x7F000001);
			localAdress.port=SDL_SwapBE16(tempPort);
			packet->address=localAdress;
			packet->len=4;
			memcpy(packet->data, "PABO", 4);
			if (SDLNet_UDP_Send(socket, -1, packet)==1)
			{
				fprintf(logFile, "findLocalPort:: suceeded to send packet\n");
			}
			else
			{
				fprintf(logFile, "findLocalPort:: failed to send packet\n");
				SDLNet_FreePacket(packet);
				SDLNet_UDP_Close(tempSocket);
				continue; // We try with another port
			}
			SDLNet_FreePacket(packet);
		}
		
		// Three, we wait for this packet
		{
			assert(tempSocket);
			UDPpacket *packet=NULL;
			packet=SDLNet_AllocPacket(4);
			assert(packet);
			int count=0;
			while (SDLNet_UDP_Recv(tempSocket, packet)!=1)
			{
				if (count++>=3)
					break;
				SDL_Delay(100);
				fprintf(logFile, "findLocalPort::delay 100ms to wait for the loop-back packet\n");
			}
			if (count>=3)
			{
				fprintf(logFile, "findLocalPort::no Packet received !!!!\n");
				SDLNet_FreePacket(packet);
				SDLNet_UDP_Close(tempSocket);
				continue; // We try with another port
			}
			if (memcmp(packet->data, "PABO", 4)==0)
			{
				fprintf(logFile, "findLocalPort::Packet received.\n");
				fprintf(logFile, "findLocalPort::packet->address=%s\n", Utilities::stringIP(packet->address));
				localPort=packet->address.port;
			}
			
			SDLNet_FreePacket(packet);
		}
		
		// Four, we close the tempServer
		SDLNet_UDP_Close(tempSocket);
		
		if (localPort)
			break;
	}
	
	return localPort;
}

bool MultiplayersJoin::isFileMapDownload(double &progress)
{
	if (endOfFileIndex>0)
		progress=(double)unreceivedIndex/(double)endOfFileIndex;
	if (downloadStream)
		return true;
	else
		return false;
}
