/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
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
		case MNetSendGameHeader:
		message.reset(new NetSendGameHeader);
		break;
		case MNetStartGame:
		message.reset(new NetStartGame);
		break;
		case MNetRequestMap:
		message.reset(new NetRequestMap);
		break;
		case MNetSendFileInformation:
		message.reset(new NetSendFileInformation);
		break;
		case MNetSendFileChunk:
		message.reset(new NetSendFileChunk);
		break;
		case MNetRequestNextChunk:
		message.reset(new NetRequestNextChunk);
		break;
		case MNetKickPlayer:
		message.reset(new NetKickPlayer);
		break;
		case MNetLeaveGame:
		message.reset(new NetLeaveGame);
		break;
		case MNetReadyToLaunch:
		message.reset(new NetReadyToLaunch);
		break;
		case MNetNotReadyToLaunch:
		message.reset(new NetNotReadyToLaunch);
		break;
		case MNetSendGamePlayerInfo:
		message.reset(new NetSendGamePlayerInfo);
		break;
		case MNetEveryoneReadyToLaunch:
		message.reset(new NetEveryoneReadyToLaunch);
		break;
		case MNetNotEveryoneReadyToLaunch:
		message.reset(new NetNotEveryoneReadyToLaunch);
		break;
		case MNetRequestAddAI:
		message.reset(new NetRequestAddAI);
		break;
		case MNetRemoveAI:
		message.reset(new NetRemoveAI);
		break;
		case MNetChangePlayersTeam:
		message.reset(new NetChangePlayersTeam);
		break;
		case MNetRequestGameStart:
		message.reset(new NetRequestGameStart);
		break;
		case MNetRefuseGameStart:
		message.reset(new NetRefuseGameStart);
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
	stream->writeUint8(order->sender, "sender");
	stream->writeUint32(order->gameCheckSum, "checksum");
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
	order->sender = stream->readUint8("sender");
	order->gameCheckSum = stream->readUint32("checksum");
	
	delete[] buffer;
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
	netVersion=NET_PROTOCOL_VERSION;
}



Uint8 NetSendClientInformation::getMessageType() const
{
	return MNetSendClientInformation;
}



void NetSendClientInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendClientInformation");
	stream->writeUint16(netVersion, "netVersion ");
	stream->writeLeaveSection();
}



void NetSendClientInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendClientInformation");
	netVersion=stream->readUint16("netVersion");
	stream->readLeaveSection();
}



std::string NetSendClientInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendClientInformation(netVersion="<<netVersion<<")";
	return s.str();
}



bool NetSendClientInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendClientInformation))
	{
		const NetSendClientInformation& r = dynamic_cast<const NetSendClientInformation&>(rhs);
		if(r.netVersion == netVersion)
		{
			return true;
		}
	}
	return false;
}


Uint16 NetSendClientInformation::getNetVersion() const
{
	return netVersion;
}



NetSendServerInformation::NetSendServerInformation(YOGLoginPolicy loginPolicy, YOGGamePolicy gamePolicy, Uint16 playerID)
	: loginPolicy(loginPolicy), gamePolicy(gamePolicy), playerID(playerID)
{

}



NetSendServerInformation::NetSendServerInformation()
	: loginPolicy(YOGRequirePassword), gamePolicy(YOGSingleGame), playerID(0)
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
	stream->writeUint16(playerID, "playerID ");
	stream->writeLeaveSection();
}


void NetSendServerInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendServerInformation");
	loginPolicy=static_cast<YOGLoginPolicy>(stream->readUint8("loginPolicy"));
	gamePolicy=static_cast<YOGGamePolicy>(stream->readUint8("gamePolicy"));
	playerID=stream->readUint16("playerID");
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

	s<<"playerID="<<playerID<<"; ";
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



Uint16 NetSendServerInformation::getPlayerID() const
{
	return playerID;
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



std::string NetAttemptRegistration::getUsername() const
{
	return username;
}



std::string NetAttemptRegistration::getPassword() const
{
	return password;
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
	chatChannel = 0;
}
	


NetGameJoinAccepted::NetGameJoinAccepted(Uint32 chatChannel)
	: chatChannel(chatChannel)
{

}



Uint8 NetGameJoinAccepted::getMessageType() const
{
	return MNetGameJoinAccepted;
}



void NetGameJoinAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetGameJoinAccepted");
	stream->writeUint32(chatChannel, "chatChannel");
	stream->writeLeaveSection();
}



void NetGameJoinAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetGameJoinAccepted");
	chatChannel = stream->readUint32("chatChannel");
	stream->readLeaveSection();
}



std::string NetGameJoinAccepted::format() const
{
	std::ostringstream s;
	s<<"NetGameJoinAccepted(chatChannel="<<chatChannel<<")";
	return s.str();
}



bool NetGameJoinAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetGameJoinAccepted))
	{
		const NetGameJoinAccepted& r = dynamic_cast<const NetGameJoinAccepted&>(rhs);
		if(r.chatChannel != chatChannel)
		{
			return false;
		}
		return true;
	}
	return false;
}



Uint32 NetGameJoinAccepted::getChatChannel() const
{
	return chatChannel;
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



NetSendYOGMessage::NetSendYOGMessage(Uint32 channel, boost::shared_ptr<YOGMessage> message)
	: channel(channel), message(message)
{

}



NetSendYOGMessage::NetSendYOGMessage()
	: channel(0)
{

}



Uint8 NetSendYOGMessage::getMessageType() const
{
	return MNetSendYOGMessage;
}



void NetSendYOGMessage::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendYOGMessage");
	stream->writeUint32(channel, "channel");
	message->encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendYOGMessage::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendYOGMessage");
	channel = stream->readUint32("channel");
	message.reset(new YOGMessage);
	message->decodeData(stream);
	stream->readLeaveSection();
}



std::string NetSendYOGMessage::format() const
{
	std::ostringstream s;
	s<<"NetSendYOGMessage(channel="<<channel<<")";
	return s.str();
}



bool NetSendYOGMessage::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendYOGMessage))
	{
		const NetSendYOGMessage& r = dynamic_cast<const NetSendYOGMessage&>(rhs);
		if(channel != r.channel)
			return false;
		else if(!message && !r.message)
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



Uint32 NetSendYOGMessage::getChannel() const
{
	return channel;
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
	chatChannel = 0;
}


NetCreateGameAccepted::NetCreateGameAccepted(Uint32 chatChannel)
	: chatChannel(chatChannel)
{

}



Uint8 NetCreateGameAccepted::getMessageType() const
{
	return MNetCreateGameAccepted;
}



void NetCreateGameAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCreateGameAccepted");
	stream->writeUint32(chatChannel, "chatChannel");
	stream->writeLeaveSection();
}



void NetCreateGameAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGameAccepted");
	chatChannel = stream->readUint32("chatChannel");
	stream->readLeaveSection();
}



std::string NetCreateGameAccepted::format() const
{
	std::ostringstream s;
	s<<"NetCreateGameAccepted(chatChannel="<<chatChannel<<")";
	return s.str();
}



bool NetCreateGameAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGameAccepted))
	{
		const NetCreateGameAccepted& r = dynamic_cast<const NetCreateGameAccepted&>(rhs);
		if(chatChannel != r.chatChannel)
		{
			return false;
		}
		return true;
	}
	return false;
}



Uint32 NetCreateGameAccepted::getChatChannel() const
{
	return chatChannel;
}



NetCreateGameRefused::NetCreateGameRefused()
{
	reason = YOGCreateRefusalUnknown;
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
		if(reason == r.reason)
			return true;	
	}
	return false;
}


YOGGameCreateRefusalReason NetCreateGameRefused::getRefusalReason() const
{
	return reason;
}




NetSendGameHeader::NetSendGameHeader()
{

}


NetSendGameHeader::NetSendGameHeader(const GameHeader& gameHeader)
	:	gameHeader(gameHeader)
{

}



Uint8 NetSendGameHeader::getMessageType() const
{
	return MNetSendGameHeader;
}



void NetSendGameHeader::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendGameHeader");
	gameHeader.saveWithoutPlayerInfo(stream);
	stream->writeLeaveSection();
}



void NetSendGameHeader::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendGameHeader");
	gameHeader.loadWithoutPlayerInfo(stream, VERSION_MINOR);
	stream->readLeaveSection();
}



std::string NetSendGameHeader::format() const
{
	std::ostringstream s;
	s<<"NetSendGameHeader()";
	return s.str();
}



bool NetSendGameHeader::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendGameHeader))
	{
		//const NetSendGameHeader& r = dynamic_cast<const NetSendGameHeader&>(rhs);
//		if(gameHeader == r.gameHeader)
		return true;
	}
	return false;
}

	

