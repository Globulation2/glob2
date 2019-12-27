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

#ifndef YOGServerRouterAdministrator_h
#define YOGServerRouterAdministrator_h

#include <string>
#include "boost/shared_ptr.hpp"
#include <vector>

class YOGServerRouter;
class YOGServerRouterPlayer;
class YOGServerRouterAdministratorCommand;

///This governs the system of administrative commands to the YOG server
class YOGServerRouterAdministrator
{
public:
	///Constructs the administration engine
	YOGServerRouterAdministrator(YOGServerRouter* router);

	///Destroys the administration engine
	~YOGServerRouterAdministrator();

	///Interprets whether the given message is an administrative command,
	///and if so, executes it. If it was, returns true, otherwise, returns
	///false
	bool executeAdministrativeCommand(const std::string& message, YOGServerRouterPlayer* player);

	///This sends a message to the player from the administrator engine
	void sendTextMessage(const std::string& message, YOGServerRouterPlayer* admin);

	///Flushes the text
	void flushTexts(YOGServerRouterPlayer* admin);

private:
	YOGServerRouter* router;
	std::vector<YOGServerRouterAdministratorCommand*> commands;
	std::string allText;
};

#endif
