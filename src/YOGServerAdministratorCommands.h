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

#ifndef YOGServerAdministratorCommand_h
#define YOGServerAdministratorCommand_h

#include <string>
#include <vector>
#include "boost/shared_ptr.hpp"

class YOGServerAdministrator;
class YOGServer;
class YOGServerPlayer;

///This defines a generic command
class YOGServerAdministratorCommand
{
public:
	virtual ~YOGServerAdministratorCommand() {}

	///Returns this YOGServerAdministratorCommand help message
	virtual std::string getHelpMessage()=0;
	
	///Returns the command name for this YOGServerAdministratorCommand
	virtual std::string getCommandName()=0;
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	virtual bool doesMatch(const std::vector<std::string>& tokens)=0;
	
	///Executes the code for the administrator command
	virtual void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player)=0;
};



///Shutsdown the server
class YOGServerRestart : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};



///Mutes a player
class YOGMutePlayer : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};



///Unmutes a player
class YOGUnmutePlayer : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};



///Resets a password
class YOGResetPassword : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};



///Bans a user
class YOGBanPlayer : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};



///Unbans a user
class YOGUnbanPlayer : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};



///Prints the list of banned players
class YOGShowBannedPlayers : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};



///Bans an IP
class YOGBanIP : public YOGServerAdministratorCommand
{
public:
	///Returns this YOGServerAdministratorCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerAdministratorCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerAdministratorCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, boost::shared_ptr<YOGServerPlayer> player);
};

#endif