void NetSendGameHeader::downloadToGameHeader(GameHeader& newGameHeader)
{
	//This is a special trick used to avoid having to manually copy over every
	//variable
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	GAGCore::BinaryOutputStream* ostream = new BinaryOutputStream(backend);
	gameHeader.saveWithoutPlayerInfo(ostream);

	backend->seekFromStart(0);
	GAGCore::BinaryInputStream* istream = new BinaryInputStream(backend);
	newGameHeader.loadWithoutPlayerInfo(istream, VERSION_MINOR);
}




NetSendGamePlayerInfo::NetSendGamePlayerInfo()
{

}




NetSendGamePlayerInfo::NetSendGamePlayerInfo(GameHeader& gameHeader)
	:	gameHeader(gameHeader)
{
}



Uint8 NetSendGamePlayerInfo::getMessageType() const
{
	return MNetSendGamePlayerInfo;
}



void NetSendGamePlayerInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendGamePlayerInfo");
	gameHeader.savePlayerInfo(stream);
	stream->writeLeaveSection();
}



void NetSendGamePlayerInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendGamePlayerInfo");
	gameHeader.loadPlayerInfo(stream, VERSION_MINOR);
	stream->readLeaveSection();
}



std::string NetSendGamePlayerInfo::format() const
{
	std::ostringstream s;
	s<<"NetSendGamePlayerInfo()";
	return s.str();
}



bool NetSendGamePlayerInfo::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendGamePlayerInfo))
	{
		//const NetSendGamePlayerInfo& r = dynamic_cast<const NetSendGamePlayerInfo&>(rhs);
		return true;
	}
	return false;
}



void NetSendGamePlayerInfo::downloadToGameHeader(GameHeader& header)
{
	//This is a special trick used to avoid having to manually copy over every
	//variable
	MemoryStreamBackend* backend = new MemoryStreamBackend;
	GAGCore::BinaryOutputStream* ostream = new BinaryOutputStream(backend);
	gameHeader.savePlayerInfo(ostream);

	backend->seekFromStart(0);
	GAGCore::BinaryInputStream* istream = new BinaryInputStream(backend);
	header.loadPlayerInfo(istream, VERSION_MINOR);
}



NetStartGame::NetStartGame()
{

}



Uint8 NetStartGame::getMessageType() const
{
	return MNetStartGame;
}



void NetStartGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetStartGame");
	stream->writeLeaveSection();
}



void NetStartGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetStartGame");
	stream->readLeaveSection();
}



std::string NetStartGame::format() const
{
	std::ostringstream s;
	s<<"NetStartGame()";
	return s.str();
}



bool NetStartGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetStartGame))
	{
		//const NetStartGame& r = dynamic_cast<const NetStartGame&>(rhs);
		return true;
	}
	return false;
}



NetRequestMap::NetRequestMap()
{

}



Uint8 NetRequestMap::getMessageType() const
{
	return MNetRequestMap;
}



void NetRequestMap::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestMap");
	stream->writeLeaveSection();
}



void NetRequestMap::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestMap");
	stream->readLeaveSection();
}



std::string NetRequestMap::format() const
{
	std::ostringstream s;
	s<<"NetRequestMap()";
	return s.str();
}



bool NetRequestMap::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestMap))
	{
		//const NetRequestMap& r = dynamic_cast<const NetRequestMap&>(rhs);
		return true;
	}
	return false;
}



NetSendFileInformation::NetSendFileInformation()
	: size(0)
{

}


NetSendFileInformation::NetSendFileInformation(Uint32 filesize)
	: size(filesize)
{
}



Uint8 NetSendFileInformation::getMessageType() const
{
	return MNetSendFileInformation;
}



void NetSendFileInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendFileInformation");
	stream->writeUint32(size, "size");
	stream->writeLeaveSection();
}



void NetSendFileInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendFileInformation");
	size = stream->readUint32("size");
	stream->readLeaveSection();
}



std::string NetSendFileInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendFileInformation(size="<<size<<")";
	return s.str();
}



bool NetSendFileInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendFileInformation))
	{
		const NetSendFileInformation& r = dynamic_cast<const NetSendFileInformation&>(rhs);
		if(r.size == size)
			return true;
	}
	return false;
}



Uint32 NetSendFileInformation::getFileSize() const
{
	return size;
}



NetSendFileChunk::NetSendFileChunk()
{
	std::fill(data, data+1024, 0);
	size=0;
}



NetSendFileChunk::NetSendFileChunk(boost::shared_ptr<GAGCore::InputStream> stream)
{
	size=0;
	int pos=0;
	while(!stream->isEndOfStream() && size < 1024)
	{
		stream->read(data+pos, 1, NULL);
		//For some reason the last byte is an overread, so it should be ignored
		if(!stream->isEndOfStream())
		{
			pos+=1;
			size+=1;
		}
	}
}



