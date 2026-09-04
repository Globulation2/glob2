// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGServerPasswordRegistry_h
#define __YOGServerPasswordRegistry_h

#include "YOGConsts.h"
#include <map>
#include <string>

///This classes purpose is to store and validate usernames and passwords
///for the server
class YOGServerPasswordRegistry
{
public:
	///Constructs the password registry by loading from the passwords file
	YOGServerPasswordRegistry();

	///Verifies that the information is correct
	YOGLoginState verifyLoginInformation(const std::string& username, const std::string& password); 

	///Registers a user with the given information
	YOGLoginState registerInformation(const std::string& username, const std::string& password); 

	///This resets a players password
	void resetPlayersPassword(const std::string& username);
	
private:
	///Writes the passwords and usernames to a text file
	void flushPasswords();
	///Reads the passwords and usernames from a text file
	void readPasswords();
	///This performs a one way transformation (whatever it be) on the given username and password
	///for security reasons. Most likely to be a hash of some sort
	std::string transform(const std::string& username, const std::string& password);
	std::map<std::string, std::string> passwords;
	std::string invalidChars;
};


#endif
