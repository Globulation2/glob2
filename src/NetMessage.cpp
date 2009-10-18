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
		case MNetRequestFile:
		message.reset(new NetRequestFile);
		break;
		case MNetSendFileInformation:
		message.reset(new NetSendFileInformation);
		break;
		case MNetSendFileChunk:
		message.reset(new NetSendFileChunk);
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
		case MNetPing:
		message.reset(new NetPing);
		break;
		case MNetPingReply:
		message.reset(new NetPingReply);
		break;
		case MNetSetLatencyMode:
		message.reset(new NetSetLatencyMode);
		break;
		case MNetPlayerJoinsGame:
		message.reset(new NetPlayerJoinsGame);
		break;
		case MNetAddAI:
		message.reset(new NetAddAI);
		break;
		case MNetSendReteamingInformation:
		message.reset(new NetSendReteamingInformation);
		break;
		case MNetSendGameResult:
		message.reset(new NetSendGameResult);
		break;
		case MNetPlayerIsBanned:
		message.reset(new NetPlayerIsBanned);
		break;
		case MNetIPIsBanned:
		message.reset(new NetIPIsBanned);
		break;
		case MNetRegisterRouter:
		message.reset(new NetRegisterRouter);
		break;
		case MNetAcknowledgeRouter:
		message.reset(new NetAcknowledgeRouter);
		break;
		case MNetSetGameInRouter:
		message.reset(new NetSetGameInRouter);
		break;
		case MNetSendAfterJoinGameInformation:
		message.reset(new NetSendAfterJoinGameInformation);
		break;
		case MNetRouterAdministratorLogin:
		message.reset(new NetRouterAdministratorLogin);
		break;
		case MNetRouterAdministratorSendCommand:
		message.reset(new NetRouterAdministratorSendCommand);
		break;
		case MNetRouterAdministratorSendText:
		message.reset(new NetRouterAdministratorSendText);
		break;
		case MNetRouterAdministratorLoginAccepted:
		message.reset(new NetRouterAdministratorLoginAccepted);
		break;
		case MNetRouterAdministratorLoginRefused:
		message.reset(new NetRouterAdministratorLoginRefused);
		break;
		case MNetDownloadableMapInfos:
		message.reset(new NetDownloadableMapInfos);
		break;
		case MNetRequestDownloadableMapList:
		message.reset(new NetRequestDownloadableMapList);
		break;
		case MNetRequestMapUpload:
		message.reset(new NetRequestMapUpload);
		break;
		case MNetAcceptMapUpload:
		message.reset(new NetAcceptMapUpload);
		break;
		case MNetRefuseMapUpload:
		message.reset(new NetRefuseMapUpload);
		break;
		case MNetCancelSendingFile:
		message.reset(new NetCancelSendingFile);
		break;
		case MNetCancelRecievingFile:
		message.reset(new NetCancelRecievingFile);
		break;
		case MNetRequestMapThumbnail:
		message.reset(new NetRequestMapThumbnail);
		break;
		case MNetSendMapThumbnail:
		message.reset(new NetSendMapThumbnail);
		break;
		case MNetSubmitRatingOnMap:
		message.reset(new NetSubmitRatingOnMap);
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
	
	order = Order::getOrder(buffer, size, VERSION_MINOR);
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



NetGameJoinRefused::NetGameJoinRefused(YOGServerGameJoinRefusalReason reason)
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
	reason=static_cast<YOGServerGameJoinRefusalReason>(stream->readUint8("reason"));
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




YOGServerGameJoinRefusalReason NetGameJoinRefused::getRefusalReason() const
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
	gameID = 0;
	routerIP = "";
	fileID = 0;
}


NetCreateGameAccepted::NetCreateGameAccepted(Uint32 chatChannel, Uint16 gameID, const std::string& routerIP, Uint16 fileID)
	: chatChannel(chatChannel), gameID(gameID), routerIP(routerIP), fileID(fileID)
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
	stream->writeUint16(gameID, "gameID");
	stream->writeText(routerIP, "routerIP");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetCreateGameAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCreateGameAccepted");
	chatChannel = stream->readUint32("chatChannel");
	gameID = stream->readUint16("gameID");
	routerIP = stream->readText("routerIP");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetCreateGameAccepted::format() const
{
	std::ostringstream s;
	s<<"NetCreateGameAccepted(chatChannel="<<chatChannel<<",gameID="<<gameID<<",routerIP="<<routerIP<<",fileID="<<fileID<<")";
	return s.str();
}



