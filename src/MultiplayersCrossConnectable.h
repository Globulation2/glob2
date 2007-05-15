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

#ifndef __MULTIPLAYERS_CROSS_CONNECTABLE_H
#define __MULTIPLAYERS_CROSS_CONNECTABLE_H

#include "SessionConnection.h"

class MultiplayersCrossConnectable:public SessionConnection
{
public:
	MultiplayersCrossConnectable();
	virtual ~MultiplayersCrossConnectable() { }
	void tryCrossConnections(void);
	bool sameip(IPaddress ip);
	bool send(Uint8 *data, int size);
	bool send(const Uint8 u, const Uint8 v);
	void sendingTime();
	void confirmedMessage(Uint8 *data, int size, IPaddress ip);
	void receivedMessage(Uint8 *data, int size, IPaddress ip);
	void sendMessage(const char *s);
	
public:
	bool isServer;
	IPaddress serverIP;
	char serverNickName[32];
	
	Uint8 messageID;
	struct Message
	{
		Uint8 messageID;
		Uint8 messageType;
		char userName[32];
		char text[512];
		int timeout;
		int TOTL;
		bool guiPainted;
		bool received[33];
	};
	std::list<Message> sendingMessages;
	std::list<Message> receivedMessages;

private:
	FILE *logFile;
};


#endif