Uint8 NetSendFileChunk::getMessageType() const
{
	return MNetSendFileChunk;
}



void NetSendFileChunk::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendFileChunk");
	stream->writeUint32(size, "size");
	stream->write(data, size, "data");
	stream->writeLeaveSection();
}



void NetSendFileChunk::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendFileChunk");
	size = stream->readUint32("size");
	stream->read(data, size, "data");
	stream->readLeaveSection();
}



std::string NetSendFileChunk::format() const
{
	std::ostringstream s;
	s<<"NetSendFileChunk(size="<<size<<")";
	return s.str();
}



bool NetSendFileChunk::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendFileChunk))
	{
		const NetSendFileChunk& r = dynamic_cast<const NetSendFileChunk&>(rhs);
		for(int i=0; i<1024; ++i)
		{
			if(data[i] != r.data[i])
				return false;
		}
		return true;
	}
	return false;
}



const Uint8* NetSendFileChunk::getBuffer() const
{
	return data;
}



Uint32 NetSendFileChunk::getChunkSize() const
{
	return size;
}



NetRequestNextChunk::NetRequestNextChunk()
{

}



Uint8 NetRequestNextChunk::getMessageType() const
{
	return MNetRequestNextChunk;
}



void NetRequestNextChunk::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestNextChunk");
	stream->writeLeaveSection();
}



void NetRequestNextChunk::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestNextChunk");
	stream->readLeaveSection();
}



std::string NetRequestNextChunk::format() const
{
	std::ostringstream s;
	s<<"NetRequestNextChunk()";
	return s.str();
}



bool NetRequestNextChunk::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestNextChunk))
	{
		//const NetRequestNextChunk& r = dynamic_cast<const NetRequestNextChunk&>(rhs);
		return true;
	}
	return false;
}



NetKickPlayer::NetKickPlayer()
	: playerID(0), reason(YOGUnknownKickReason)
{
}



NetKickPlayer::NetKickPlayer(Uint16 playerID, YOGKickReason reason)
	: playerID(playerID), reason(reason)
{
}



Uint8 NetKickPlayer::getMessageType() const
{
	return MNetKickPlayer;
}



void NetKickPlayer::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetKickPlayer");
	stream->writeUint16(playerID, "playerID");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetKickPlayer::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetKickPlayer");
	playerID = stream->readUint16("playerID");
	reason = static_cast<YOGKickReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetKickPlayer::format() const
{
	std::ostringstream s;
	s<<"NetKickPlayer(playerID="<<playerID<<"; reason="<<reason<<")";
	return s.str();
}



bool NetKickPlayer::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetKickPlayer))
	{
		const NetKickPlayer& r = dynamic_cast<const NetKickPlayer&>(rhs);
		if(r.playerID == playerID && r.reason == reason)
			return true;
	}
	return false;
}



Uint16 NetKickPlayer::getPlayerID()
{
	return playerID;
}



YOGKickReason NetKickPlayer::getReason()
{
	return reason;
}




NetLeaveGame::NetLeaveGame()
{

}



Uint8 NetLeaveGame::getMessageType() const
{
	return MNetLeaveGame;
}



void NetLeaveGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetLeaveGame");
	stream->writeLeaveSection();
}



void NetLeaveGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetLeaveGame");
	stream->readLeaveSection();
}



std::string NetLeaveGame::format() const
{
	std::ostringstream s;
	s<<"NetLeaveGame()";
	return s.str();
}



bool NetLeaveGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetLeaveGame))
	{
		//const NetLeaveGame& r = dynamic_cast<const NetLeaveGame&>(rhs);
		return true;
	}
	return false;
}



NetReadyToLaunch::NetReadyToLaunch()
	: playerID(0)
{

}



NetReadyToLaunch::NetReadyToLaunch(Uint16 playerID)
	: playerID(playerID)
{
}



Uint8 NetReadyToLaunch::getMessageType() const
{
	return MNetReadyToLaunch;
}



void NetReadyToLaunch::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetReadyToLaunch");
	stream->writeUint16(playerID, "playerID");
	stream->writeLeaveSection();
}



void NetReadyToLaunch::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetReadyToLaunch");
	playerID = stream->readUint16("playerID");
	stream->readLeaveSection();
}



std::string NetReadyToLaunch::format() const
{
	std::ostringstream s;
	s<<"NetReadyToLaunch("<<"playerID="<<playerID<<"; "<<")";
	return s.str();
}



