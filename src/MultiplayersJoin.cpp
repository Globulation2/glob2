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

#include "MultiplayersJoin.h"
#include "GlobalContainer.h"
#include "GAG.h"
#include "NetDefine.h"
#include "Marshaling.h"
#include "Utilities.h"

MultiplayersJoin::MultiplayersJoin(bool shareOnYOG)
:MultiplayersCrossConnectable()
{
	yogGameInfo=NULL;
	downloadStream=NULL;
	logFile=globalContainer->logFileManager.getFile("MultiplayersJoin.log");
	assert(logFile);
	duplicatePacketFile=0;
	init(shareOnYOG);
}

MultiplayersJoin::~MultiplayersJoin()
{
	if (destroyNet)
	{
		if (socket)
		{
			if (channel!=-1)
			{
				send(CLIENT_QUIT_NEW_GAME);
				SDLNet_UDP_Unbind(socket, channel);
				fprintf(logFile, "~MultiplayersJoin::Socket unbinded.\n");
			}
			SDLNet_UDP_Close(socket);
			socket=NULL;
			fprintf(logFile, "Socket closed.\n");
		}
	}
	if (duplicatePacketFile)
		fprintf(logFile, "MultiplayersJoin:: duplicatePacketFile=%d\n", duplicatePacketFile);
	if (downloadStream)
	{
		fprintf(logFile, "MultiplayersJoin:: download not finished.\n");
		//TODO: delete/remove the incomplete file !
		SDL_RWclose(downloadStream);
		downloadStream=NULL;
	}
}

void MultiplayersJoin::init(bool shareOnYOG)
{
	waitingState=WS_TYPING_SERVER_NAME;
	waitingTimeout=0;
	waitingTimeoutSize=0;
	waitingTOTL=0;

	startGameTimeCounter=0;

	serverName=serverNameMemory;
	serverName[0]=0;
	playerName[0]=0;
	
	serverIP.host=0;
	serverIP.port=0;
	ipFromNAT=false;
	
	kicked=false;
	
	if (lan.enable(SERVER_PORT))
	{
		if (shareOnYOG)
			broadcastState=BS_DISABLE_YOG;
		else
			broadcastState=BS_ENABLE_LAN;
	}
	else
		broadcastState=BS_BAD;
	
	broadcastTimeout=0;
	listHasChanged=false;
	
	this->shareOnYOG=shareOnYOG;
	
	if (yogGameInfo)
	{
		delete yogGameInfo;
		yogGameInfo=NULL;
	}
	
	if (downloadStream)
	{
		SDL_RWclose(downloadStream);
		downloadStream=NULL;
	}
	unreceivedIndex=0;
	endOfFileIndex=0xFFFFFFFF;
	for (int i=0; i<NET_WINDOW_SIZE; i++)
	{
		netWindow[i].index=0;
		netWindow[i].received=false;
		netWindow[i].packetSize=0; //set 512 in release
	}
	startDownloadTimeout=SHORT_NETWORK_TIMEOUT;
	
	logFile=globalContainer->logFileManager.getFile("MultiplayersJoin.log");
	assert(logFile);
	
	fprintf(logFile, "new MultiplayersJoin\n");
	
	if (duplicatePacketFile)
		fprintf(logFile, "duplicatePacketFile=%d\n", duplicatePacketFile);
	duplicatePacketFile=0;
}

void MultiplayersJoin::dataPresenceRecieved(char *data, int size, IPaddress ip)
{
	if (size!=4)
	{
		fprintf(logFile, "Bad size for a Presence packet recieved!\n");
		waitingState=WS_WAITING_FOR_PRESENCE;
		waitingTimeout=0;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		return;
	}
	
	fprintf(logFile, "dataPresenceRecieved\n");
	
	waitingState=WS_WAITING_FOR_SESSION_INFO;
	waitingTimeout=0; //Timeout at one to (not-re) send. (hack but nice)
	waitingTimeoutSize=LONG_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
}