bool NetCreateGameAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCreateGameAccepted))
	{
		const NetCreateGameAccepted& r = dynamic_cast<const NetCreateGameAccepted&>(rhs);
		if(chatChannel != r.chatChannel || gameID != r.gameID || routerIP != r.routerIP || fileID != r.fileID)
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



Uint16 NetCreateGameAccepted::getGameID() const
{
	return gameID;
}



const std::string NetCreateGameAccepted::getGameRouterIP() const
{
	return routerIP;
}



Uint16 NetCreateGameAccepted::getFileID() const
{
	return fileID;
}



NetCreateGameRefused::NetCreateGameRefused()
{
	reason = YOGCreateRefusalUnknown;
}



NetCreateGameRefused::NetCreateGameRefused(YOGServerGameCreateRefusalReason reason)
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
	reason = static_cast<YOGServerGameCreateRefusalReason>(stream->readUint8("reason"));
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


YOGServerGameCreateRefusalReason NetCreateGameRefused::getRefusalReason() const
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
	MemoryStreamBackend* obackend = new MemoryStreamBackend;
	GAGCore::BinaryOutputStream* ostream = new BinaryOutputStream(obackend);
	gameHeader.saveWithoutPlayerInfo(ostream);


	obackend->seekFromStart(0);
	MemoryStreamBackend* ibackend = new MemoryStreamBackend(*obackend);
	GAGCore::BinaryInputStream* istream = new BinaryInputStream(ibackend);
	newGameHeader.loadWithoutPlayerInfo(istream, VERSION_MINOR);

	delete ostream;
	delete istream;
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
	MemoryStreamBackend* obackend = new MemoryStreamBackend;
	GAGCore::BinaryOutputStream* ostream = new BinaryOutputStream(obackend);
	gameHeader.savePlayerInfo(ostream);

	obackend->seekFromStart(0);
	MemoryStreamBackend* ibackend = new MemoryStreamBackend(*obackend);
	GAGCore::BinaryInputStream* istream = new BinaryInputStream(ibackend);
	header.loadPlayerInfo(istream, VERSION_MINOR);

	delete ostream;
	delete istream;
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



NetRequestFile::NetRequestFile()
	: fileID(0)
{

}



NetRequestFile::NetRequestFile(Uint16 fileID)
	: fileID(fileID)
{

}



Uint8 NetRequestFile::getMessageType() const
{
	return MNetRequestFile;
}



void NetRequestFile::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestFile");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetRequestFile::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestFile");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetRequestFile::format() const
{
	std::ostringstream s;
	s<<"NetRequestFile(fileID="<<fileID<<")";
	return s.str();
}



bool NetRequestFile::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestFile))
	{
		const NetRequestFile& r = dynamic_cast<const NetRequestFile&>(rhs);
		if(fileID == r.fileID)
			return true;
	}
	return false;
}



Uint16 NetRequestFile::getFileID()
{
	return fileID;
}



NetSendFileInformation::NetSendFileInformation()
	: size(0), fileID(0)
{

}


NetSendFileInformation::NetSendFileInformation(Uint32 filesize, Uint16 fileID)
	: size(filesize), fileID(fileID)
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
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetSendFileInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendFileInformation");
	size = stream->readUint32("size");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetSendFileInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendFileInformation(size="<<size<<",fileID="<<fileID<<")";
	return s.str();
}



bool NetSendFileInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendFileInformation))
	{
		const NetSendFileInformation& r = dynamic_cast<const NetSendFileInformation&>(rhs);
		if(r.size == size && r.fileID == fileID)
			return true;
	}
	return false;
}



Uint32 NetSendFileInformation::getFileSize() const
{
	return size;
}



Uint16 NetSendFileInformation::getFileID() const
{
	return fileID;
}



NetSendFileChunk::NetSendFileChunk()
{
	std::fill(data, data+4096, 0);
	size=0;
	fileID=0;
}



NetSendFileChunk::NetSendFileChunk(boost::shared_ptr<GAGCore::InputStream> stream, Uint16 fileID)
	: fileID(fileID)
{
	size=0;
	int pos=0;
	while(!stream->isEndOfStream() && size < 4096)
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
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetSendFileChunk::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendFileChunk");
	size = stream->readUint32("size");
	stream->read(data, size, "data");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetSendFileChunk::format() const
{
	std::ostringstream s;
	s<<"NetSendFileChunk(size="<<size<<",fileID="<<fileID<<")";
	return s.str();
}



bool NetSendFileChunk::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendFileChunk))
	{
		const NetSendFileChunk& r = dynamic_cast<const NetSendFileChunk&>(rhs);
		for(int i=0; i<4096; ++i)
		{
			if(data[i] != r.data[i])
				return false;
		}
		if(fileID != r.fileID)
			return false;
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



Uint16 NetSendFileChunk::getFileID() const
{
	return fileID;
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



NetRefuseGameStart::NetRefuseGameStart(YOGServerGameStartRefusalReason refusalReason)
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
	refusalReason = static_cast<YOGServerGameStartRefusalReason>(stream->readUint8("refusalReason"));
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


YOGServerGameStartRefusalReason NetRefuseGameStart::getRefusalReason() const
{
	return refusalReason;
}




NetPing::NetPing()
{

}



Uint8 NetPing::getMessageType() const
{
	return MNetPing;
}



void NetPing::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPing");
	stream->writeLeaveSection();
}



void NetPing::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPing");
	stream->readLeaveSection();
}