bool NetReadyToLaunch::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetReadyToLaunch))
	{
		const NetReadyToLaunch& r = dynamic_cast<const NetReadyToLaunch&>(rhs);
		if(r.playerID == playerID)
			return true;
	}
	return false;
}


Uint16 NetReadyToLaunch::getPlayerID() const
{
	return playerID;
}




NetNotReadyToLaunch::NetNotReadyToLaunch()
	: playerID(0)
{

}



NetNotReadyToLaunch::NetNotReadyToLaunch(Uint16 playerID)
	:playerID(playerID)
{
}



Uint8 NetNotReadyToLaunch::getMessageType() const
{
	return MNetNotReadyToLaunch;
}



void NetNotReadyToLaunch::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetNotReadyToLaunch");
	stream->writeUint16(playerID, "playerID");
	stream->writeLeaveSection();
}



void NetNotReadyToLaunch::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetNotReadyToLaunch");
	playerID = stream->readUint16("playerID");
	stream->readLeaveSection();
}



std::string NetNotReadyToLaunch::format() const
{
	std::ostringstream s;
	s<<"NetNotReadyToLaunch("<<"playerID="<<playerID<<"; "<<")";
	return s.str();
}



bool NetNotReadyToLaunch::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetNotReadyToLaunch))
	{
		const NetNotReadyToLaunch& r = dynamic_cast<const NetNotReadyToLaunch&>(rhs);
		if(r.playerID == playerID)
			return true;
	}
	return false;
}


Uint16 NetNotReadyToLaunch::getPlayerID() const
{
	return playerID;
}




NetEveryoneReadyToLaunch::NetEveryoneReadyToLaunch()
{

}



Uint8 NetEveryoneReadyToLaunch::getMessageType() const
{
	return MNetEveryoneReadyToLaunch;
}



void NetEveryoneReadyToLaunch::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetEveryoneReadyToLaunch");
	stream->writeLeaveSection();
}



void NetEveryoneReadyToLaunch::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetEveryoneReadyToLaunch");
	stream->readLeaveSection();
}



std::string NetEveryoneReadyToLaunch::format() const
{
	std::ostringstream s;
	s<<"NetEveryoneReadyToLaunch()";
	return s.str();
}



bool NetEveryoneReadyToLaunch::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetEveryoneReadyToLaunch))
	{
		//const NetEveryoneReadyToLaunch& r = dynamic_cast<const NetEveryoneReadyToLaunch&>(rhs);
		return true;
	}
	return false;
}



NetNotEveryoneReadyToLaunch::NetNotEveryoneReadyToLaunch()
{

}



Uint8 NetNotEveryoneReadyToLaunch::getMessageType() const
{
	return MNetNotEveryoneReadyToLaunch;
}



void NetNotEveryoneReadyToLaunch::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetNotEveryoneReadyToLaunch");
	stream->writeLeaveSection();
}



void NetNotEveryoneReadyToLaunch::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetNotEveryoneReadyToLaunch");
	stream->readLeaveSection();
}



std::string NetNotEveryoneReadyToLaunch::format() const
{
	std::ostringstream s;
	s<<"NetNotEveryoneReadyToLaunch()";
	return s.str();
}



bool NetNotEveryoneReadyToLaunch::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetNotEveryoneReadyToLaunch))
	{
		//const NetNotEveryoneReadyToLaunch& r = dynamic_cast<const NetNotEveryoneReadyToLaunch&>(rhs);
		return true;
	}
	return false;
}



NetRequestAddAI::NetRequestAddAI()
	: type(0)
{

}



NetRequestAddAI::NetRequestAddAI(Uint8 type)
	:type(type)
{
}



Uint8 NetRequestAddAI::getMessageType() const
{
	return MNetRequestAddAI;
}



void NetRequestAddAI::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestAddAI");
	stream->writeUint8(type, "type");
	stream->writeLeaveSection();
}



void NetRequestAddAI::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestAddAI");
	type = stream->readUint8("type");
	stream->readLeaveSection();
}



std::string NetRequestAddAI::format() const
{
	std::ostringstream s;
	s<<"NetRequestAddAI("<<"type="<<type<<"; "<<")";
	return s.str();
}



bool NetRequestAddAI::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestAddAI))
	{
		const NetRequestAddAI& r = dynamic_cast<const NetRequestAddAI&>(rhs);
		if(r.type == type)
			return true;
	}
	return false;
}


