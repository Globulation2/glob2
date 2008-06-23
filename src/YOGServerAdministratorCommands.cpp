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

#include "YOGServerAdministratorCommands.h"
#include "YOGServerAdministrator.h"
#include "YOGServer.h"
#include "NetMessage.h"
#include "YOGServerPlayer.h"
#include "boost/lexical_cast.hpp"

std::string YOGServerRestart::getHelpMessage()
{
	return ".server_restart    Hard resets the server";
}



std::string YOGServerRestart::getCommandName()
{
	return ".server_restart";
}



bool YOGServerRestart::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 1)
		return false;
	return true;
}
	


bool YOGServerRestart::allowedForModerator()
{
	return false;
}



void YOGServerRestart::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	exit(0);
}



std::string YOGMutePlayer::getHelpMessage()
{
	return ".mute_player <playername> <time>    Mutes a player for the given number of minutes. Default 10 if not included.";
}



std::string YOGMutePlayer::getCommandName()
{
	return ".mute_player";
}



bool YOGMutePlayer::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() == 2)
		return true;
	
	if(tokens.size() == 3)
	{
		try
		{
			boost::lexical_cast<int>(tokens[2]);
		}
		catch(boost::bad_lexical_cast& error)
		{
			return false;
		}
		return true;
	}
	return false;
}
	


bool YOGMutePlayer::allowedForModerator()
{
	return true;
}



void YOGMutePlayer::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	int time = 10;
	if(tokens.size() == 3)
		time = boost::lexical_cast<int>(tokens[2]);
	if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
	{
		boost::posix_time::ptime unmute_time = boost::posix_time::second_clock::local_time() + boost::posix_time::minutes(time);
		YOGPlayerStoredInfo i = server->getPlayerStoredInfoManager().getPlayerStoredInfo(name);
		i.setMuted(unmute_time);
		server->getPlayerStoredInfoManager().setPlayerStoredInfo(name, i);
		admin->sendTextMessage("Player muted: "+name, player);
	}
	else
	{
		admin->sendTextMessage("Could not find player: "+name, player);
	}
}



std::string YOGUnmutePlayer::getHelpMessage()
{
	return ".unmute_player <playername>    Unmutes a player";
}



std::string YOGUnmutePlayer::getCommandName()
{
	return ".unmute_player";
}



bool YOGUnmutePlayer::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGUnmutePlayer::allowedForModerator()
{
	return true;
}



void YOGUnmutePlayer::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
	{
		YOGPlayerStoredInfo i = server->getPlayerStoredInfoManager().getPlayerStoredInfo(name);
		i.setUnmuted();
		server->getPlayerStoredInfoManager().setPlayerStoredInfo(name, i);
		admin->sendTextMessage("Player unmuted: "+name, player);
	}
	else
	{
		admin->sendTextMessage("Could not find player: "+name, player);
	}
}



std::string YOGResetPassword::getHelpMessage()
{
	return ".reset_password <playername>    Resets the password for a player";
}



std::string YOGResetPassword::getCommandName()
{
	return ".reset_password";
}



bool YOGResetPassword::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGResetPassword::allowedForModerator()
{
	return false;
}



void YOGResetPassword::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	server->getServerPasswordRegistry().resetPlayersPassword(name);
	admin->sendTextMessage("Players password reset: "+name, player);
}



std::string YOGBanPlayer::getHelpMessage()
{
	return ".ban_player <playername>    Bans a player indefinitly";
}



std::string YOGBanPlayer::getCommandName()
{
	return ".ban_player";
}



bool YOGBanPlayer::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGBanPlayer::allowedForModerator()
{
	return false;
}



void YOGBanPlayer::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
	{
		YOGPlayerStoredInfo i = server->getPlayerStoredInfoManager().getPlayerStoredInfo(name);
		i.setBanned();
		server->getPlayerStoredInfoManager().setPlayerStoredInfo(name, i);
		boost::shared_ptr<YOGServerPlayer> nplayer = server->getPlayer(name);
		if(nplayer)
		{
			boost::shared_ptr<NetPlayerIsBanned> send(new NetPlayerIsBanned);
			nplayer->sendMessage(send);
			nplayer->closeConnection();
		}
		admin->sendTextMessage("Player banned: "+name, player);
	}
	else
	{
		admin->sendTextMessage("Could not find player: "+name, player);
	}
}



std::string YOGUnbanPlayer::getHelpMessage()
{
	return ".unban_player <playername>    Unbans a player";
}



std::string YOGUnbanPlayer::getCommandName()
{
	return ".unban_player";
}



bool YOGUnbanPlayer::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGUnbanPlayer::allowedForModerator()
{
	return false;
}



void YOGUnbanPlayer::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
	{
		YOGPlayerStoredInfo i = server->getPlayerStoredInfoManager().getPlayerStoredInfo(name);
		i.setUnbanned();
		server->getPlayerStoredInfoManager().setPlayerStoredInfo(name, i);
		admin->sendTextMessage("Player unbanned: "+name, player);
	}
	else
	{
		admin->sendTextMessage("Could not find player: "+name, player);
	}
}



std::string YOGShowBannedPlayers::getHelpMessage()
{
	return ".show_banned_players    Prints the list of banned players";
}



std::string YOGShowBannedPlayers::getCommandName()
{
	return ".show_banned_players";
}



