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

#ifndef YOGClientCommandManager_h
#define YOGClientCommandManager_h

#include <string>
#include <vector>

class YOGClient;
class YOGClientCommand;

///This manages client commands, like /block
class YOGClientCommandManager
{
public:
	YOGClientCommandManager(YOGClient* client);

	///Destroys the administration engine
	~YOGClientCommandManager();

	///Interprets whether the given message is a client command, and if so
	///executes it. If it wasn't a command, the string this returns will be
	///empty
	std::string executeClientCommand(const std::string& message);

private:
	YOGClient* client;
	std::vector<YOGClientCommand*> commands;
};


#endif