void MultiplayersJoin::dataSessionInfoRecieved(char *data, int size, IPaddress ip)
{
	int pn=getSint32(data, 4);

	if ((pn<0)||(pn>=32))
	{
		fprintf(logFile, "Warning: bad dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);
		waitingTimeout=0;
		return;
	}

	myPlayerNumber=pn;
	fprintf(logFile, "dataSessionInfoRecieved myPlayerNumber=%d\n", myPlayerNumber);

	unCrossConnectSessionInfo();

	if (!sessionInfo.setData(data+8, size-8))
	{
		fprintf(logFile, "Bad content, or bad size for a sessionInfo packet recieved!\n");
		return;
	}
	
	fprintf(logFile, "sessionInfo.numberOfPlayer=%d, numberOfTeam=%d\n", sessionInfo.numberOfPlayer, sessionInfo.numberOfTeam);
	
	if (ipFromNAT)
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			assert(!sessionInfo.players[j].waitForNatResolution);
	else
		for (int j=0; j<sessionInfo.numberOfPlayer; j++)
			sessionInfo.players[j].waitForNatResolution=sessionInfo.players[j].ipFromNAT;
	
	validSessionInfo=true;
	waitingState=WS_WAITING_FOR_CHECKSUM_CONFIRMATION;
	waitingTimeout=0; //Timeout at one to (not-re) send. (hack but nice)
	waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
	waitingTOTL=DEFAULT_NETWORK_TOTL;
	
	//do we need to download the file from host ? :
	if (sessionInfo.mapGenerationDescriptor && sessionInfo.fileIsAMap)
	{
		fprintf(logFile, "MultiplayersJoin::no need for download, we have a random map.\n");
	}
	else
	{
		const char *filename=NULL;
		
		fprintf(logFile, "MultiplayersJoin::we may need to download, we don't have a random map.\n");
		if (sessionInfo.fileIsAMap)
			filename=sessionInfo.map.getMapFileName();
		else
			filename=sessionInfo.map.getGameFileName();
		
		assert(filename);
		assert(filename[0]);
		fprintf(logFile, "MultiplayersJoin::filename=%s.\n", filename);
		SDL_RWops *stream=globalContainer->fileManager.open(filename,"rb");
		if (stream)
		{
			fprintf(logFile, "MultiplayersJoin::we don't need to download, we do have the file!\n");
			SDL_RWclose(stream);
			filename=NULL;
		}
		else
		{
			fprintf(logFile, "MultiplayersJoin::we do need to download, we don't have the file!\n");
		}
		
		if (filename)
		{
			if (downloadStream)
			{
				if (duplicatePacketFile)
					fprintf(logFile, "MultiplayersJoin:: duplicatePacketFile=%d\n", duplicatePacketFile);
				duplicatePacketFile=0;
				SDL_RWclose(downloadStream);
				downloadStream=NULL;
			}
			unreceivedIndex=0;
			endOfFileIndex=0xFFFFFFFF;
			for (int i=0; i<NET_WINDOW_SIZE; i++)
			{
				netWindow[i].index=0;
				netWindow[i].received=false;
				netWindow[i].packetSize=0; //set 512 in release
			}
			
			downloadStream=globalContainer->fileManager.open(filename,"wb");
		}
	}
	
	if (startDownloadTimeout>2)
		startDownloadTimeout=2;
}

void MultiplayersJoin::dataFileRecieved(char *data, int size, IPaddress ip)
{
	if (downloadStream==NULL)
	{
		fprintf(logFile, "MultiplayersJoin:: no more data file wanted.\n");
		fprintf(logFile, "endOfFileIndex=%d, unreceivedIndex=%d\n", endOfFileIndex, unreceivedIndex);
		if (endOfFileIndex)
		{
			char data[72];
			memset(data, 0, 72);

			data[0]=NEW_PLAYER_WANTS_FILE;
			data[1]=0;
			data[2]=0;
			data[3]=0;

			addUint32(data, 0xFFFFFFFF, 4);
			//for (int ri=0; ri<16; ri++)
			//	addUint32(data, firstReceived[ri], 8+ri*4);

			send(data, 72);
		}
		return;
	}
	int windowIndex=(int)getSint32(data, 4);
	Uint32 writingIndex=getUint32(data, 8);
	int writingSize=size-12;
	fprintf(logFile, "MultiplayersJoin:: received data. size=%d, writingIndex=%d, windowIndex=%d, writingSize=%d\n", size, writingIndex, windowIndex, writingSize);
	
	if (windowIndex==-1 && writingSize!=0)
	{
		fprintf(logFile, "1 we received an bad windowIndex in data file !!!.\n");
		return;
	}
	else if ((windowIndex<0)||(windowIndex>=NET_WINDOW_SIZE))
	{
		fprintf(logFile, "2 we received an bad windowIndex in data file !!!.\n");
		return;
	}
	
	if (writingSize==0)
	{
		endOfFileIndex=writingIndex;
		fprintf(logFile, "1 end of the file is %d.\n", endOfFileIndex);
	}
	else
	{
		if (netWindow[windowIndex].received && netWindow[windowIndex].index==writingIndex && netWindow[windowIndex].packetSize==writingSize)
		{
			duplicatePacketFile++;
			fprintf(logFile, "duplicated \n");
		}
		else if (startDownloadTimeout>2)
			startDownloadTimeout=2;
		
		SDL_RWseek(downloadStream, writingIndex, SEEK_SET);
		SDL_RWwrite(downloadStream, data+12, writingSize, 1);

		netWindow[windowIndex].index=writingIndex;
		netWindow[windowIndex].received=true;
		netWindow[windowIndex].packetSize=writingSize;

		fprintf(logFile, "unreceivedIndex=%d, writingIndex=%d.\n", unreceivedIndex, writingIndex);
	}
	
	bool hit=true;
	bool anyHit=false;
	while (hit)
	{
		hit=false;
		for (int i=0; i<NET_WINDOW_SIZE; i++)
			if (netWindow[i].received)
			{
				Uint32 index=netWindow[i].index;
				Uint32 packetSize=netWindow[i].packetSize;
				if (unreceivedIndex==index)
				{
					unreceivedIndex=index+packetSize;
					hit=true;
					anyHit=true;
				}
			}
	}
	if (anyHit)
		fprintf(logFile, "MultiplayersJoin::new unreceivedIndex=%d.\n", unreceivedIndex);
	
	if (endOfFileIndex==unreceivedIndex)
	{
		if (duplicatePacketFile)
			fprintf(logFile, "MultiplayersJoin:: duplicatePacketFile=%d\n", duplicatePacketFile);
		duplicatePacketFile=0;
		fprintf(logFile, "download's file closed\n");
		SDL_RWclose(downloadStream);
		downloadStream=NULL;
	}
	
}

void MultiplayersJoin::checkSumConfirmationRecieved(char *data, int size, IPaddress ip)
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

	Sint32 rsc=getSint32(data, 4);
	Sint32 lsc=sessionInfo.checkSum();

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
		if (sessionInfo.players[j].netState>=BasePlayer::PNS_BINDED)
		{
			sessionInfo.players[j].close();
			sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
		}
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

void MultiplayersJoin::crossConnectionFirstMessage(char *data, int size, IPaddress ip)
{
	fprintf(logFile, "crossConnectionFirstMessage\n");

	if (size!=8)
	{
		fprintf(logFile, "Bad size for a crossConnectionFirstMessage packet recieved!\n");
		return;
	}

	Sint32 p=data[4];
	fprintf(logFile, "p=%d\n", p);

	if ((p>=0)&&(p<sessionInfo.numberOfPlayer))
	{
		if (sessionInfo.players[p].ip.host!=ip.host)
		{
			if (sessionInfo.players[p].waitForNatResolution)
			{
				fprintf(logFile, "player p=%d, with old nat ip(%s), has been solved by the new ip(%s)!\n", p, Utilities::stringIP(ip), Utilities::stringIP(sessionInfo.players[p].ip));
				sessionInfo.players[p].ip=ip;
				sessionInfo.players[p].ipFromNAT=false;
				sessionInfo.players[p].waitForNatResolution=false;
			}
			else
				fprintf(logFile, "Warning: crossConnectionFirstMessage packet recieved(p=%d), but from ip(%s), but should be ip(%s)! (ip spoofing danger)\n", p, Utilities::stringIP(ip), Utilities::stringIP(sessionInfo.players[p].ip));
		}
		else if ((sessionInfo.players[p].netState>=BasePlayer::PNS_BINDED))
		{
			if (crossPacketRecieved[p]<1)
				crossPacketRecieved[p]=1;
			fprintf(logFile, "crossConnectionFirstMessage packet recieved (%d)\n", p);

			char data[8];
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
			fprintf(logFile, "player %d is not binded! (crossPacketRecieved[%d]=%d).\n", p, p, crossPacketRecieved[p]);
		}
	}
	else
		fprintf(logFile, "Dangerous crossConnectionFirstMessage packet recieved (%d)!\n", p);

}

void MultiplayersJoin::checkAllCrossConnected(void)
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
		fprintf(logFile, "All players are cross connected to me !!\n");
		waitingState=WS_CROSS_CONNECTING_ACHIEVED;
		if (waitingTimeout>0)
		{
			waitingTimeout-=waitingTimeoutSize;
			assert(waitingTimeoutSize);
			waitingTOTL++;
		}
	}
}

