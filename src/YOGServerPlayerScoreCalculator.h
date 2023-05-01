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

#ifndef YOGServerPlayerScoreCalculator_h
#define YOGServerPlayerScoreCalculator_h

#include "YOGGameResults.h"
#include "GameHeader.h"

class YOGServer;

//This class does the function of calculating and updating player scores
class YOGServerPlayerScoreCalculator
{
public:
	///Constructs the score calculator
	YOGServerPlayerScoreCalculator(YOGServer* server);

	///Processes the result of a single game
	void proccessResults(YOGGameResults& results, GameHeader& header);
private:
	YOGServer* server;
};

#endif
