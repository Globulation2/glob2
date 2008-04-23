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

#ifndef YOGServerRouterAdministratorCommand_h
#define YOGServerRouterAdministratorCommand_h

#include <string>
#include <vector>
#include "boost/shared_ptr.hpp"

class YOGServerRouterAdministrator;
class YOGServerRouter;
class YOGServerRouterPlayer;

///This defines a generic command
class YOGServerRouterAdministratorCommand
{
public:
	virtual ~YOGServerRouterAdministratorCommand() {}

	///Returns this YOGServerRouterAdministratorCommand help message
	virtual std::string getHelpMessage()=0;
	
	///Returns the command name for this YOGServerRouterAdministratorCommand
	virtual std::string getCommandName()=0;
	
	///Returns true if the given set of tokens match whats required for this YOGServerRouterAdministratorCommand
	virtual bool doesMatch(const std::vector<std::string>& tokens)=0;
	
	///Executes the code for the administrator command
	virtual void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player)=0;
};

///This defines a generic command
class YOGServerRouterShutdownCommand : public YOGServerRouterAdministratorCommand
{
public:
	///Returns this YOGServerRouterShutdownCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerRouterShutdownCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerRouterShutdownCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player);
};

#endif