Uint8 NetRequestAddAI::getAIType() const
{
	return type;
}




NetRemoveAI::NetRemoveAI()
	: playerNum(0)
{

}



NetRemoveAI::NetRemoveAI(Uint8 playerNum)
	:playerNum(playerNum)
{
}



Uint8 NetRemoveAI::getMessageType() const
{
	return MNetRemoveAI;
}



void NetRemoveAI::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRemoveAI");
	stream->writeUint8(playerNum, "playerNum");
	stream->writeLeaveSection();
}



void NetRemoveAI::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRemoveAI");
	playerNum = stream->readUint8("playerNum");
	stream->readLeaveSection();
}



std::string NetRemoveAI::format() const
{
	std::ostringstream s;
	s<<"NetRemoveAI("<<"playerNum="<<playerNum<<"; "<<")";
	return s.str();
}



bool NetRemoveAI::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRemoveAI))
	{
		const NetRemoveAI& r = dynamic_cast<const NetRemoveAI&>(rhs);
		if(r.playerNum == playerNum)
			return true;
	}
	return false;
}


Uint8 NetRemoveAI::getPlayerNumber() const
{
	return playerNum;
}




NetChangePlayersTeam::NetChangePlayersTeam()
	: player(0), team(0)
{

}



NetChangePlayersTeam::NetChangePlayersTeam(Uint8 player, Uint8 team)
	:player(player), team(team)
{
}



Uint8 NetChangePlayersTeam::getMessageType() const
{
	return MNetChangePlayersTeam;
}



void NetChangePlayersTeam::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetChangePlayersTeam");
	stream->writeUint8(player, "player");
	stream->writeUint8(team, "team");
	stream->writeLeaveSection();
}



void NetChangePlayersTeam::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetChangePlayersTeam");
	player = stream->readUint8("player");
	team = stream->readUint8("team");
	stream->readLeaveSection();
}



std::string NetChangePlayersTeam::format() const
{
	std::ostringstream s;
	s<<"NetChangePlayersTeam("<<"player="<<player<<"; "<<"team="<<team<<"; "<<")";
	return s.str();
}



bool NetChangePlayersTeam::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetChangePlayersTeam))
	{
		const NetChangePlayersTeam& r = dynamic_cast<const NetChangePlayersTeam&>(rhs);
		if(r.player == player && r.team == team)
			return true;
	}
	return false;
}


Uint8 NetChangePlayersTeam::getPlayer() const
{
	return player;
}



Uint8 NetChangePlayersTeam::getTeam() const
{
	return team;
}




NetRequestGameStart::NetRequestGameStart()
{

}



Uint8 NetRequestGameStart::getMessageType() const
{
	return MNetRequestGameStart;
}



void NetRequestGameStart::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestGameStart");
	stream->writeLeaveSection();
}



void NetRequestGameStart::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestGameStart");
	stream->readLeaveSection();
}



std::string NetRequestGameStart::format() const
{
	std::ostringstream s;
	s<<"NetRequestGameStart()";
	return s.str();
}



bool NetRequestGameStart::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestGameStart))
	{
		//const NetRequestGameStart& r = dynamic_cast<const NetRequestGameStart&>(rhs);
		return true;
	}
	return false;
}



NetRefuseGameStart::NetRefuseGameStart()
	: refusalReason(YOGUnknownStartRefusalReason)
{

}



NetRefuseGameStart::NetRefuseGameStart(YOGGameStartRefusalReason refusalReason)
	:refusalReason(refusalReason)
{
}



Uint8 NetRefuseGameStart::getMessageType() const
{
	return MNetRefuseGameStart;
}



void NetRefuseGameStart::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseGameStart");
	stream->writeUint8(refusalReason, "refusalReason");
	stream->writeLeaveSection();
}



void NetRefuseGameStart::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseGameStart");
	refusalReason = static_cast<YOGGameStartRefusalReason>(stream->readUint8("refusalReason"));
	stream->readLeaveSection();
}



std::string NetRefuseGameStart::format() const
{
	std::ostringstream s;
	s<<"NetRefuseGameStart("<<"refusalReason="<<refusalReason<<"; "<<")";
	return s.str();
}



bool NetRefuseGameStart::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseGameStart))
	{
		const NetRefuseGameStart& r = dynamic_cast<const NetRefuseGameStart&>(rhs);
		if(r.refusalReason == refusalReason)
			return true;
	}
	return false;
}


YOGGameStartRefusalReason NetRefuseGameStart::getRefusalReason() const
{
	return refusalReason;
}



//append_code_position