std::string NetPing::format() const
{
	std::ostringstream s;
	s<<"NetPing()";
	return s.str();
}



bool NetPing::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPing))
	{
		//const NetPing& r = dynamic_cast<const NetPing&>(rhs);
		return true;
	}
	return false;
}



NetPingReply::NetPingReply()
{

}



Uint8 NetPingReply::getMessageType() const
{
	return MNetPingReply;
}



void NetPingReply::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPingReply");
	stream->writeLeaveSection();
}



void NetPingReply::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPingReply");
	stream->readLeaveSection();
}



std::string NetPingReply::format() const
{
	std::ostringstream s;
	s<<"NetPingReply()";
	return s.str();
}



bool NetPingReply::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPingReply))
	{
		//const NetPingReply& r = dynamic_cast<const NetPingReply&>(rhs);
		return true;
	}
	return false;
}



NetSetLatencyMode::NetSetLatencyMode()
	: latencyAdjustment(0)
{

}



NetSetLatencyMode::NetSetLatencyMode(Uint8 latencyAdjustment)
	:latencyAdjustment(latencyAdjustment)
{
}



Uint8 NetSetLatencyMode::getMessageType() const
{
	return MNetSetLatencyMode;
}



void NetSetLatencyMode::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSetLatencyMode");
	stream->writeUint8(latencyAdjustment, "latencyAdjustment");
	stream->writeLeaveSection();
}



void NetSetLatencyMode::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSetLatencyMode");
	latencyAdjustment = stream->readUint8("latencyAdjustment");
	stream->readLeaveSection();
}



std::string NetSetLatencyMode::format() const
{
	std::ostringstream s;
	s<<"NetSetLatencyMode("<<"latencyAdjustment="<<static_cast<int>(latencyAdjustment)<<"; "<<")";
	return s.str();
}



bool NetSetLatencyMode::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSetLatencyMode))
	{
		const NetSetLatencyMode& r = dynamic_cast<const NetSetLatencyMode&>(rhs);
		if(r.latencyAdjustment == latencyAdjustment)
			return true;
	}
	return false;
}


Uint8 NetSetLatencyMode::getLatencyAdjustment() const
{
	return latencyAdjustment;
}




NetPlayerJoinsGame::NetPlayerJoinsGame()
	: playerID(0), playerName("")
{

}



NetPlayerJoinsGame::NetPlayerJoinsGame(Uint16 playerID, std::string playerName)
	:playerID(playerID), playerName(playerName)
{
}



Uint8 NetPlayerJoinsGame::getMessageType() const
{
	return MNetPlayerJoinsGame;
}



void NetPlayerJoinsGame::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPlayerJoinsGame");
	stream->writeUint16(playerID, "playerID");
	stream->writeText(playerName, "playerName");
	stream->writeLeaveSection();
}



void NetPlayerJoinsGame::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPlayerJoinsGame");
	playerID = stream->readUint16("playerID");
	playerName = stream->readText("playerName");
	stream->readLeaveSection();
}



std::string NetPlayerJoinsGame::format() const
{
	std::ostringstream s;
	s<<"NetPlayerJoinsGame("<<"playerID="<<playerID<<"; "<<"playerName="<<playerName<<"; "<<")";
	return s.str();
}



bool NetPlayerJoinsGame::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPlayerJoinsGame))
	{
		const NetPlayerJoinsGame& r = dynamic_cast<const NetPlayerJoinsGame&>(rhs);
		if(r.playerID == playerID && r.playerName == playerName)
			return true;
	}
	return false;
}


Uint16 NetPlayerJoinsGame::getPlayerID() const
{
	return playerID;
}



std::string NetPlayerJoinsGame::getPlayerName() const
{
	return playerName;
}




NetAddAI::NetAddAI()
	: type(0)
{

}



NetAddAI::NetAddAI(Uint8 type)
	:type(type)
{
}



Uint8 NetAddAI::getMessageType() const
{
	return MNetAddAI;
}



void NetAddAI::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAddAI");
	stream->writeUint8(type, "type");
	stream->writeLeaveSection();
}



void NetAddAI::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAddAI");
	type = stream->readUint8("type");
	stream->readLeaveSection();
}



std::string NetAddAI::format() const
{
	std::ostringstream s;
	s<<"NetAddAI("<<"type="<<type<<"; "<<")";
	return s.str();
}



bool NetAddAI::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAddAI))
	{
		const NetAddAI& r = dynamic_cast<const NetAddAI&>(rhs);
		if(r.type == type)
			return true;
	}
	return false;
}


Uint8 NetAddAI::getType() const
{
	return type;
}




NetSendReteamingInformation::NetSendReteamingInformation()
{

}



