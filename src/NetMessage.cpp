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
#include <iostream>
#include <sstream>
#include "Version.h"
#include "BinaryStream.h"

using namespace GAGCore;

shared_ptr<NetMessage> NetMessage::getNetMessage(GAGCore::InputStream* stream)
{
	Uint8 netType = stream->readUint8("messageType");
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
		case MNetUpdateGameList:
		message.reset(new NetUpdateGameList);
		break;
		case MNetDisconnect:
		message.reset(new NetDisconnect);
		break;
		case MNetAttemptRegistration:
		message.reset(new NetAttemptRegistration);
		break;
		case MNetAcceptRegistration:
		message.reset(new NetAcceptRegistration);
		break;
		case MNetRefuseRegistration:
		message.reset(new NetRefuseRegistration);
		break;
		case MNetUpdatePlayerList:
		message.reset(new NetUpdatePlayerList);
		break;
		case MNetCreateGame:
		message.reset(new NetCreateGame);
		break;
		case MNetAttemptJoinGame:
		message.reset(new NetAttemptJoinGame);
		break;
		case MNetGameJoinAccepted:
		message.reset(new NetGameJoinAccepted);
		break;
		case MNetGameJoinRefused:
		message.reset(new NetGameJoinRefused);
		break;
		case MNetSendYOGMessage:
		message.reset(new NetSendYOGMessage);
		break;
		case MNetSendMapHeader:
		message.reset(new NetSendMapHeader);
		break;
		case MNetCreateGameAccepted:
		message.reset(new NetCreateGameAccepted);
		break;
		case MNetCreateGameRefused:
		message.reset(new NetCreateGameRefused);
		break;
		case MNetUpdateGameHeaderPlayers:
		message.reset(new NetUpdateGameHeaderPlayers);
		break;
		///append_create_point
	}
	message->decodeData(stream);
	return message;
}



bool NetMessage::operator!=(const NetMessage& rhs) const
{
	return !(*this == rhs);
}



NetSendOrder::NetSendOrder()
{
}


	
NetSendOrder::NetSendOrder(boost::shared_ptr<Order> newOrder)
{
	order=newOrder;
}


	
void NetSendOrder::changeOrder(boost::shared_ptr<Order> newOrder)
{
	order = newOrder;
}


	
boost::shared_ptr<Order> NetSendOrder::getOrder()
{
	return order;
}



Uint8 NetSendOrder::getMessageType() const
{
	return MNetSendOrder;
}



void NetSendOrder::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendOrder");
	Uint32 orderLength = order->getDataLength();
	stream->writeUint32(orderLength+1, "size");
	stream->writeUint8(order->getOrderType(), "orderType");
	stream->write(order->getData(), order->getDataLength(), "data");
	stream->writeLeaveSection();
}



void NetSendOrder::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendOrder");
	size_t size=stream->readUint32("size");
	Uint8* buffer = new Uint8[size];
	stream->read(buffer, size, "data");
	stream->readLeaveSection();
	
	order = Order::getOrder(buffer, size);
	delete buffer;
}



std::string NetSendOrder::format() const
{
	std::stringstream s;
	if(order==NULL)
	{
		s<<"NetSendOrder()";
	}
	else
	{
		s<<"NetSendOrder(orderType="<<static_cast<int>(order->getOrderType())<<")";
	}
	return s.str();
}



bool NetSendOrder::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendOrder))
	{
		const NetSendOrder& r = dynamic_cast<const NetSendOrder&>(rhs);
		if(order==NULL || r.order==NULL)
		{
			return order == r.order;
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



void NetSendClientInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendClientInformation");
	stream->writeUint16(versionMinor, "versionMinor ");
	stream->writeLeaveSection();
}



void NetSendClientInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendClientInformation");
	versionMinor=stream->readUint16("versionMinor");
	stream->readLeaveSection();
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


Uint16 NetSendClientInformation::getVersionMinor() const
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



void NetSendServerInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendServerInformation");
	stream->writeUint8(loginPolicy, "loginPolicy ");
	stream->writeUint8(gamePolicy, "gamePolicy ");
	stream->writeLeaveSection();
}


void NetSendServerInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendServerInformation");
	loginPolicy=static_cast<YOGLoginPolicy>(stream->readUint8("loginPolicy"));
	gamePolicy=static_cast<YOGGamePolicy>(stream->readUint8("gamePolicy"));
	stream->readLeaveSection();
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



void NetAttemptLogin::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAttemptLogin");
	stream->writeText(username, "username");
	stream->writeText(password, "password");
	stream->writeLeaveSection();
}



void NetAttemptLogin::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptLogin");
	username=stream->readText("username");
	password=stream->readText("password");
	stream->readLeaveSection();
}



std::string NetAttemptLogin::format() const
{
	std::ostringstream s;
	s<<"NetAttemptLogin("<<"username=\""<<username<<"\"; password=\""<<password<<"\")";
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



void NetLoginSuccessful::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetLoginSuccessful");
	stream->writeLeaveSection();
}



void NetLoginSuccessful::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptLogin");
	stream->readLeaveSection();

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
//		const NetLoginSuccessful& r = dynamic_cast<const NetLoginSuccessful&>(rhs);
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



void NetRefuseLogin::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseLogin");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetRefuseLogin::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseLogin");
	reason=static_cast<YOGLoginState>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetRefuseLogin::format() const
{
	std::ostringstream s;
	std::string sreason;
	if(reason == YOGLoginSuccessful)
		sreason="YOGLoginSuccessful";
	if(reason == YOGLoginUnknown)
		sreason="YOGLoginUnknown";
	if(reason == YOGPasswordIncorrect)
		sreason="YOGPasswordIncorrect";
	if(reason == YOGUsernameAlreadyUsed)
		sreason="YOGUsernameAlreadyUsed";
	if(reason == YOGUserNotRegistered)
		sreason="YOGUserNotRegistered";
	s<<"NetRefuseLogin(reason="<<sreason<<")";
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


NetUpdateGameList::NetUpdateGameList()
{
	
}



Uint8 NetUpdateGameList::getMessageType() const
{
	return MNetUpdateGameList;
}



void NetUpdateGameList::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetUpdateGameList");
	stream->writeEnterSection("removedGames");
	stream->writeUint8(removedGames.size(), "size");
	for(Uint16 i=0; i<removedGames.size(); ++i)
	{
		stream->writeUint16(removedGames[i], "removedGames[i]");
	}
	stream->writeLeaveSection();
	
	stream->writeEnterSection("updatedGames");
	stream->writeUint8(updatedGames.size(), "size");
	for(Uint16 i=0; i<updatedGames.size(); ++i)
	{
		updatedGames[i].encodeData(stream);
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}



void NetUpdateGameList::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetUpdateGameList");
	
	stream->readEnterSection("removedGames");
	Uint8 size = stream->readUint8("size");
	removedGames.resize(size);
	for(Uint16 i=0; i<removedGames.size(); ++i)
	{
		removedGames[i]=stream->readUint16("removedGames[i]");
	}
	stream->readLeaveSection();
	
	stream->readEnterSection("updatedGames");
	size = stream->readUint8("size");
	updatedGames.resize(size);
	for(Uint16 i=0; i<updatedGames.size(); ++i)
	{
		updatedGames[i].decodeData(stream);
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
}



std::string NetUpdateGameList::format() const
{
	std::ostringstream s;
	s<<"NetUpdateGameList(removedGames "<<removedGames.size()<<", updatedGames "<<updatedGames.size()<<")";
	return s.str();
}



bool NetUpdateGameList::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetUpdateGameList))
	{
		const NetUpdateGameList& r = dynamic_cast<const NetUpdateGameList&>(rhs);
		if(r.removedGames == removedGames && r.updatedGames == updatedGames)
		{
			return true;
		}
	}
	return false;
}



