// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

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

