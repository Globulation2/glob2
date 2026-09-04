// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGServerAdministratorCommand_h
#define YOGServerAdministratorCommand_h

#include <string>
#include <vector>
#include <memory>

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
	
	///Returns true if this command can be executed by both moderators and administrators, false if it can only be executed by administrators
	virtual bool allowedForModerator()=0;
	
	///Executes the code for the administrator command
	virtual void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player)=0;
};



///Shutsdown the server
class YOGServerRestart : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();

	std::string getCommandName();

	bool doesMatch(const std::vector<std::string>& tokens);

	bool allowedForModerator();

	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Mutes a player
class YOGMutePlayer : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Unmutes a player
class YOGUnmutePlayer : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Resets a password
class YOGResetPassword : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Bans a user
class YOGBanPlayer : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Unbans a user
class YOGUnbanPlayer : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Prints the list of banned players
class YOGShowBannedPlayers : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Bans an IP
class YOGBanIP : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This adds an administrator
class YOGAddAdministrator : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This removes an administrator
class YOGRemoveAdministrator : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This adds a moderator
class YOGAddModerator : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This removes a moderator
class YOGRemoveModerator : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This deletes a map
class YOGRemoveMap : public YOGServerAdministratorCommand
{
public:
	std::string getHelpMessage();
	
	std::string getCommandName();
	
	bool doesMatch(const std::vector<std::string>& tokens);
	
	bool allowedForModerator();
	
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};

#endif
