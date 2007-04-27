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

#include "NetMessage.h"
#include <algorithm>
#include <stringstream>

static NetMessage* NetMessage::getNetMessage(const Uint8 *netData, int dataLength)
{
	Uint8 netType = netData[0];
	NetMessage* message = NULL;
	switch(netType)
	{
		case MNetSendOrder:
		{
			message = new NetSendOrder;
			message->decodeData(netData, datalength);
		}
		break;
	}
	return message;
}


NetSendOrder::NetSendOrder()
{
	order=NULL;
}


	
NetSendOrder::NetSendOrder(Order* newOrder)
{
	order=newOrder;
}


	
void NetSendOrder::changeOrder(Order* newOrder)
{
	if(order!=NULL)
		delete order;
	order=newOrder;
}


	
Order* NetSendOrder::getOrder()
{
	return order;
}



Uint8 NetSendOrder::getMessageType()
{
	return MNetSendOrder;
}



Uint8 *NetSendOrder::encodeData(void)
{
	Uint32 length = getDataLength();
	Uint8* data = new Uint8[length];
	data[0] = getMessageType();
	Uint8* orderData = order->getData();
	///Copy the data from the order to the local copy
	std::copy(orderData, orderData+length-1, data+1, data+length);
	delete orderData;
	return data;
}



Uint32 NetSendOrder::getDataLength(void)
{
	return 1 + order->getDataLength();
}



bool NetSendOrder::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 type = data[0];
	order = Order::getOrder(data+1, dataLength-1);
}



std::string NetSendOrder::format()
{
	std::ostringstream s;
	if(order==NULL)
	{
		s<<"NetSendOrder()";
	}
	else
	{
		s<<"NetSendOrder(orderType="<<order->type<<")";
	}
	return s.str();
}


