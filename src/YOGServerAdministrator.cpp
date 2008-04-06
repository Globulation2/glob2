/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "YOGServerAdministrator.h"
#include "YOGServer.h"
#include "YOGServerPlayer.h"
#include "YOGMessage.h"
#include "NetMessage.h"

YOGServerAdministrator::YOGServerAdministrator(YOGServer* server)
	: server(server)
{

}
	

bool YOGServerAdministrator::executeAdministrativeCommand(const std::string& message, boost::shared_ptr<YOGServerPlayer> player)
{
	if(message.substr(0, 15)==".server_restart")
	{
		exit(0);
	}	
	else if(message.substr(0, 13) == ".mute_player ")
	{
		std::string name = message.substr(13, message.size());
		if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
		{
			boost::posix_time::ptime unmute_time = boost::posix_time::second_clock::local_time() + boost::posix_time::minutes(10);
			server->getPlayerStoredInfoManager().getPlayerStoredInfo(name).setMuted(unmute_time);
			sendTextMessage("Player muted: "+name, player);
		}
		else
		{
			sendTextMessage("Could not find player: "+name, player);
		}
	}
	else if(message.substr(0, 15) == ".unmute_player ")
	{
		std::string name = message.substr(15, message.size());
		if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
		{
			server->getPlayerStoredInfoManager().getPlayerStoredInfo(name).setUnmuted();
			sendTextMessage("Player unmuted: "+name, player);
		}
		else
		{	
			sendTextMessage("Could not find player: "+name, player);
		}
	}
	else if(message.substr(0, 16) == ".reset_password ")
	{
		std::string name = message.substr(16, message.size());
		server->getServerPasswordRegistry().resetPlayersPassword(name);
		sendTextMessage("Players password reset: "+name, player);
	}
	else if(message.substr(0, 5) == ".help")
	{
		sendTextMessage("The current list of YOG Administrative Commands are: ", player);
		sendTextMessage(".server_restart    Hard resets the server", player);
		sendTextMessage(".mute_player <playername>    Mutes a player for 10 minutes ", player);
		sendTextMessage(".unmute_player <playername>    Unmutes a player", player);
		sendTextMessage(".reset_password <playername>    Resets the password for a player", player);
		sendTextMessage(".help    Shows this help message", player);
	}

	return false;
}


void YOGServerAdministrator::sendTextMessage(const std::string& message, boost::shared_ptr<YOGServerPlayer> player)
{
	boost::shared_ptr<YOGMessage> m(new YOGMessage(message, "admin", YOGAdministratorMessage));
	boost::shared_ptr<NetSendYOGMessage> send(new NetSendYOGMessage(LOBBY_CHAT_CHANNEL, m));
	player->sendMessage(send);
}

