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

YOGServerAdministrator::YOGServerAdministrator(YOGServer* server)
	: server(server)
{

}
	

bool YOGServerAdministrator::executeAdministrativeCommand(const std::string& message)
{
	if(message=="server_restart")
		exit(0);
		
	if(message.substr(0, 12) == "mute_player ")
	{
		std::string name = message.substr(12, message.size());
		if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
			server->getPlayerStoredInfoManager().getPlayerStoredInfo(name).setMuted();
	}
		
	if(message.substr(0, 14) == "unmute_player ")
	{
		std::string name = message.substr(14, message.size());
		if(server->getPlayerStoredInfoManager().doesStoredInfoExist(name))
			server->getPlayerStoredInfoManager().getPlayerStoredInfo(name).setUnmuted();
	}
		
	if(message.substr(0, 15) == "reset_password ")
	{
		std::string name = message.substr(15, message.size());
		server->getServerPasswordRegistry().resetPlayersPassword(name);
	}

	return false;
}