NetDisconnect::NetDisconnect()
{

}



Uint8 NetDisconnect::getMessageType() const
{
	return MNetDisconnect;
}



void NetDisconnect::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetDisconnect");
	stream->writeLeaveSection();
}


void NetDisconnect::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetDisconnect");
	stream->readLeaveSection();
}



std::string NetDisconnect::format() const
{
	std::ostringstream s;
	s<<"NetDisconnect()";
	return s.str();
}



bool NetDisconnect::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetDisconnect))
	{
//		const NetDisconnect& r = dynamic_cast<const NetDisconnect&>(rhs);
		return true;
	}
	return false;
}



NetAttemptRegistration::NetAttemptRegistration()
{

}




NetAttemptRegistration::NetAttemptRegistration(const std::string& username, const std::string& password)
	: username(username), password(password)
{
	
}
	



Uint8 NetAttemptRegistration::getMessageType() const
{
	return MNetAttemptRegistration;
}



void NetAttemptRegistration::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAttemptRegistration");
	stream->writeText(username, "username");
	stream->writeText(password, "password");
	stream->writeLeaveSection();
}



void NetAttemptRegistration::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptRegistration");
	username=stream->readText("username");
	password=stream->readText("password");
	stream->readLeaveSection();
}



std::string NetAttemptRegistration::format() const
{
	std::ostringstream s;
	s<<"NetAttemptRegistration(username=\""<<username<<"\"; password=\""<<password<<"\")";
	return s.str();
}



bool NetAttemptRegistration::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAttemptRegistration))
	{
		const NetAttemptRegistration& r = dynamic_cast<const NetAttemptRegistration&>(rhs);
		if(username == r.username && password == r.password)
			return true;
	}
	return false;
}



NetAcceptRegistration::NetAcceptRegistration()
{

}



Uint8 NetAcceptRegistration::getMessageType() const
{
	return MNetAcceptRegistration;
}



void NetAcceptRegistration::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAcceptRegistration");
	stream->writeLeaveSection();
}



void NetAcceptRegistration::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAcceptRegistration");
	stream->readLeaveSection();
}



std::string NetAcceptRegistration::format() const
{
	std::ostringstream s;
	s<<"NetAcceptRegistration()";
	return s.str();
}



bool NetAcceptRegistration::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAcceptRegistration))
	{
//		const NetAcceptRegistration& r = dynamic_cast<const NetAcceptRegistration&>(rhs);
		return true;
	}
	return false;
}



NetRefuseRegistration::NetRefuseRegistration()
{
	reason = YOGLoginUnknown;
}



NetRefuseRegistration::NetRefuseRegistration(YOGLoginState reason)
	: reason(reason)
{

}



Uint8 NetRefuseRegistration::getMessageType() const
{
	return MNetRefuseRegistration;
}



void NetRefuseRegistration::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseRegistration");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}


void NetRefuseRegistration::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseRegistration");
	reason=static_cast<YOGLoginState>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetRefuseRegistration::format() const
{
	std::ostringstream s;
	std::string sreason;
	if(reason == YOGLoginSuccessful)
		sreason="YOGLoginSuccessful";
	if(reason == YOGLoginUnknown)
		sreason="YOGLoginUnknown";
	if(reason == YOGPasswordIncorrect)
		sreason="YOGPasswordIncorrect";
	if(reason == YOGUsernameAlreadyUsed)
		sreason="YOGUsernameAlreadyUsed";
	if(reason == YOGUserNotRegistered)
		sreason="YOGUserNotRegistered";
	s<<"NetRefuseRegistration(reason="<<sreason<<")";
	return s.str();
}



bool NetRefuseRegistration::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseRegistration))
	{
		const NetRefuseRegistration& r = dynamic_cast<const NetRefuseRegistration&>(rhs);
		if(reason == r.reason)
			return true;
	}
	return false;
}



