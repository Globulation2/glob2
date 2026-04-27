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