void MultiplayersJoin::crossConnectionSecondMessage(char *data, int size, IPaddress ip)
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

void MultiplayersJoin::stillCrossConnectingConfirmation(IPaddress ip)
{
	if (waitingState>=WS_CROSS_CONNECTING_START_CONFIRMED)
	{
		fprintf(logFile, "server(%s) has recieved our stillCrossConnecting state.\n", Utilities::stringIP(ip));
		waitingTimeout=SHORT_NETWORK_TIMEOUT;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL;
	}
	else
		fprintf(logFile, "Warning: ip(%s) sent us a stillCrossConnectingConfirmation while in a bad state!.\n", Utilities::stringIP(ip));
}

void MultiplayersJoin::crossConnectionsAchievedConfirmation(IPaddress ip)
{
	if (waitingState>=WS_CROSS_CONNECTING_ACHIEVED)
	{
		fprintf(logFile, "server(%x,%d has recieved our crossConnection achieved state.\n", ip.host, ip.port);
		waitingState=WS_CROSS_CONNECTING_SERVER_HEARD;
		waitingTimeout=SHORT_NETWORK_TIMEOUT;
		waitingTimeoutSize=SHORT_NETWORK_TIMEOUT;
		waitingTOTL=DEFAULT_NETWORK_TOTL;
	}
	else
		fprintf(logFile, "Warning: ip(%s) sent us a crossConnection achieved state while in a bad state!.\n", Utilities::stringIP(ip));
}

