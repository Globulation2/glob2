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

#ifndef __YOGServerGameLog_h
#define __YOGServerGameLog_h

#include "YOGGameResults.h"
#include "boost/date_time/posix_time/posix_time.hpp"
#include "SDL_net.h"

///This class keeps a complete list of games played
class YOGServerGameLog
{
public:
	///Constructs the game log
	YOGServerGameLog();

	///Adds a game result to the log
	void addGameResults(YOGGameResults results);
	
	///Updates this game log, periodically saving and changing the log file
	void update();
private:
	///This saves the game log
	void save();
	///This loads the game log
	void load();
	///This is the current hour
	boost::posix_time::ptime hour;
	///This is the list of games from this hour
	std::vector<YOGGameResults> games;
	///This is the next time the list will be flushed
	boost::posix_time::ptime flushTime;
	///This is set when the list has changed
	bool modified;
};

#endif
