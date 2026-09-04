// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerAdministratorList.h"

#include "Stream.h"
#include "Toolkit.h"
#include "FileManager.h"
#include <iostream>
#include "YOGConsts.h"

using namespace GAGCore;

YOGServerAdministratorList::YOGServerAdministratorList()
{
	load();
}


	
bool YOGServerAdministratorList::isAdministrator(const std::string& playerName)
{
	if(admins.find(playerName) != admins.end())
		return true;
	return false;
}



void YOGServerAdministratorList::addAdministrator(const std::string& playerName)
{
	admins.insert(playerName);
	save();
}



void YOGServerAdministratorList::removeAdministrator(const std::string& playerName)
{
	if(admins.find(playerName)!=admins.end())
		admins.erase(admins.find(playerName));
	save();
}



void YOGServerAdministratorList::save()
{
	OutputLineStream* stream = new OutputLineStream(Toolkit::getFileManager()->openOutputStreamBackend(YOG_SERVER_FOLDER+"admins.txt"));
	for(std::set<std::string>::iterator i=admins.begin(); i!=admins.end(); ++i)
	{
		if(*i != "")
		{
			stream->writeLine(*i);
		}
	}
	delete stream;
}



void YOGServerAdministratorList::load()
{
	InputLineStream* stream = new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend(YOG_SERVER_FOLDER+"admins.txt"));
	while(!stream->isEndOfStream())
	{
		std::string name = stream->readLine();
		admins.insert(name);
	}
	delete stream;
}


