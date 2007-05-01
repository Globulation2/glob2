/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "YOGPlayer.h"

YOGPlayer::YOGPlayer(shared_ptr<NetConnection> connection) : connection(connection)
{
	connectionState = WaitingForClientInformation;
}



void YOGPlayer::update()
{
	//Parse incoming messages.
	shared_ptr<NetMessage> message = nc.getMessage();
	Uint8 type = message->getMessageType();
	if(type==MNetSendClientInformation)
	{
		shared_ptr<NetSendClientInformation> info = static_pointer_cast<NetSendClientInformation>(message);
		versionMinor = info->versionMinor;
		connectionState = NeedToSendServerInformation;
	}
}


bool YOGPlayer::isConnected()
{
	return connection.isConnected();
}