NetSendReteamingInformation::NetSendReteamingInformation(NetReteamingInformation reteamingInfo)
	:reteamingInfo(reteamingInfo)
{
}



Uint8 NetSendReteamingInformation::getMessageType() const
{
	return MNetSendReteamingInformation;
}



void NetSendReteamingInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendReteamingInformation");
	reteamingInfo.encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendReteamingInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendReteamingInformation");
	reteamingInfo.decodeData(stream);
	stream->readLeaveSection();
}



std::string NetSendReteamingInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendReteamingInformation()";
	return s.str();
}



bool NetSendReteamingInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendReteamingInformation))
	{
		const NetSendReteamingInformation& r = dynamic_cast<const NetSendReteamingInformation&>(rhs);
		if(r.reteamingInfo == reteamingInfo)
			return true;
	}
	return false;
}


NetReteamingInformation NetSendReteamingInformation::getReteamingInfo() const
{
	return reteamingInfo;
}




NetSendGameResult::NetSendGameResult()
	: result(YOGGameResultUnknown)
{

}



NetSendGameResult::NetSendGameResult(YOGGameResult result)
	:result(result)
{
}



Uint8 NetSendGameResult::getMessageType() const
{
	return MNetSendGameResult;
}



void NetSendGameResult::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendGameResult");
	stream->writeUint8(static_cast<Uint8>(result), "result");
	stream->writeLeaveSection();
}



void NetSendGameResult::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendGameResult");
	result = static_cast<YOGGameResult>(stream->readUint8("result"));
	stream->readLeaveSection();
}



std::string NetSendGameResult::format() const
{
	std::ostringstream s;
	s<<"NetSendGameResult("<<"result="<<result<<"; "<<")";
	return s.str();
}



bool NetSendGameResult::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendGameResult))
	{
		const NetSendGameResult& r = dynamic_cast<const NetSendGameResult&>(rhs);
		if(r.result == result)
			return true;
	}
	return false;
}


YOGGameResult NetSendGameResult::getGameResult() const
{
	return result;
}




NetPlayerIsBanned::NetPlayerIsBanned()
{

}



Uint8 NetPlayerIsBanned::getMessageType() const
{
	return MNetPlayerIsBanned;
}



void NetPlayerIsBanned::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetPlayerIsBanned");
	stream->writeLeaveSection();
}



void NetPlayerIsBanned::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetPlayerIsBanned");
	stream->readLeaveSection();
}



std::string NetPlayerIsBanned::format() const
{
	std::ostringstream s;
	s<<"NetPlayerIsBanned()";
	return s.str();
}



bool NetPlayerIsBanned::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetPlayerIsBanned))
	{
		//const NetPlayerIsBanned& r = dynamic_cast<const NetPlayerIsBanned&>(rhs);
		return true;
	}
	return false;
}



NetIPIsBanned::NetIPIsBanned()
{

}



Uint8 NetIPIsBanned::getMessageType() const
{
	return MNetIPIsBanned;
}



void NetIPIsBanned::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetIPIsBanned");
	stream->writeLeaveSection();
}



void NetIPIsBanned::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetIPIsBanned");
	stream->readLeaveSection();
}



std::string NetIPIsBanned::format() const
{
	std::ostringstream s;
	s<<"NetIPIsBanned()";
	return s.str();
}



bool NetIPIsBanned::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetIPIsBanned))
	{
		//const NetIPIsBanned& r = dynamic_cast<const NetIPIsBanned&>(rhs);
		return true;
	}
	return false;
}



NetRegisterRouter::NetRegisterRouter()
{

}



Uint8 NetRegisterRouter::getMessageType() const
{
	return MNetRegisterRouter;
}



void NetRegisterRouter::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRegisterRouter");
	stream->writeLeaveSection();
}



void NetRegisterRouter::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRegisterRouter");
	stream->readLeaveSection();
}



std::string NetRegisterRouter::format() const
{
	std::ostringstream s;
	s<<"NetRegisterRouter()";
	return s.str();
}



bool NetRegisterRouter::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRegisterRouter))
	{
		//const NetRegisterRouter& r = dynamic_cast<const NetRegisterRouter&>(rhs);
		return true;
	}
	return false;
}



NetAcknowledgeRouter::NetAcknowledgeRouter()
{

}



Uint8 NetAcknowledgeRouter::getMessageType() const
{
	return MNetAcknowledgeRouter;
}



void NetAcknowledgeRouter::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAcknowledgeRouter");
	stream->writeLeaveSection();
}



void NetAcknowledgeRouter::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAcknowledgeRouter");
	stream->readLeaveSection();
}



std::string NetAcknowledgeRouter::format() const
{
	std::ostringstream s;
	s<<"NetAcknowledgeRouter()";
	return s.str();
}



bool NetAcknowledgeRouter::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAcknowledgeRouter))
	{
		//const NetAcknowledgeRouter& r = dynamic_cast<const NetAcknowledgeRouter&>(rhs);
		return true;
	}
	return false;
}



