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
	shared_ptr<NetMessage> message;
	switch(netType)
	{
		case MNetSendOrder:
		{
			message.reset(new NetSendOrder);
			message->decodeData(netData, datalength);
		}
		break;
	}
	return message;
}



bool NetMessage::operator!=(const NetMessage& rhs) const
{
	return !(*this == rhs);
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
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	Uint32 orderLength = order->getDataLength();
	data[0] = getMessageType();
	Uint8* orderData = order->getData();
	///Copy the data from the order to the local copy
	std::copy(orderData, orderData+orderLength, data+1, data+length);
	delete orderData;
	return data;
}



Uint16 NetSendOrder::getDataLength(void)
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



bool NetSendOrder::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendOrder)
	{
		//Basic type comparison, since Order does not have good comparison facilities
		const NetSendOrder& r = dynamic_cast<const NetSendOrder&>(rhs);
		if(order==NULL || rhs.order==NULL)
		{
			return order == rhs.order;
		}
		if(typeid(r.order) == typeid(order))
		{
			return true;
		}
	}
	return false;
}



NetSendClientInformation::NetSendClientInformation()
{
	versionMinor=VERSION_MINOR;
}



Uint8 NetSendClientInformation::getMessageType()
{
	return MNetSendClientInformation;
}



Uint8 *NetSendClientInformation::encodeData(void)
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	//Write the version minor
	SDLNet_Write8(length, versionMinor);
	return data;
}



Uint16 NetSendClientInformation::getDataLength(void)
{
	return 2;
}



bool NetSendClientInformation::decodeData(const Uint8 *data, int dataLength)
{
	versionMinor = SDLNet_Read16(data);
}



std::string NetSendClientInformation::format()
{
	std::ostringstream s;
	s<<"NetSendClientInformation(versionMinor="<<versionMinor<<")";
	return s.str();
}



bool NetSendClientInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendClientInformation)
	{
		//Basic type comparison, since Order does not have good comparison facilities
		const NetSendClientInformation& r = dynamic_cast<const NetSendClientInformation&>(rhs);
		if(r.versionMinor == versionMinor)
		{
			return true;
		}
	}
	return false;
}

