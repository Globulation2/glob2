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
		message.reset(new NetSendOrder);
		break;
		case MNetSendClientInformation:
		message.reset(new NetSendClientInformation);
		break;
		case MNetSendServerInformation:
		message.reset(new NetSendServerInformation);
		break;
		case MNetAttemptLogin:
		message.reset(new NetAttemptLogin);
		break;
		case MNetLoginSuccessful:
		message.reset(new NetLoginSuccessful);
		break;
		case MNetRefuseLogin:
		message.reset(new NetRefuseLogin);
		break;
	}
	message->decodeData(netData, datalength);
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



Uint8 NetSendOrder::getMessageType() const
{
	return MNetSendOrder;
}



Uint8 *NetSendOrder::encodeData() const
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



Uint16 NetSendOrder::getDataLength() const
{
	return 1 + order->getDataLength();
}



bool NetSendOrder::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 type = data[0];
	order = Order::getOrder(data+1, dataLength-1);
}



std::string NetSendOrder::format() const
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
	if(typeid(rhs)==typeid(NetSendOrder))
	{
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



Uint8 NetSendClientInformation::getMessageType() const
{
	return MNetSendClientInformation;
}



Uint8 *NetSendClientInformation::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	data[0] = getMessageType();
	//Write the version minor
	SDLNet_Write16(data+1, versionMinor);
	return data;
}



Uint16 NetSendClientInformation::getDataLength() const
{
	return 3;
}



bool NetSendClientInformation::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 type = data[0];
	versionMinor = SDLNet_Read16(data+1);
}



std::string NetSendClientInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendClientInformation(versionMinor="<<versionMinor<<")";
	return s.str();
}



bool NetSendClientInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendClientInformation))
	{
		const NetSendClientInformation& r = dynamic_cast<const NetSendClientInformation&>(rhs);
		if(r.versionMinor == versionMinor)
		{
			return true;
		}
	}
	return false;
}


Uint63 NetSendClientInformation::getVersionMinor() const
{
	return versionMinor;
}



NetSendServerInformation::NetSendServerInformation(YOGLoginPolicy loginPolicy, YOGGamePolicy gamePolicy)
	: loginPolicy(loginPolicy), gamePolicy(gamePolicy)
{

}



NetSendServerInformation::NetSendServerInformation()
	: loginPolicy(YOGRequirePassword), gamePolicy(YOGSingleGame)
{

}



Uint8 NetSendServerInformation::getMessageType() const
{
	return MNetSendServerInformation;
}



Uint8 *NetSendServerInformation::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	data[0] = getMessageType();
	//Write the policy information
	data[1] = static_cast<Uint8>(loginPolicy);
	data[2] = static_cast<Uint8>(gamePolicy);
	return data;
}



Uint16 NetSendServerInformation::getDataLength() const
{
	return 3;
}



bool NetSendServerInformation::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 type = data[0];
	loginPolicy = data[1];
	gamePolicy = data[2];
}



std::string NetSendServerInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendServerInformation(";
	if(loginPolicy == YOGRequirePassword)
		s<<"loginPolicy=YOGRequirePassword; ";
	else if(loginPolicy == YOGAnonymousLogin)
		s<<"loginPolicy=YOGAnonymousLogin; ";

	if(gamePolicy == YOGSingleGame)
		s<<"gamePolicy=YOGSingleGame; ";
	else if(gamePolicy == YOGMultipleGames)
		s<<"gamePolicy=YOGMultipleGames; ";

	s<<")";
	return s.str();
}



bool NetSendServerInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendServerInformation))
	{
		const NetSendServerInformation& r = dynamic_cast<const NetSendServerInformation&>(rhs);
		if(r.loginPolicy == loginPolicy && r.gamePolicy == gamePolicy)
		{
			return true;
		}
	}
	return false;
}


	
YOGLoginPolicy NetSendServerInformation::getLoginPolicy() const
{
	return loginPolicy;
}


	
YOGGamePolicy NetSendServerInformation::getGamePolicy() const
{
	return gamePolicy;
}