NetSetGameInRouter::NetSetGameInRouter()
	: gameID(0)
{

}



NetSetGameInRouter::NetSetGameInRouter(Uint16 gameID)
	:gameID(gameID)
{
}



Uint8 NetSetGameInRouter::getMessageType() const
{
	return MNetSetGameInRouter;
}



void NetSetGameInRouter::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSetGameInRouter");
	stream->writeUint16(gameID, "gameID");
	stream->writeLeaveSection();
}



void NetSetGameInRouter::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSetGameInRouter");
	gameID = stream->readUint16("gameID");
	stream->readLeaveSection();
}



std::string NetSetGameInRouter::format() const
{
	std::ostringstream s;
	s<<"NetSetGameInRouter("<<"gameID="<<gameID<<"; "<<")";
	return s.str();
}



bool NetSetGameInRouter::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSetGameInRouter))
	{
		const NetSetGameInRouter& r = dynamic_cast<const NetSetGameInRouter&>(rhs);
		if(r.gameID == gameID)
			return true;
	}
	return false;
}


Uint16 NetSetGameInRouter::getGameID() const
{
	return gameID;
}




NetSendAfterJoinGameInformation::NetSendAfterJoinGameInformation()
	: info()
{

}



NetSendAfterJoinGameInformation::NetSendAfterJoinGameInformation(YOGAfterJoinGameInformation info)
	:info(info)
{
}



Uint8 NetSendAfterJoinGameInformation::getMessageType() const
{
	return MNetSendAfterJoinGameInformation;
}



void NetSendAfterJoinGameInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendAfterJoinGameInformation");
	info.encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendAfterJoinGameInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendAfterJoinGameInformation");
	info.decodeData(stream);
	stream->readLeaveSection();
}



std::string NetSendAfterJoinGameInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendAfterJoinGameInformation("<<"="<<"; "<<")";
	return s.str();
}



bool NetSendAfterJoinGameInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendAfterJoinGameInformation))
	{
		const NetSendAfterJoinGameInformation& r = dynamic_cast<const NetSendAfterJoinGameInformation&>(rhs);
		if(r.info == info)
			return true;
	}
	return false;
}


YOGAfterJoinGameInformation NetSendAfterJoinGameInformation::getAfterJoinGameInformation() const
{
	return info;
}




NetRouterAdministratorLogin::NetRouterAdministratorLogin()
	: password()
{

}



NetRouterAdministratorLogin::NetRouterAdministratorLogin(std::string password)
	:password(password)
{
}



Uint8 NetRouterAdministratorLogin::getMessageType() const
{
	return MNetRouterAdministratorLogin;
}



void NetRouterAdministratorLogin::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorLogin");
	stream->writeText(password, "password");
	stream->writeLeaveSection();
}



void NetRouterAdministratorLogin::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorLogin");
	password = stream->readText("password");
	stream->readLeaveSection();
}



std::string NetRouterAdministratorLogin::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorLogin("<<"password="<<password<<"; "<<")";
	return s.str();
}



bool NetRouterAdministratorLogin::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorLogin))
	{
		const NetRouterAdministratorLogin& r = dynamic_cast<const NetRouterAdministratorLogin&>(rhs);
		if(r.password == password)
			return true;
	}
	return false;
}


std::string NetRouterAdministratorLogin::getPassword() const
{
	return password;
}




NetRouterAdministratorSendCommand::NetRouterAdministratorSendCommand()
	: command("")
{

}



NetRouterAdministratorSendCommand::NetRouterAdministratorSendCommand(std::string command)
	:command(command)
{
}



Uint8 NetRouterAdministratorSendCommand::getMessageType() const
{
	return MNetRouterAdministratorSendCommand;
}



void NetRouterAdministratorSendCommand::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorSendCommand");
	stream->writeText(command, "command");
	stream->writeLeaveSection();
}



void NetRouterAdministratorSendCommand::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorSendCommand");
	command = stream->readText("command");
	stream->readLeaveSection();
}



std::string NetRouterAdministratorSendCommand::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorSendCommand("<<"command="<<command<<"; "<<")";
	return s.str();
}



bool NetRouterAdministratorSendCommand::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorSendCommand))
	{
		const NetRouterAdministratorSendCommand& r = dynamic_cast<const NetRouterAdministratorSendCommand&>(rhs);
		if(r.command == command)
			return true;
	}
	return false;
}


std::string NetRouterAdministratorSendCommand::getCommand() const
{
	return command;
}




NetRouterAdministratorSendText::NetRouterAdministratorSendText()
	: text("")
{

}



NetRouterAdministratorSendText::NetRouterAdministratorSendText(std::string text)
	:text(text)
{
}



Uint8 NetRouterAdministratorSendText::getMessageType() const
{
	return MNetRouterAdministratorSendText;
}