YOGLoginState NetRefuseRegistration::getRefusalReason() const
{
	return reason;
}


NetUpdatePlayerList::NetUpdatePlayerList()
{

}



Uint8 NetUpdatePlayerList::getMessageType() const
{
	return MNetUpdatePlayerList;
}



void NetUpdatePlayerList::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetUpdatePlayerList");
	stream->writeEnterSection("removedPlayers");
	stream->writeUint8(removedPlayers.size(), "size");
	for(Uint16 i=0; i<removedPlayers.size(); ++i)
	{
		stream->writeUint16(removedPlayers[i], "removedPlayers[i]");
	}
	stream->writeLeaveSection();
	
	stream->writeEnterSection("updatedPlayers");
	stream->writeUint8(updatedPlayers.size(), "size");
	for(Uint16 i=0; i<updatedPlayers.size(); ++i)
	{
		updatedPlayers[i].encodeData(stream);
	}
	stream->writeLeaveSection();

	stream->writeLeaveSection();
}



void NetUpdatePlayerList::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetUpdatePlayerList");
	
	stream->readEnterSection("removedPlayers");
	Uint8 size = stream->readUint8("size");
	removedPlayers.resize(size);
	for(Uint16 i=0; i<removedPlayers.size(); ++i)
	{
		removedPlayers[i]=stream->readUint16("removedPlayers[i]");
	}
	stream->readLeaveSection();
	
	stream->readEnterSection("updatedPlayers");
	size = stream->readUint8("size");
	updatedPlayers.resize(size);
	for(Uint16 i=0; i<updatedPlayers.size(); ++i)
	{
		updatedPlayers[i].decodeData(stream);
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
}



std::string NetUpdatePlayerList::format() const
{
	std::ostringstream s;
	s<<"NetUpdatePlayerList(updatedPlayers "<<updatedPlayers.size()<<", removedPlayers "<<removedPlayers.size()<<")";
	return s.str();
}



bool NetUpdatePlayerList::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetUpdatePlayerList))
	{
		const NetUpdatePlayerList& r = dynamic_cast<const NetUpdatePlayerList&>(rhs);
		if(updatedPlayers == r.updatedPlayers && removedPlayers == r.removedPlayers)
			return true;
	}
	return false;
}



NetCreateGame::NetCreateGame()
{

}



NetCreateGame::NetCreateGame(const std::string& gameName)
	: gameName(gameName)
{

}




Uint8 NetCreateGame::getMessageType() const
{
	return MNetCreateGame;
}



void NetCreateGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCreateGame");
	stream->writeText(gameName, "gameName");
	stream->writeLeaveSection();
}



void NetCreateGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGame");
	gameName=stream->readText("gameName");
	stream->readLeaveSection();
}



std::string NetCreateGame::format() const
{
	std::ostringstream s;
	s<<"NetCreateGame(gameName=\""<<gameName<<"\")";
	return s.str();
}



bool NetCreateGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGame))
	{
		const NetCreateGame& r = dynamic_cast<const NetCreateGame&>(rhs);
		if(r.gameName == gameName)
			return true;
	}
	return false;
}



const std::string& NetCreateGame::getGameName() const
{
	return gameName;
}



NetAttemptJoinGame::NetAttemptJoinGame()
{
	gameID = 0;
}



NetAttemptJoinGame::NetAttemptJoinGame(Uint16 gameID)
	: gameID(gameID)
{

}



Uint8 NetAttemptJoinGame::getMessageType() const
{
	return MNetAttemptJoinGame;
}



void NetAttemptJoinGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAttemptJoinGame");
	stream->writeUint16(gameID, "gameID");
	stream->writeLeaveSection();
}



void NetAttemptJoinGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAttemptJoinGame");
	gameID=stream->readUint16("gameID");
	stream->readLeaveSection();
}



