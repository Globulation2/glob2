// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

/// Enumeration of message types carried over the YOG/lobby protocol.
/// The first block (through MNetSendServerInformation) must keep its order
/// to maintain wire compatibility with older clients/servers; later entries
/// may be reordered freely as the protocol is glob2-version-locked.
enum NetMessageType
{
	// These must be kept in this order to maintain compatibility with future versions of glob2
	MNetRegistrationAccepted,
	MNetAttemptLogin,
	MNetRegistrationRequest,
	MNetDisconnect,
	MNetLoginSuccessful,
	MNetPing,
	MNetPingReply,
	MNetRefuseLogin,
	MNetRegistrationRefused,
	MNetSendClientInformation,
	MNetSendServerInformation,

	// These are all glob2 version dependent and can be kept in any order
	MNetAcknowledgeRouter,
	MNetAddAI,
	MNetAttemptJoinGame,
	MNetChangePlayersTeam,
	MNetCreateGame,
	MNetCreateGameAccepted,
	MNetCreateGameRefused,
	MNetGameJoinAccepted,
	MNetGameJoinRefused,
	MNetIPIsBanned,
	MNetKickPlayer,
	MNetLeaveGame,
	MNetNotReadyToLaunch,
	MNetPlayerIsBanned,
	MNetPlayerJoinsGame,
	MNetReadyToLaunch,
	MNetRefuseGameStart,
	MNetRegisterRouter,
	MNetRemoveAI,
	MNetRequestGameStart,
	MNetRequestFile,
	MNetRouterAdministratorLogin,
	MNetRouterAdministratorLoginAccepted,
	MNetRouterAdministratorLoginRefused,
	MNetRouterAdministratorCommandRequest,
	MNetRouterAdministratorCommandResponse,
	MNetSendAfterJoinGameInformation,
	MNetSendFileChunk,
	MNetSendFileInformation,
	MNetSendGameHeader,
	MNetSendGamePlayerInfo,
	MNetSendGameResult,
	MNetSendMapHeader,
	MNetSendOrder,
	MNetSendReteamingInformation,
	MNetSendYOGMessage,
	MNetSetGameInRouter,
	MNetSetLatencyMode,
	MNetStartGame,
	MNetUpdateGameList,
	MNetUpdatePlayerList,
	MNetDownloadableMapInfos,
	MNetRequestDownloadableMapList,
	MNetRequestMapUpload,
	MNetAcceptMapUpload,
	MNetRefuseMapUpload,
	MNetCancelSendingFile,
	MNetCancelRecievingFile,
	MNetRequestMapThumbnail,
	MNetSendMapThumbnail,
	MNetSubmitRatingOnMap,
};
