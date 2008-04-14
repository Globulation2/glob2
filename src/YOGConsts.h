/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __YOG_CONSTS_H
#define __YOG_CONSTS_H

#include <string>
#include "SDL_net.h"

///New YOG constants
extern const Uint16 YOG_SERVER_PORT;
extern const std::string YOG_SERVER_IP;
//const std::string YOG_SERVER_IP = "127.0.0.1";

#define IRC_CHAN "#glob2"
#define IRC_SERVER "irc.globulation2.org"

///This is the chat channel of the main lobby
const Uint32 LOBBY_CHAT_CHANNEL=0;

///Policies for login
enum YOGLoginPolicy
{
	///This represents an unknown policy. Used by the client
	///before the policy information has been transferred across the network
	YOGUnknownLoginPolicy,
	///This policy demands a password with the login
	YOGRequirePassword,
	///This policy allows anonymous logins.
	YOGAnonymousLogin,
};

///Policies for the games the server hosts
enum YOGGamePolicy
{
	///This represents an unknown policy. Used by the client
	///before the policy information has been transferred across the network
	YOGUnknownGamePolicy,
	///In this policy, the server hosts one game, and all connected are a part of it.
	YOGSingleGame,
	///In this policy, the server hosts multiple games, and users can choose what game
	///they are a part of
	YOGMultipleGames,
};

///Represents the state of a login. Whether it was accepted, or if it
///was refused, and if so, for what reason.
enum YOGLoginState
{
	///This represents when the login was successful.
	YOGLoginSuccessful,
	///This represents when the login state is unknown
	YOGLoginUnknown,
	///This means that the password was incorrect.
	///(only for servers that require registration)
	YOGPasswordIncorrect,
	///This means that a user with the same username is already registered.
	YOGUsernameAlreadyUsed,
	///This means that no registered user with that username exists
	///(only for servers that require registration)
	YOGUserNotRegistered,
	///This means that the clients version is too old, they must update
	YOGClientVersionTooOld,
	///This means that this username is already logged in
	YOGAlreadyAuthenticated,
	///This means that this username is banned
	YOGUsernameBanned,
};

///This represents the reason why the player could not join a game.
enum YOGServerGameJoinRefusalReason
{
	///This represents internally an unknown reason
	YOGJoinRefusalUnknown,
	///This occurs when the game has started already
	YOGServerGameHasAlreadyStarted,
	///This occurs when the game has the maximum number of players
	YOGServerGameIsFull,
	///This represents the game not existing, it may have been closed
	///in the time that the message took to arrive at the server
	YOGServerGameDoesntExist,
};

///This represents the reason why the player could not join a game.
enum YOGServerGameCreateRefusalReason
{
	///This represents internally an unknown reason
	YOGCreateRefusalUnknown,
};

///This is used to represent the types of messages that can be sent through YOG
enum YOGMessageType
{
	///This means a normal message sent to all connected users
	YOGNormalMessage,
	///This means a private message sent to one user
	YOGPrivateMessage,
	///This means an administrator message sent by YOG
	YOGAdministratorMessage,
	///This is a message sent only to members of the same game
	YOGServerGameMessage,
};

///This is used to represent the various reasons a player may be
///removed from a game.
enum YOGKickReason
{
	///This means the host has disconnected and all players must quit
	YOGHostDisconnect,
	///This means that the host has kicked the player
	YOGKickedByHost,
	///This represents an unknown reason
	YOGUnknownKickReason,
};

///This is used to represent the various reasons the server may refuse to start
///a game.
enum YOGServerGameStartRefusalReason
{
	///This represents an unknown reason
	YOGUnknownStartRefusalReason,
	///This means the host has disconnected and all players must quit
	YOGNotAllPlayersReady,
};


///This is used to represent the a players success state after a game,
enum YOGGameResult
{
	///This represents when the player in question has won the game
	YOGGameResultWonGame,
	///This resprsents when the player in question has lost the game
	YOGGameResultLostGame,
	///This represents when the player in question has quit its last game
	YOGGameResultQuitGame,
	///This reprsents when the player in question lost connection
	YOGGameResultConnectionLost,
	///This represents when the game result is unknown
	YOGGameResultUnknown,
};

#endif 
