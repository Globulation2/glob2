// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "NetMessage.h"

#include <iostream>

#include "AuthMessages.h"
#include "FileTransferMessages.h"
#include "GameCreateMessages.h"
#include "GameHeaderMessages.h"
#include "GameJoinMessages.h"
#include "GameLaunchMessages.h"
#include "GameTeamMessages.h"
#include "LobbyMessages.h"
#include "MapDatabaseMessages.h"
#include "MapUploadMessages.h"
#include "OrderMessages.h"
#include "RegistrationMessages.h"
#include "RouterAdminMessages.h"
#include "RouterMessages.h"

std::shared_ptr<NetMessage> NetMessage::getNetMessage(GAGCore::InputStream* stream)
{
	Uint8 netType = stream->readUint8("messageType");
	std::shared_ptr<NetMessage> message;
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
		case MNetRegistrationRequest:
		message.reset(new NetRegistrationRequest);
		break;
		case MNetRegistrationAccepted:
		message.reset(new NetRegistrationAccepted);
		break;
		case MNetRegistrationRefused:
		message.reset(new NetRegistrationRefused);
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
		case MNetRouterAdministratorCommandRequest:
		message.reset(new NetRouterAdministratorCommandRequest);
		break;
		case MNetRouterAdministratorCommandResponse:
		message.reset(new NetRouterAdministratorCommandResponse);
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
		default:
		// Untrusted byte from the wire didn't match any known opcode.
		// Drop the message and let the caller handle the null shared_ptr
		// (existing call sites already guard with `if(!message) return;`).
		std::cerr << "NetMessage::getNetMessage: unknown opcode " << (int)netType << std::endl;
		return std::shared_ptr<NetMessage>();
	}
	message->decodeData(stream);
	return message;
}

bool NetMessage::operator!=(const NetMessage& rhs) const
{
	return !(*this == rhs);
}
