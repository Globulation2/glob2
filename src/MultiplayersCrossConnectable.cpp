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

#include "MultiplayersCrossConnectable.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"

MultiplayersCrossConnectable::MultiplayersCrossConnectable()
:SessionConnection()
{
	logFile=globalContainer->logFileManager->getFile("MultiplayersCrossConnectable.log");
	assert(logFile);
	
	serverIP.host=0;
	serverIP.port=0;
}

void MultiplayersCrossConnectable::tryCrossConnections(void)
{
	bool sucess=true;
	char data[8];
	data[0]=PLAYER_CROSS_CONNECTION_FIRST_MESSAGE;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	data[4]=myPlayerNumber;
	data[5]=0;
	data[6]=0;
	data[7]=0;
	
	for (int j=0; j<sessionInfo.numberOfPlayer; j++)
		if (sessionInfo.players[j].type==BasePlayer::P_IP)
			if (!sessionInfo.players[j].waitForNatResolution)
			{
				if (crossPacketRecieved[j]<3) // NOTE: is this still usefull ?
				{
					if (sessionInfo.players[j].netState<BasePlayer::PNS_BINDED)
					{
						int freeChannel=getFreeChannel();
						if (!sessionInfo.players[j].bind(socket, freeChannel))
						{
							fprintf(logFile, "Player %d with ip %s is not bindable!\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
							sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
							sucess=false;
							break;
						}
					}
					sessionInfo.players[j].netState=BasePlayer::PNS_BINDED;

					if (!sessionInfo.players[j].send(data, 8))//&&(sessionInfo.players[j].netState<=BasePlayer::PNS_SENDING_FIRST_PACKET)*/
					{
						fprintf(logFile, "Player %d with ip %s is not sendable!\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
						sessionInfo.players[j].netState=BasePlayer::PNS_BAD;
						sucess=false;
						break;
					}
					sessionInfo.players[j].netState=BasePlayer::PNS_SENDING_FIRST_PACKET;
					fprintf(logFile, "We send player %d with ip(%s) the PLAYER_CROSS_CONNECTION_FIRST_MESSAGE\n", j, Utilities::stringIP(sessionInfo.players[j].ip));
				}
			}
			else
				fprintf(logFile, "We wait for nat resolution of player %d.\n", j);
}

int MultiplayersCrossConnectable::getFreeChannel()
{
	for (int channel=1; channel<SDLNET_MAX_UDPCHANNELS; channel++) // By glob2 convention, channel 0 is reserved for game host
	{
		bool good=true;
		for (int i=0; i<32; i++)
		{
			if (sessionInfo.players[i].channel==channel)
				good=false;
		}
		if (good)
		{
			fprintf(logFile, "good free channel=%d\n", channel);
			return channel;
		}
	}
	assert(false);
	return -1;
}
