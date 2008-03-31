/*
  Copyright (C) 2007 Bradley Arsenault

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
};


#endif
