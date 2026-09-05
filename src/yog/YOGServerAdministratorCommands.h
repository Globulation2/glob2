// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

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

	///Returns true if this command can be executed by both moderators and administrators, false if it can only be executed by administrators
	virtual bool allowedForModerator()=0;

	///Executes the code for the administrator command
	virtual void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player)=0;

	///Returns true if the token count is within this command's accepted range.
	bool doesMatch(std::size_t count) const
	{
		return int(count) >= minTokens && int(count) <= maxTokens;
	}

protected:
	explicit YOGServerAdministratorCommand(int fixedTokens) : minTokens(fixedTokens), maxTokens(fixedTokens) {}
	YOGServerAdministratorCommand(int min, int max) : minTokens(min), maxTokens(max) {}

private:
	int minTokens;
	int maxTokens;
};



///Shutsdown the server
class YOGServerRestart : public YOGServerAdministratorCommand
{
public:
	YOGServerRestart() : YOGServerAdministratorCommand(1) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Mutes a player
class YOGMutePlayer : public YOGServerAdministratorCommand
{
public:
	YOGMutePlayer() : YOGServerAdministratorCommand(2, 3) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Unmutes a player
class YOGUnmutePlayer : public YOGServerAdministratorCommand
{
public:
	YOGUnmutePlayer() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Resets a password
class YOGResetPassword : public YOGServerAdministratorCommand
{
public:
	YOGResetPassword() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Bans a user
class YOGBanPlayer : public YOGServerAdministratorCommand
{
public:
	YOGBanPlayer() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Unbans a user
class YOGUnbanPlayer : public YOGServerAdministratorCommand
{
public:
	YOGUnbanPlayer() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Prints the list of banned players
class YOGShowBannedPlayers : public YOGServerAdministratorCommand
{
public:
	YOGShowBannedPlayers() : YOGServerAdministratorCommand(1) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///Bans an IP
class YOGBanIP : public YOGServerAdministratorCommand
{
public:
	YOGBanIP() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This adds an administrator
class YOGAddAdministrator : public YOGServerAdministratorCommand
{
public:
	YOGAddAdministrator() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This removes an administrator
class YOGRemoveAdministrator : public YOGServerAdministratorCommand
{
public:
	YOGRemoveAdministrator() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This adds a moderator
class YOGAddModerator : public YOGServerAdministratorCommand
{
public:
	YOGAddModerator() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This removes a moderator
class YOGRemoveModerator : public YOGServerAdministratorCommand
{
public:
	YOGRemoveModerator() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};



///This deletes a map
class YOGRemoveMap : public YOGServerAdministratorCommand
{
public:
	YOGRemoveMap() : YOGServerAdministratorCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	bool allowedForModerator();
	void execute(YOGServer* server, YOGServerAdministrator* admin, const std::vector<std::string>& tokens, std::shared_ptr<YOGServerPlayer> player);
};
