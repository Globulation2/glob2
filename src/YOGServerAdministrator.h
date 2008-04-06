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

#ifndef __YOGServerAdministrator_h
#define __YOGServerAdministrator_h

#include <string>
#include "boost/shared_ptr.hpp"

class YOGServer;
class YOGServerPlayer;

///This governs the system of administrative commands to the YOG server
class YOGServerAdministrator
{
public:
	///Constructs the administration engine
	YOGServerAdministrator(YOGServer* server);
	
	///Interprets whether the given message is an administrative command,
	///and if so, executes it. If it was, returns true, otherwise, returns
	///false
	bool executeAdministrativeCommand(const std::string& message, boost::shared_ptr<YOGServerPlayer> player);

private:
	///This sends a message to the player from the administrator engine
	void sendTextMessage(const std::string& message, boost::shared_ptr<YOGServerPlayer> player);

	YOGServer* server;
};

#endif