NetAttemptLogin::NetAttemptLogin(const std::string& username, const std::string& password)
	: username(username), password(password)
{

}



NetAttemptLogin::NetAttemptLogin()
{

}



Uint8 NetAttemptLogin::getMessageType() const
{
	return MNetAttemptLogin;
}



Uint8 *NetAttemptLogin::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	//Use pos to keep track of the position in the data
	Uint8 pos = 0;
	data[pos] = getMessageType();
	pos+=1;
	//Write the username.
	data[pos] = static_cast<Uint8>(username.size());
	pos+=1;
	std::copy(username.begin(), username.end(), data+pos);
	pos+=username.size();
	//Write the password
	data[pos] = static_cast<Uint8>(password.size());
	pos+=1;
	std::copy(password.begin(), password.end(), data+pos);
	pos+=password.size();
	return data;
}



Uint16 NetAttemptLogin::getDataLength() const
{
	return 3 + username.size() + password.size();
}



bool NetAttemptLogin::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 pos = 0;
	Uint8 type = data[pos];
	pos+=1;
	
	//Read in the username
	Uint8 usernameLength = data[pos];
	pos+=1;
	for(int i=0; i<usernameLength; ++i)
	{
		username+=static_cast<char>(data[pos]);
		pos+=1;
	}
	
	//Read in the password
	Uint8 passwordLength = data[pos];
	pos+=1;
	for(int i=0; i<usernameLength; ++i)
	{
		password+=static_cast<char>(data[pos]);
		pos+=1;
	}
}



std::string NetAttemptLogin::format() const
{
	std::ostringstream s;
	s<<"NetAttemptLogin("<<"username="<<username<<"; password="<<password<<")";
	return s.str();
}



bool NetAttemptLogin::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAttemptLogin))
	{
		const NetAttemptLogin& r = dynamic_cast<const NetAttemptLogin&>(rhs);
		if(r.username == username && r.password==password)
		{
			return true;
		}
	}
	return false;
}



const std::string& NetAttemptLogin::getUsername() const
{
	return username;
}



const std::string& NetAttemptLogin::getPassword() const
{
	return password;
}



NetLoginSuccessful::NetLoginSuccessful()
{

}



Uint8 NetLoginSuccessful::getMessageType() const
{
	return MNetLoginSuccessful;
}



Uint8 *NetLoginSuccessful::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	data[0] = getMessageType();
	return data;
}



Uint16 NetLoginSuccessful::getDataLength() const
{
	return 1;
}



bool NetLoginSuccessful::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 type = data[0];
}



std::string NetLoginSuccessful::format() const
{
	std::ostringstream s;
	s<<"NetLoginSuccessful()";
	return s.str();
}



bool NetLoginSuccessful::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetLoginSuccessful))
	{
		const NetLoginSuccessful& r = dynamic_cast<const NetLoginSuccessful&>(rhs);
		return true;
	}
	return false;
}


NetRefuseLogin::NetRefuseLogin()
	: reason(YOGLoginSuccessful)
{

}



NetRefuseLogin::NetRefuseLogin(YOGLoginState reason)
	: reason(reason)
{

}



Uint8 NetRefuseLogin::getMessageType() const
{
	return MNetRefuseLogin;
}



Uint8 *NetRefuseLogin::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	data[0] = getMessageType();
	data[1] = static_cast<Uint8>(reason);
	return data;
}



Uint16 NetRefuseLogin::getDataLength() const
{
	return 2;
}



bool NetRefuseLogin::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 type = data[0];
	reason = data[1];
}



std::string NetRefuseLogin::format() const
{
	std::ostringstream s;
	s<<"NetRefuseLogin(reason="<<reason<<")";
	return s.str();
}



bool NetRefuseLogin::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseLogin))
	{
		const NetRefuseLogin& r = dynamic_cast<const NetRefuseLogin&>(rhs);
		if(r.reason == reason)
		{
			return true;
		}
	}
	return false;
}


	
YOGLoginState NetRefuseLogin::getRefusalReason() const
{
	return reason;
}