void NetRouterAdministratorSendText::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorSendText");
	stream->writeText(text, "text");
	stream->writeLeaveSection();
}



void NetRouterAdministratorSendText::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorSendText");
	text = stream->readText("text");
	stream->readLeaveSection();
}



std::string NetRouterAdministratorSendText::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorSendText("<<"text="<<text<<"; "<<")";
	return s.str();
}



bool NetRouterAdministratorSendText::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorSendText))
	{
		const NetRouterAdministratorSendText& r = dynamic_cast<const NetRouterAdministratorSendText&>(rhs);
		if(r.text == text)
			return true;
	}
	return false;
}


std::string NetRouterAdministratorSendText::getText() const
{
	return text;
}




NetRouterAdministratorLoginAccepted::NetRouterAdministratorLoginAccepted()
{

}



Uint8 NetRouterAdministratorLoginAccepted::getMessageType() const
{
	return MNetRouterAdministratorLoginAccepted;
}



void NetRouterAdministratorLoginAccepted::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorLoginAccepted");
	stream->writeLeaveSection();
}



void NetRouterAdministratorLoginAccepted::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorLoginAccepted");
	stream->readLeaveSection();
}



std::string NetRouterAdministratorLoginAccepted::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorLoginAccepted()";
	return s.str();
}



bool NetRouterAdministratorLoginAccepted::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorLoginAccepted))
	{
		//const NetRouterAdministratorLoginAccepted& r = dynamic_cast<const NetRouterAdministratorLoginAccepted&>(rhs);
		return true;
	}
	return false;
}



NetRouterAdministratorLoginRefused::NetRouterAdministratorLoginRefused()
	: reason(YOGRouterLoginUnknown)
{

}



NetRouterAdministratorLoginRefused::NetRouterAdministratorLoginRefused(YOGRouterAdministratorLoginRefusalReason reason)
	:reason(reason)
{
}



Uint8 NetRouterAdministratorLoginRefused::getMessageType() const
{
	return MNetRouterAdministratorLoginRefused;
}



void NetRouterAdministratorLoginRefused::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRouterAdministratorLoginRefused");
	stream->writeUint8(reason, "reason");
	stream->writeLeaveSection();
}



void NetRouterAdministratorLoginRefused::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRouterAdministratorLoginRefused");
	reason = static_cast<YOGRouterAdministratorLoginRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetRouterAdministratorLoginRefused::format() const
{
	std::ostringstream s;
	s<<"NetRouterAdministratorLoginRefused("<<"reason="<<reason<<"; "<<")";
	return s.str();
}



bool NetRouterAdministratorLoginRefused::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRouterAdministratorLoginRefused))
	{
		const NetRouterAdministratorLoginRefused& r = dynamic_cast<const NetRouterAdministratorLoginRefused&>(rhs);
		if(r.reason == reason)
			return true;
	}
	return false;
}


YOGRouterAdministratorLoginRefusalReason NetRouterAdministratorLoginRefused::getReason() const
{
	return reason;
}




NetDownloadableMapInfos::NetDownloadableMapInfos()
	: maps()
{

}



NetDownloadableMapInfos::NetDownloadableMapInfos(std::vector<YOGDownloadableMapInfo> maps)
	:maps(maps)
{
}



Uint8 NetDownloadableMapInfos::getMessageType() const
{
	return MNetDownloadableMapInfos;
}



void NetDownloadableMapInfos::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetDownloadableMapInfos");
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "size");
	for(unsigned int i=0; i<maps.size(); ++i)
	{
		stream->writeEnterSection(i);
		maps[i].encodeData(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void NetDownloadableMapInfos::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetDownloadableMapInfos");
	stream->readEnterSection("maps");
	Uint32 size = stream->readUint32("maps");
	maps.resize(size);
	for(unsigned int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		maps[i].decodeData(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
}



std::string NetDownloadableMapInfos::format() const
{
	std::ostringstream s;
	s<<"NetDownloadableMapInfos(maps.size()="<<maps.size()<<"; "<<")";
	return s.str();
}



bool NetDownloadableMapInfos::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetDownloadableMapInfos))
	{
		const NetDownloadableMapInfos& r = dynamic_cast<const NetDownloadableMapInfos&>(rhs);
		if(r.maps == maps)
			return true;
	}
	return false;
}


std::vector<YOGDownloadableMapInfo> NetDownloadableMapInfos::getMaps() const
{
	return maps;
}




NetRequestDownloadableMapList::NetRequestDownloadableMapList()
{

}



Uint8 NetRequestDownloadableMapList::getMessageType() const
{
	return MNetRequestDownloadableMapList;
}



void NetRequestDownloadableMapList::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestDownloadableMapList");
	stream->writeLeaveSection();
}



void NetRequestDownloadableMapList::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestDownloadableMapList");
	stream->readLeaveSection();
}



std::string NetRequestDownloadableMapList::format() const
{
	std::ostringstream s;
	s<<"NetRequestDownloadableMapList()";
	return s.str();
}