void MultiplayersJoin::serverAskForBeginning(char *data, int size, IPaddress ip)
{
	if (size!=8)
	{
		fprintf(logFile, "Warning: ip(%s) sent us a bad serverAskForBeginning!.\n", Utilities::stringIP(ip));
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

void MultiplayersJoin::treatData(char *data, int size, IPaddress ip)
{
	if ((data[1]!=0)||(data[2]!=0)||(data[3]!=0))
	{
		fprintf(logFile, "Bad packet recieved (%d,%d,%d,%d)!\n", data[0], data[1], data[2], data[3]);
		return;
	}
	switch (data[0])
	{
		case FULL_FILE_DATA:
			//fprintf(logFile, "part of map received from ip(%x,%d)!\n", ip.host, ip.port);
			dataFileRecieved(data, size, ip);
		break;
	
		case SERVER_WATER:
			fprintf(logFile, "water received from ip(%s)!\n", Utilities::stringIP(ip));
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
				//TODO : show an explaination pannel for the joiner.
				fprintf(logFile, "Server kicked you.\n");
				myPlayerNumber=-1;
				waitingState=WS_TYPING_SERVER_NAME;
				kicked=true;
			}
		break;
		case SERVER_QUIT_NEW_GAME :
			if (waitingState<WS_SERVER_START_GAME)
			{
				//TODO : show an explaination pannel for the joiner.
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
			stillCrossConnectingConfirmation(ip);
		break;

		case SERVER_HEARD_CROSS_CONNECTION_CONFIRMATION :
			crossConnectionsAchievedConfirmation(ip);
		break;

		case SERVER_ASK_FOR_GAME_BEGINNING :
			serverAskForBeginning(data, size, ip);
		break;

		default:
			fprintf(logFile, "Unknow kind of packet(%d) recieved from ip(%s)!\n", data[0], Utilities::stringIP(ip));
	}
}

void MultiplayersJoin::receiveTime()
{
	if ((broadcastState==BS_ENABLE_LAN) || (broadcastState==BS_ENABLE_YOG))
	{
		std::list<LANHost>::iterator it;
		for (it=LANHosts.begin(); it!=LANHosts.end(); ++it)
			it->timeout--;
		
		int v;
		LANHost lanhost;
		//fprintf(logFile, "broadcastState=%d.\n", broadcastState);
		if (lan.receive(&v, lanhost.gameName, lanhost.serverNickName))
		{
			fprintf(logFile, "received broadcast response v=(%d), gameName=(%s), serverNickName=(%s).\n", v, lanhost.gameName, lanhost.serverNickName);
			if ((broadcastState==BS_ENABLE_LAN && v==BROADCAST_RESPONSE_LAN)
				|| (broadcastState==BS_ENABLE_YOG && v==BROADCAST_RESPONSE_YOG))
			{
				lanhost.ip=lan.getSenderIP();
				lanhost.timeout=2*DEFAULT_NETWORK_TIMEOUT;
				//We ckeck if this host is already in the list:
				bool already=false;
				for (it=LANHosts.begin(); it!=LANHosts.end(); ++it)
					if (strncmp(lanhost.gameName, it->gameName, 32)==0)
					{
						already=true;
						it->timeout=2*DEFAULT_NETWORK_TIMEOUT;
						break;
					}
				if (!already)
				{
					fprintf(logFile, "added (%s) LANHosts\n", Utilities::stringIP(lanhost.ip));
					LANHosts.push_front(lanhost);
					listHasChanged=true;
				}
			}
			else
				fprintf(logFile, "bad state to rember broadcasting\n");
		}

		for (it=LANHosts.begin(); it!=LANHosts.end(); ++it)
		{
			if (it->timeout<0)
			{
				fprintf(logFile, "removed (%s) LANHosts\n", Utilities::stringIP(it->ip));
				std::list<LANHost>::iterator ittemp=it;
				it=LANHosts.erase(ittemp);
				listHasChanged=true;
			}
		}
		
		if (broadcastState==BS_ENABLE_YOG)
			for (it=LANHosts.begin(); it!=LANHosts.end(); ++it)
				if (strncmp(it->serverNickName, serverNickName, 32)==0)
				{
					if (serverIP.host!=it->ip)
					{
						if ((socket)&&(channel!=-1))
						{
							fprintf(logFile, "MultiplayersJoin:NAT:Unbinding socket.\n");
							// In improbable case that the target ip really hosted a game,
							// we send him a quit game message
							send(CLIENT_QUIT_NEW_GAME);
							SDLNet_UDP_Unbind(socket, channel);
							fprintf(logFile, "MultiplayersJoin:NAT:Socket unbinded.\n");
						}
						
						serverIP.host=it->ip;
						serverIP.port=SDL_SwapBE16(SERVER_PORT);
						fprintf(logFile, "Found a local game with same serverNickName=(%s).\n", serverNickName);
						char *s=SDLNet_ResolveIP(&serverIP);
						if (s)
							serverName=s;
						else
						{
							Utilities::stringIP(serverNameMemory, 128, serverIP.host);
							serverName=serverNameMemory;
						}
						ipFromNAT=true;
						fprintf(logFile, "Trying NAT. serverIP.host=(%s)(%s)\n",Utilities::stringIP(serverIP), serverName);
						
						channel=SDLNet_UDP_Bind(socket, -1, &serverIP);
						if (channel != -1)
						{
							fprintf(logFile, "MultiplayersJoin:NAT:suceeded to bind socket (socket=%x) (channel=%d)\n", (int)socket, channel);
							fprintf(logFile, "MultiplayersJoin:NAT:serverIP.port=%d\n", SDL_SwapBE16(serverIP.port));
						}
						else
						{
							fprintf(logFile, "MultiplayersJoin:NAT:failed to bind socket\n");
							waitingState=WS_TYPING_SERVER_NAME;
						}
					}
				}
				else
				{
					fprintf(logFile, "MultiplayersJoin:NAT: (%s)!=(%s)\n", it->serverNickName, serverNickName);
				}
	}
}

void MultiplayersJoin::onTimer(Uint32 tick)
{
	// call yog step TODO: avoit Host AND Join to 
	if (shareOnYOG)
		globalContainer->yog->step(); // YOG cares about firewall and NATipFromNAT
	
	sendingTime();
	receiveTime();
	
	if ((waitingState!=WS_TYPING_SERVER_NAME) && socket)
	{
		UDPpacket *packet=NULL;
		packet=SDLNet_AllocPacket(MAX_PACKET_SIZE);
		assert(packet);

		while (SDLNet_UDP_Recv(socket, packet)==1)
		{
			fprintf(logFile, "recieved packet (%d)\n", packet->data[0]);
			//fprintf(logFile, "packet=%d\n", (int)packet);
			//fprintf(logFile, "packet->channel=%d\n", packet->channel);
			//fprintf(logFile, "packet->len=%d\n", packet->len);
			//fprintf(logFile, "packet->maxlen=%d\n", packet->maxlen);
			//fprintf(logFile, "packet->status=%d\n", packet->status);
			//fprintf(logFile, "packet->address=%x,%d\n", packet->address.host, packet->address.port);

			//fprintf(logFile, "packet->data=%s\n", packet->data);

			treatData((char *)(packet->data), packet->len, packet->address);
		}

		SDLNet_FreePacket(packet);
	}

	if (waitingState==WS_SERVER_START_GAME)
	{
		if (--startGameTimeCounter<0)
		{
			fprintf(logFile, "MultiplayersJoin::Lets quit this screen and start game!\n");
		}
	}
}

char *MultiplayersJoin::getStatusString()
{
	char *s;
	switch (waitingState)
	{
		case WS_BAD:
			s=globalContainer->texts.getString("[bad error in connection system]");
		break;
		case WS_TYPING_SERVER_NAME:
			s=globalContainer->texts.getString("[not connected]");
		break;
		case WS_WAITING_FOR_PRESENCE:
			s=globalContainer->texts.getString("[presence request sent]");
		break;
		case WS_WAITING_FOR_SESSION_INFO:
			s=globalContainer->texts.getString("[session request sent]");
		break;
		case WS_WAITING_FOR_CHECKSUM_CONFIRMATION:
			s=globalContainer->texts.getString("[checksum sent]");
		break;
		case WS_OK:
			s=globalContainer->texts.getString("[connected to server]");
		break;
		case WS_CROSS_CONNECTING:
		case WS_CROSS_CONNECTING_START_CONFIRMED:
			s=globalContainer->texts.getString("[connecting to all players]");
		break;
		case WS_CROSS_CONNECTING_ACHIEVED:
		case WS_CROSS_CONNECTING_SERVER_HEARD:
		case WS_SERVER_START_GAME:
			s=globalContainer->texts.getString("[connected to all players]");
		break;
		default:
			assert(false);
		break;
	}
	int l=strlen(s)+1;
	char *t=new char[l];
	strncpy(t, s, l);
	return t;
}

void MultiplayersJoin::sendingTime()
{
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
	
	if ((broadcastState==BS_ENABLE_LAN || broadcastState==BS_ENABLE_YOG)&&(--broadcastTimeout<0))
	{
		if (lan.send(BROADCAST_REQUEST))
			broadcastTimeout=DEFAULT_NETWORK_TIMEOUT;
		else
			broadcastState=BS_BAD;
	}
	
	if ( (waitingState!=WS_TYPING_SERVER_NAME) && downloadStream && (startDownloadTimeout--<0) )
	{
		Uint32 firstReceived[16];

		for (int j=0; j<16; j++)
		{
			firstReceived[j]=0xFFFFFFFF;
			for (int i=0; i<NET_WINDOW_SIZE; i++)
				if (netWindow[i].received)
				{
					Uint32 index=netWindow[i].index;
					if (index<firstReceived[j])
					{
						if (j)
						{
							if (index>firstReceived[j-1])
								firstReceived[j]=index;
						}
						else if (index>unreceivedIndex)
							firstReceived[j]=index;
					}
				}
		}
		char data[72];
		memset(data, 0, 72);
		
		data[0]=NEW_PLAYER_WANTS_FILE;
		data[1]=0;
		data[2]=0;
		data[3]=0;
		
		addUint32(data, unreceivedIndex, 4);
		for (int ri=0; ri<16; ri++)
			addUint32(data, firstReceived[ri], 8+ri*4);
		
		send(data, 72);
		fprintf(logFile, "MultiplayersJoin::sending download request\n");
		fprintf(logFile, "MultiplayersJoin::unreceivedIndex=%d\n", unreceivedIndex);
		fprintf(logFile, "receivedIndex=(");
		for (int ix=0; ix<16; ix++)
		{
			firstReceived[ix]=getUint32(data, 8+ix*4);
			fprintf(logFile, "%d, ", firstReceived[ix]);
		}
		fprintf(logFile, ").\n");
		
		startDownloadTimeout=SHORT_NETWORK_TIMEOUT;
	}

	if ((waitingState!=WS_TYPING_SERVER_NAME)&&(--waitingTimeout<0))
	{
		// OK, we are connecting, but a timeout occured.
		
		if (--waitingTOTL<0)
		{
			fprintf(logFile, "Last TOTL spent, server has left\n");
			waitingState=WS_TYPING_SERVER_NAME;
			
			if (broadcastState==BS_ENABLE_YOG || broadcastState==BS_ENABLE_YOG)
				broadcastState=BS_DISABLE_YOG;
			fprintf(logFile, "disabling NAT detection too. bs=(%d)\n", broadcastState);
		}
		else
			fprintf(logFile, "TOTL %d\n", waitingTOTL);
			
		
			
		switch (waitingState)
		{
		case WS_TYPING_SERVER_NAME:
		{
			// Nothing to send to none.
		}
		break;

		case WS_WAITING_FOR_PRESENCE:
		{
			if (sendPresenceRequest())
			{
				if (shareOnYOG)
				{
					if (broadcastState==BS_DISABLE_YOG)
						broadcastState=BS_ENABLE_YOG;
					fprintf(logFile, "enabling NAT detection too. bs=(%d)\n", broadcastState);
				}
			}
			else
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
			MultiplayersCrossConnectable::tryCrossConnections();
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
	UDPpacket *packet=SDLNet_AllocPacket(40);

	assert(packet);

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=40;
	packet->data[0]=NEW_PLAYER_WANTS_PRESENCE;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	addSint32(packet->data, (Sint32)ipFromNAT, 4);
	memset(packet->data+8, 0, 32);
	strncpy((char *)(packet->data+8), playerName, 32);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
		fprintf(logFile, "succeded to send presence request packet to host=(%s)(%s) ipFromNAT=%d\n", Utilities::stringIP(serverIP), serverName, ipFromNAT);
	else
	{
		fprintf(logFile, "failed to send presence request packet to host=(%s(%s))\n", Utilities::stringIP(serverIP), serverName);
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
	UDPpacket *packet=SDLNet_AllocPacket(12);

	assert(packet);

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=12;
	packet->data[0]=NEW_PLAYER_WANTS_SESSION_INFO;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;

	memset(packet->data+4, 0, 8);

	Uint32 netHost=SDL_SwapBE32((Uint32)serverIP.host);
	Uint32 netPort=(Uint32)SDL_SwapBE16(serverIP.port);
	fprintf(logFile, "sendSessionInfoRequest() stringIP=%s, netHost=%s netPort=%d\n", Utilities::stringIP(serverIP), Utilities::stringIP(netHost), netPort);
	addUint32(packet->data, netHost, 4);
	addUint32(packet->data, netPort, 8);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
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

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=8;
	packet->data[0]=NEW_PLAYER_SEND_CHECKSUM_CONFIRMATION;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	Sint32 cs=sessionInfo.checkSum();
	fprintf(logFile, "MultiplayersJoin::cs=%x.\n", cs);
	addSint32((char *)(packet->data), cs, 4);

	if (SDLNet_UDP_Send(socket, channel, packet)==1)
	{
		fprintf(logFile, "suceeded to send confirmation packet\n");
		//fprintf(logFile, "packet->channel=%d\n", packet->channel);
		//fprintf(logFile, "packet->len=%d\n", packet->len);
		//fprintf(logFile, "packet->maxlen=%d\n", packet->maxlen);
		//fprintf(logFile, "packet->status=%d\n", packet->status);
		//fprintf(logFile, "packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//fprintf(logFile, "packet->data=%s, cs=%x\n", packet->data, cs);
	}
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

bool MultiplayersJoin::send(char *data, int size)
{
	UDPpacket *packet=SDLNet_AllocPacket(size);

	assert(packet);

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=size;
	memcpy(packet->data, data, size);
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
	{
		fprintf(logFile, "MultiplayersJoin::send suceeded to send packet\n");
	}
	else
	{
		fprintf(logFile, "MultiplayersJoin::send failed to send packet (channel=%d)\n", channel);
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

	packet->channel=channel;
	packet->address=serverIP;
	packet->len=4;
	packet->data[0]=v;
	packet->data[1]=0;
	packet->data[2]=0;
	packet->data[3]=0;
	if (SDLNet_UDP_Send(socket, -1, packet)==1)
	{
		fprintf(logFile, "MultiplayersJoin::send suceeded to send packet v=%d\n", v);
		//fprintf(logFile, "packet->channel=%d\n", packet->channel);
		//fprintf(logFile, "packet->len=%d\n", packet->len);
		//fprintf(logFile, "packet->maxlen=%d\n", packet->maxlen);
		//fprintf(logFile, "packet->status=%d\n", packet->status);
		//fprintf(logFile, "packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//fprintf(logFile, "packet->data=%s\n", packet->data);
	}
	else
	{
		fprintf(logFile, "MultiplayersJoin::send failed to send packet (v=%d) (channel=%d)\n", v, channel);
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

	packet->channel=channel;
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
	{
		fprintf(logFile, "MultiplayersJoin::send suceeded to send packet v=%d\n", v);
		//fprintf(logFile, "packet->channel=%d\n", packet->channel);
		//fprintf(logFile, "packet->len=%d\n", packet->len);
		//fprintf(logFile, "packet->maxlen=%d\n", packet->maxlen);
		//fprintf(logFile, "packet->status=%d\n", packet->status);
		//fprintf(logFile, "packet->address=%x,%d\n", packet->address.host, packet->address.port);

		//fprintf(logFile, "packet->data=%s\n", packet->data);
	}
	else
	{
		fprintf(logFile, "MultiplayersJoin::send failed to send packet (v=%d) (channel=%d)\n", v, channel);
		SDLNet_FreePacket(packet);
		return false;
	}

	SDLNet_FreePacket(packet);

	return true;
}

bool MultiplayersJoin::tryConnection()
{
	quitThisGame();

	assert(socket==NULL);
	socket=SDLNet_UDP_Open(ANY_PORT);
	
	if (socket)
	{
		IPaddress *localAddress=SDLNet_UDP_GetPeerAddress(socket, -1);
		fprintf(logFile, "Socket opened at ip(%x) port(%d)\n", localAddress->host, localAddress->port);
	}
	else
	{
		fprintf(logFile, "failed to open a socket.\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}

	
	if (!shareOnYOG)
	{
		if (SDLNet_ResolveHost(&serverIP, serverName, SERVER_PORT)==0)
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

	channel=SDLNet_UDP_Bind(socket, -1, &serverIP);
	//or zzz? channel=SDLNet_UDP_Bind(socket, 0, &serverIP);

	if (channel != -1)
	{
		fprintf(logFile, "suceeded to bind socket (socket=%x) (channel=%d)\n", (int)socket, channel);
		fprintf(logFile, "serverIP.port=%d\n", serverIP.port);
	}
	else
	{
		fprintf(logFile, "failed to bind socket\n");
		waitingState=WS_TYPING_SERVER_NAME;
		return false;
	}
	
	if (shareOnYOG)
	{
		//globalContainer->yog->setGameSocket(socket);//TODO: is may be usefull in some NAT or firewall extremes configuration.
		waitingTOTL=DEFAULT_NETWORK_TOTL+1; //because the first try is lost if there is a firewall or NAT.
	}
	else
		waitingTOTL=DEFAULT_NETWORK_TOTL-1;
	
	
	IPaddress *localAddress=SDLNet_UDP_GetPeerAddress(socket, -1);
	fprintf(logFile, "Socket opened at ip(%x) port(%d)\n", localAddress->host, localAddress->port);
	
	return sendPresenceRequest();
}

void MultiplayersJoin::quitThisGame()
{
	fprintf(logFile, "quitThisGame() (this=%x)(socket=%x).\n", (int)this, (int)socket);
	unCrossConnectSessionInfo();

	if (socket)
	{
		if (channel!=-1)
		{
			fprintf(logFile, "Unbinding socket.\n");
			send(CLIENT_QUIT_NEW_GAME);
			SDLNet_UDP_Unbind(socket, channel);
			fprintf(logFile, "MultiplayersJoin::quitThisGame::Socket unbinded.\n");
		}
		fprintf(logFile, "Closing socket.\n");
		SDLNet_UDP_Close(socket);
		socket=NULL;
		fprintf(logFile, "Socket closed.\n");
	}
	
	waitingState=WS_TYPING_SERVER_NAME;
	if (broadcastState==BS_ENABLE_YOG || broadcastState==BS_ENABLE_YOG)
		broadcastState=BS_DISABLE_YOG;
	fprintf(logFile, "disabling NAT detection too. bs=(%d)\n", broadcastState);
}

bool MultiplayersJoin::tryConnection(YOG::GameInfo *yogGameInfo)
{
	assert(yogGameInfo);
	if (this->yogGameInfo)
		delete this->yogGameInfo;
	this->yogGameInfo=yogGameInfo;
	
	serverName=serverNameMemory;
	char *s=SDLNet_ResolveIP(&yogGameInfo->ip);
	if (s)
	{
		strncpy(serverName, s, 128);
		serverName[127]=0;
	}
	else
	{
		Utilities::stringIP(serverName, 128, yogGameInfo->ip.host);
		//zzz Uint32 lip=SDL_SwapBE32(yogGameInfo->ip.host);
		//zzz snprintf(serverName, 128, "%d.%d.%d.%d", (lip>>0)&0xFF, (lip>>8)&0xFF, (lip>>16)&0xFF, (lip>>24)&0xFF);
	}
	serverIP=yogGameInfo->ip;
	printf("MultiplayersJoin::tryConnection::serverName=%s\n", serverName);
	//TODO: is the serverName string usefull ? If it is, the port needs to be passed too !
	
	strncpy(playerName, globalContainer->userName, 32);
	playerName[31]=0;
	//strncpy(gameName, "ilesAleatoires", 32); //TODO: add gameName in YOG
	//gameName[31]=0;
	strncpy(serverNickName, yogGameInfo->userName, 32);
	serverNickName[31]=0;
	return tryConnection();
}