std::string NetAttemptJoinGame::format() const
{
	std::ostringstream s;
	s<<"NetAttemptJoinGame(gameID="<<gameID<<")";
	return s.str();
}



bool NetAttemptJoinGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAttemptJoinGame))
	{
		const NetAttemptJoinGame& r = dynamic_cast<const NetAttemptJoinGame&>(rhs);
		if(r.gameID == gameID)
			return true;
	}
	return false;
}



Uint16 NetAttemptJoinGame::getGameID() const
{
	return gameID;
}
	


NetGameJoinAccepted::NetGameJoinAccepted()
{

}



Uint8 NetGameJoinAccepted::getMessageType() const
{
	return MNetGameJoinAccepted;
}



void NetGameJoinAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetGameJoinAccepted");
	stream->writeLeaveSection();
}



void NetGameJoinAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetGameJoinAccepted");
	stream->readLeaveSection();
}



std::string NetGameJoinAccepted::format() const
{
	std::ostringstream s;
	s<<"NetGameJoinAccepted()";
	return s.str();
}



bool NetGameJoinAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetGameJoinAccepted))
	{
//		const NetGameJoinAccepted& r = dynamic_cast<const NetGameJoinAccepted&>(rhs);
		return true;
	}
	return false;
}



NetGameJoinRefused::NetGameJoinRefused()
{
	reason = YOGJoinRefusalUnknown;
}



NetGameJoinRefused::NetGameJoinRefused(YOGGameJoinRefusalReason reason)
	: reason(reason)
{

}



Uint8 NetGameJoinRefused::getMessageType() const
{
	return MNetGameJoinRefused;
}



void NetGameJoinRefused::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetGameJoinRefused");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetGameJoinRefused::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetGameJoinRefused");
	reason=static_cast<YOGGameJoinRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetGameJoinRefused::format() const
{
	std::ostringstream s;
	std::string sreason;
	if(reason == YOGJoinRefusalUnknown)
		sreason="YOGJoinRefusalUnknown";
	s<<"NetGameJoinRefused(reason="<<sreason<<")";
	return s.str();
}



bool NetGameJoinRefused::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetGameJoinRefused))
	{
		const NetGameJoinRefused& r = dynamic_cast<const NetGameJoinRefused&>(rhs);
		if(r.reason == reason)
			return true;	
	}
	return false;
}




YOGGameJoinRefusalReason NetGameJoinRefused::getRefusalReason() const
{
	return reason;
}



NetSendYOGMessage::NetSendYOGMessage(boost::shared_ptr<YOGMessage> message)
	: message(message)
{

}



NetSendYOGMessage::NetSendYOGMessage()
{

}



Uint8 NetSendYOGMessage::getMessageType() const
{
	return MNetSendYOGMessage;
}



void NetSendYOGMessage::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendYOGMessage");
	message->encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendYOGMessage::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendYOGMessage");
	message.reset(new YOGMessage);
	message->decodeData(stream);
	stream->readLeaveSection();
}



std::string NetSendYOGMessage::format() const
{
	std::ostringstream s;
	s<<"NetSendYOGMessage()";
	return s.str();
}



bool NetSendYOGMessage::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendYOGMessage))
	{
		const NetSendYOGMessage& r = dynamic_cast<const NetSendYOGMessage&>(rhs);
		if(!message && !r.message)
			return true;
		else if(!message && r.message)
			return false;
		else if(message && !r.message)
			return false;
		if((*message) == (*r.message))
			return true;
	}
	return false;
}



boost::shared_ptr<YOGMessage> NetSendYOGMessage::getMessage() const
{
	return message;
}



NetSendMapHeader::NetSendMapHeader()
{

}



NetSendMapHeader::NetSendMapHeader(const MapHeader& mapHeader)
	: mapHeader(mapHeader)
{

}