bool NetRequestDownloadableMapList::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestDownloadableMapList))
	{
		//const NetRequestDownloadableMapList& r = dynamic_cast<const NetRequestDownloadableMapList&>(rhs);
		return true;
	}
	return false;
}



NetRequestMapUpload::NetRequestMapUpload()
	: mapInfo()
{

}



NetRequestMapUpload::NetRequestMapUpload(YOGDownloadableMapInfo mapInfo)
	:mapInfo(mapInfo)
{
}



Uint8 NetRequestMapUpload::getMessageType() const
{
	return MNetRequestMapUpload;
}



void NetRequestMapUpload::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestMapUpload");
	mapInfo.encodeData(stream);
	stream->writeLeaveSection();
}



void NetRequestMapUpload::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestMapUpload");
	mapInfo.decodeData(stream, VERSION_MINOR);
	stream->readLeaveSection();
}



std::string NetRequestMapUpload::format() const
{
	std::ostringstream s;
	s<<"NetRequestMapUpload("<<"""="<<""<<"; "<<")";
	return s.str();
}



bool NetRequestMapUpload::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestMapUpload))
	{
		const NetRequestMapUpload& r = dynamic_cast<const NetRequestMapUpload&>(rhs);
		if(r.mapInfo == mapInfo)
			return true;
	}
	return false;
}


YOGDownloadableMapInfo NetRequestMapUpload::getMapInfo() const
{
	return mapInfo;
}




NetAcceptMapUpload::NetAcceptMapUpload()
	: fileID(0)
{

}



NetAcceptMapUpload::NetAcceptMapUpload(Uint16 fileID)
	:fileID(fileID)
{
}



Uint8 NetAcceptMapUpload::getMessageType() const
{
	return MNetAcceptMapUpload;
}



void NetAcceptMapUpload::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAcceptMapUpload");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetAcceptMapUpload::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAcceptMapUpload");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetAcceptMapUpload::format() const
{
	std::ostringstream s;
	s<<"NetAcceptMapUpload("<<"fileID="<<fileID<<"; "<<")";
	return s.str();
}



bool NetAcceptMapUpload::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAcceptMapUpload))
	{
		const NetAcceptMapUpload& r = dynamic_cast<const NetAcceptMapUpload&>(rhs);
		if(r.fileID == fileID)
			return true;
	}
	return false;
}


Uint16 NetAcceptMapUpload::getFileID() const
{
	return fileID;
}




NetRefuseMapUpload::NetRefuseMapUpload()
	: reason(YOGMapUploadReasonUnknown)
{

}



NetRefuseMapUpload::NetRefuseMapUpload(YOGMapUploadRefusalReason reason)
	:reason(reason)
{
}



Uint8 NetRefuseMapUpload::getMessageType() const
{
	return MNetRefuseMapUpload;
}



void NetRefuseMapUpload::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseMapUpload");
	stream->writeUint8(static_cast<Uint8>(reason), "reason");
	stream->writeLeaveSection();
}



void NetRefuseMapUpload::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseMapUpload");
	reason = static_cast<YOGMapUploadRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetRefuseMapUpload::format() const
{
	std::ostringstream s;
	s<<"NetRefuseMapUpload("<<"reason="<<reason<<"; "<<")";
	return s.str();
}



bool NetRefuseMapUpload::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseMapUpload))
	{
		const NetRefuseMapUpload& r = dynamic_cast<const NetRefuseMapUpload&>(rhs);
		if(r.reason == reason)
			return true;
	}
	return false;
}


YOGMapUploadRefusalReason NetRefuseMapUpload::getReason() const
{
	return reason;
}




NetCancelSendingFile::NetCancelSendingFile()
	: fileID(0)
{

}



NetCancelSendingFile::NetCancelSendingFile(Uint16 fileID)
	:fileID(fileID)
{
}



Uint8 NetCancelSendingFile::getMessageType() const
{
	return MNetCancelSendingFile;
}



void NetCancelSendingFile::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCancelSendingFile");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetCancelSendingFile::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCancelSendingFile");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetCancelSendingFile::format() const
{
	std::ostringstream s;
	s<<"NetCancelSendingFile("<<"fileID="<<fileID<<"; "<<")";
	return s.str();
}



bool NetCancelSendingFile::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCancelSendingFile))
	{
		const NetCancelSendingFile& r = dynamic_cast<const NetCancelSendingFile&>(rhs);
		if(r.fileID == fileID)
			return true;
	}
	return false;
}


Uint16 NetCancelSendingFile::getFileID() const
{
	return fileID;
}




NetCancelRecievingFile::NetCancelRecievingFile()
	: fileID(0)
{

}



NetCancelRecievingFile::NetCancelRecievingFile(Uint16 fileID)
	:fileID(fileID)
{
}



Uint8 NetCancelRecievingFile::getMessageType() const
{
	return MNetCancelRecievingFile;
}



void NetCancelRecievingFile::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCancelRecievingFile");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetCancelRecievingFile::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCancelRecievingFile");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetCancelRecievingFile::format() const
{
	std::ostringstream s;
	s<<"NetCancelRecievingFile("<<"fileID="<<fileID<<"; "<<")";
	return s.str();
}



bool NetCancelRecievingFile::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCancelRecievingFile))
	{
		const NetCancelRecievingFile& r = dynamic_cast<const NetCancelRecievingFile&>(rhs);
		if(r.fileID == fileID)
			return true;
	}
	return false;
}


Uint16 NetCancelRecievingFile::getFileID() const
{
	return fileID;
}




NetRequestMapThumbnail::NetRequestMapThumbnail()
	: mapID(0)
{

}



NetRequestMapThumbnail::NetRequestMapThumbnail(Uint16 mapID)
	: mapID(mapID)
{
}



Uint8 NetRequestMapThumbnail::getMessageType() const
{
	return MNetRequestMapThumbnail;
}



void NetRequestMapThumbnail::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestMapThumbnail");
	stream->writeUint16(mapID, "mapID");
	stream->writeLeaveSection();
}



void NetRequestMapThumbnail::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestMapThumbnail");
	mapID = stream->readUint16("mapID");
	stream->readLeaveSection();
}



std::string NetRequestMapThumbnail::format() const
{
	std::ostringstream s;
	s<<"NetRequestMapThumbnail("<<"mapID="<<mapID<<"; "<<")";
	return s.str();
}



bool NetRequestMapThumbnail::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestMapThumbnail))
	{
		const NetRequestMapThumbnail& r = dynamic_cast<const NetRequestMapThumbnail&>(rhs);
		if(r.mapID == mapID)
			return true;
	}
	return false;
}


Uint16 NetRequestMapThumbnail::getMapID() const
{
	return mapID;
}




NetSendMapThumbnail::NetSendMapThumbnail()
	: mapID(0), thumbnail()
{

}



NetSendMapThumbnail::NetSendMapThumbnail(Uint16 mapID, MapThumbnail thumbnail)
	:mapID(mapID), thumbnail(thumbnail)
{
}



Uint8 NetSendMapThumbnail::getMessageType() const
{
	return MNetSendMapThumbnail;
}



void NetSendMapThumbnail::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendMapThumbnail");
	stream->writeUint16(mapID, "mapID");
	thumbnail.encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendMapThumbnail::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendMapThumbnail");
	mapID = stream->readUint16("mapID");
	thumbnail.decodeData(stream, VERSION_MINOR);
	stream->readLeaveSection();
}



std::string NetSendMapThumbnail::format() const
{
	std::ostringstream s;
	s<<"NetSendMapThumbnail("<<"mapID="<<mapID<<"; "<<"="<<""<<"; "<<")";
	return s.str();
}



bool NetSendMapThumbnail::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendMapThumbnail))
	{
		const NetSendMapThumbnail& r = dynamic_cast<const NetSendMapThumbnail&>(rhs);
		if(r.mapID == mapID)
			return true;
	}
	return false;
}


Uint16 NetSendMapThumbnail::getMapID() const
{
	return mapID;
}



MapThumbnail NetSendMapThumbnail::getThumbnail() const
{
	return thumbnail;
}




NetSubmitRatingOnMap::NetSubmitRatingOnMap()
	: mapID(0), rating(0)
{

}



NetSubmitRatingOnMap::NetSubmitRatingOnMap(Uint16 mapID, Uint8 rating)
	:mapID(mapID), rating(rating)
{
}



Uint8 NetSubmitRatingOnMap::getMessageType() const
{
	return MNetSubmitRatingOnMap;
}



void NetSubmitRatingOnMap::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSubmitRatingOnMap");
	stream->writeUint16(mapID, "mapID");
	stream->writeUint8(rating, "rating");
	stream->writeLeaveSection();
}



void NetSubmitRatingOnMap::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSubmitRatingOnMap");
	mapID = stream->readUint16("mapID");
	rating = stream->readUint8("rating");
	stream->readLeaveSection();
}



std::string NetSubmitRatingOnMap::format() const
{
	std::ostringstream s;
	s<<"NetSubmitRatingOnMap("<<"mapID="<<mapID<<"; "<<"rating="<<rating<<"; "<<")";
	return s.str();
}



bool NetSubmitRatingOnMap::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSubmitRatingOnMap))
	{
		const NetSubmitRatingOnMap& r = dynamic_cast<const NetSubmitRatingOnMap&>(rhs);
		if(r.mapID == mapID && r.rating == rating)
			return true;
	}
	return false;
}


Uint16 NetSubmitRatingOnMap::getMapID() const
{
	return mapID;
}



Uint8 NetSubmitRatingOnMap::getRating() const
{
	return rating;
}



//append_code_position