bool YOGShowBannedPlayers::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 1)
		return false;
	return true;
}
	


bool YOGShowBannedPlayers::allowedForModerator()
{
	return false;
}



void YOGShowBannedPlayers::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::list<std::string> bannedPlayers = server->getPlayerStoredInfoManager().getBannedPlayers();
	std::string line;
	admin->sendTextMessage("Banned players: ", player);
	for(std::list<std::string>::iterator i=bannedPlayers.begin(); i!=bannedPlayers.end(); ++i)
	{
		line += *i;
		line += "        ";
	}
	if(!line.empty())
	{
		admin->sendTextMessage(line, player);
	}
}



std::string YOGBanIP::getHelpMessage()
{
	return ".ban_ip <playername>    Bans the players IP address for 24 hours";
}



std::string YOGBanIP::getCommandName()
{
	return ".ban_ip";
}



bool YOGBanIP::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGBanIP::allowedForModerator()
{
	return false;
}



void YOGBanIP::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	boost::shared_ptr<YOGServerPlayer> nplayer = server->getPlayer(name);
	if(nplayer)
	{
		boost::posix_time::ptime unban_time = boost::posix_time::second_clock::local_time() + boost::posix_time::hours(24);
		server->getServerBannedIPListManager().addBannedIP(nplayer->getPlayerIP(), unban_time);
		boost::shared_ptr<NetIPIsBanned> send(new NetIPIsBanned);
		nplayer->sendMessage(send);
		nplayer->closeConnection();
		admin->sendTextMessage("Player "+name+"'s IP "+nplayer->getPlayerIP()+" has been banned.", player);
	}
	else
	{
		admin->sendTextMessage("Player not logged in to ban: "+name, player);
	}
}



std::string YOGAddAdministrator::getHelpMessage()
{
	return ".add_administrator <playername>    Sets the player as an administrator";
}



std::string YOGAddAdministrator::getCommandName()
{
	return ".add_administrator";
}



bool YOGAddAdministrator::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGAddAdministrator::allowedForModerator()
{
	return false;
}



void YOGAddAdministrator::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	
	if(server->getAdministratorList().isAdministrator(name))
	{
		admin->sendTextMessage("Player "+name+" is already an admin.", player);
	}
	else
	{
		server->getAdministratorList().addAdministrator(name);
		admin->sendTextMessage("Player "+name+" is now an admin.", player);
	}
}



std::string YOGRemoveAdministrator::getHelpMessage()
{
	return ".remove_administrator <playername>    Removes administrator status from the player";
}



std::string YOGRemoveAdministrator::getCommandName()
{
	return ".remove_administrator";
}



bool YOGRemoveAdministrator::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGRemoveAdministrator::allowedForModerator()
{
	return false;
}



void YOGRemoveAdministrator::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	if(server->getAdministratorList().isAdministrator(name))
	{
		admin->sendTextMessage("Player "+name+" is no longer an admin.", player);
		server->getAdministratorList().removeAdministrator(name);
	}
	else
	{
		admin->sendTextMessage("Player "+name+" is not an admin.", player);
	}
}



std::string YOGAddModerator::getHelpMessage()
{
	return ".add_moderator <playername>    Adds moderator status to a player";
}



std::string YOGAddModerator::getCommandName()
{
	return ".add_moderator";
}



bool YOGAddModerator::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGAddModerator::allowedForModerator()
{
	return false;
}



void YOGAddModerator::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
	{
		YOGPlayerStoredInfo i = server->getPlayerStoredInfoManager().getPlayerStoredInfo(name);
		i.setModerator(true);
		server->getPlayerStoredInfoManager().setPlayerStoredInfo(name, i);
		admin->sendTextMessage("Player made moderator: "+name, player);
	}
	else
	{
		admin->sendTextMessage("Could not find player: "+name, player);
	}
}



std::string YOGRemoveModerator::getHelpMessage()
{
	return ".remove_moderator <playername>    Removes moderator status from a player";
}



std::string YOGRemoveModerator::getCommandName()
{
	return ".remove_moderator";
}



bool YOGRemoveModerator::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGRemoveModerator::allowedForModerator()
{
	return false;
}



void YOGRemoveModerator::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
	{
		YOGPlayerStoredInfo i = server->getPlayerStoredInfoManager().getPlayerStoredInfo(name);
		i.setModerator(false);
		server->getPlayerStoredInfoManager().setPlayerStoredInfo(name, i);
		admin->sendTextMessage("Player "+name+" had moderator status removed", player);
	}
	else
	{
		admin->sendTextMessage("Could not find player: "+name, player);
	}
}



std::string YOGRemoveMap::getHelpMessage()
{
	return ".remove_map <mapname>    Removes a map from the server respitory";
}



std::string YOGRemoveMap::getCommandName()
{
	return ".remove_map";
}



bool YOGRemoveMap::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() != 2)
		return false;
	return true;
}
	


bool YOGRemoveMap::allowedForModerator()
{
	return false;
}



void YOGRemoveMap::execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)
{
	std::string name = tokens[1];
	if(server->getMapDatabank().doesMapExist(name))
	{
		server->getMapDatabank().removeMap(name);
		admin->sendTextMessage("Map "+name+" has been removed", player);
	}
	else
	{
		admin->sendTextMessage("Could not find map: "+name, player);
	}
}