Uint8 NetSendMapHeader::getMessageType() const
{
	return MNetSendMapHeader;
}



void NetSendMapHeader::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendMapHeader");
	stream->writeUint8(getMessageType(), "messageType");
	mapHeader.save(stream);
	stream->writeLeaveSection();
}



void NetSendMapHeader::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendMapHeader");
	mapHeader.load(stream);
	stream->readLeaveSection();
}



std::string NetSendMapHeader::format() const
{
	std::ostringstream s;
	s<<"NetSendMapHeader(mapname="+mapHeader.getMapName()+")";
	return s.str();
}



bool NetSendMapHeader::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendMapHeader))
	{
		//const NetSendMapHeader& r = dynamic_cast<const NetSendMapHeader&>(rhs);
		return true;
	}
	return false;
}


const MapHeader& NetSendMapHeader::getMapHeader() const
{
	return mapHeader;
}


NetCreateGameAccepted::NetCreateGameAccepted()
{

}



Uint8 NetCreateGameAccepted::getMessageType() const
{
	return MNetCreateGameAccepted;
}



void NetCreateGameAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCreateGameAccepted");
	stream->writeLeaveSection();
}



void NetCreateGameAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGameAccepted");
	stream->readLeaveSection();
}



std::string NetCreateGameAccepted::format() const
{
	std::ostringstream s;
	s<<"NetCreateGameAccepted()";
	return s.str();
}



bool NetCreateGameAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGameAccepted))
	{
		const NetCreateGameAccepted& r = dynamic_cast<const NetCreateGameAccepted&>(rhs);
		return true;
	}
	return false;
}



NetCreateGameRefused::NetCreateGameRefused()
{

}



NetCreateGameRefused::NetCreateGameRefused(YOGGameCreateRefusalReason reason)
	: reason(reason)
{

}



Uint8 NetCreateGameRefused::getMessageType() const
{
	return MNetCreateGameRefused;
}



void NetCreateGameRefused::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCreateGameRefused");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetCreateGameRefused::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGameRefused");
	reason = static_cast<YOGGameCreateRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetCreateGameRefused::format() const
{
	std::ostringstream s;
	s<<"NetCreateGameRefused(reason="<<reason<<")";
	return s.str();
}



bool NetCreateGameRefused::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGameRefused))
	{
		const NetCreateGameRefused& r = dynamic_cast<const NetCreateGameRefused&>(rhs);
		return true;
	}
	return false;
}


YOGGameCreateRefusalReason NetCreateGameRefused::getRefusalReason() const
{
	return reason;
}


NetUpdateGameHeaderPlayers::NetUpdateGameHeaderPlayers()
{

}



NetUpdateGameHeaderPlayers::NetUpdateGameHeaderPlayers(GameHeader& gameHeader)
	: gameHeader(gameHeader)
{

}



Uint8 NetUpdateGameHeaderPlayers::getMessageType() const
{
	return MNetUpdateGameHeaderPlayers;
}



void NetUpdateGameHeaderPlayers::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetUpdateGameHeaderPlayers");
	gameHeader.savePlayerInformation(stream);
	stream->writeLeaveSection();
}



void NetUpdateGameHeaderPlayers::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetUpdateGameHeaderPlayers");
	gameHeader.loadPlayerInformation(stream, VERSION_MINOR);
	stream->readLeaveSection();
}



std::string NetUpdateGameHeaderPlayers::format() const
{
	std::ostringstream s;
	s<<"NetUpdateGameHeaderPlayers()";
	return s.str();
}



bool NetUpdateGameHeaderPlayers::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetUpdateGameHeaderPlayers))
	{
		const NetUpdateGameHeaderPlayers& r = dynamic_cast<const NetUpdateGameHeaderPlayers&>(rhs);
		return true;
	}
	return false;
}



const GameHeader& NetUpdateGameHeaderPlayers::getGameHeader()
{
	return gameHeader;
}


//append_code_position
