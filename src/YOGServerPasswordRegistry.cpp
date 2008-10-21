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

#include "YOGServerPasswordRegistry.h"
#include "Stream.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include "FileManager.h"
#include "../gnupg/sha1.c"
#include "Version.h"

#include <boost/lexical_cast.hpp>

#include <iostream>

using namespace GAGCore;

YOGServerPasswordRegistry::YOGServerPasswordRegistry()
{
	readPasswords();
	invalidChars="!@#$%^&*()-+={}[]:\";'>?./<,|\\ \n\t\r";
}



YOGLoginState YOGServerPasswordRegistry::verifyLoginInformation(const std::string& username, const std::string& password)
{
	if(passwords[username] == transform(username, password))
		return YOGLoginSuccessful;
	else if(passwords[username] == "")
		return YOGUserNotRegistered;
	return YOGPasswordIncorrect;
}



YOGLoginState YOGServerPasswordRegistry::registerInformation(const std::string& username, const std::string& password)
{
	if(passwords[username] != "")
		return YOGUsernameAlreadyUsed;
	
	for(int i=0; i<invalidChars.size(); ++i)
		if(username.find(invalidChars[i]) != std::string::npos)
			return YOGNameInvalidSpecialCharacters;
	
		
	passwords[username] = transform(username, password);
	flushPasswords();
	return YOGLoginSuccessful;
}



void YOGServerPasswordRegistry::resetPlayersPassword(const std::string& username)
{
	passwords[username] = "";
	flushPasswords();
}



void YOGServerPasswordRegistry::flushPasswords()
{
	OutputStream* stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(YOG_SERVER_FOLDER+"registry"));
	stream->writeUint32(VERSION_MINOR, "version");
	stream->writeUint32(passwords.size(), "size");
	for(std::map<std::string, std::string>::iterator i = passwords.begin(); i!=passwords.end(); ++i)
	{
		stream->writeText(i->second, "password");
		stream->writeText(i->first, "username");
	}
	delete stream;
}



void YOGServerPasswordRegistry::readPasswords()
{
	InputStream* stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(YOG_SERVER_FOLDER+"registry"));
	if(stream->isEndOfStream())
		return;
	Uint32 dataVersionMinor = stream->readUint32("version");
	Uint32 size = stream->readUint32("size");
	for(unsigned i=0; i<size; ++i)
	{
		std::string p = stream->readText("password");
		std::string u = stream->readText("username");
		passwords[u] = p;
	}
	delete stream;
}



std::string YOGServerPasswordRegistry::transform(const std::string& username, const std::string& password)
{
	///Create the to-be hashed string, this can really be any whacky combination but really
	///where just trying to reach a minimum length
	std::string salted = username + password;
	int i=1;
	while(salted.size() < 50)
	{
		salted+=boost::lexical_cast<std::string>(i);
		i+=1;
	}
	///Perform SHA1, a cast must be performed to get the data to the right type, but
	///it doesn't make a difference for the result
	SHA1_CTX context;
	SHA1Init(&context);
	SHA1Update(&context, (const unsigned char*)(salted.c_str()), salted.size());
	unsigned char digest[20];
	SHA1Final(digest, &context);
	std::string final = "";
	for(int i=0; i<20; ++i)
		final += boost::lexical_cast<std::string>(digest[i]) + "-";
	return final;
}
