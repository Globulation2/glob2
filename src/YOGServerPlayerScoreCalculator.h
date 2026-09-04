// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

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

	///Proccesses the result of a single game
	void proccessResults(YOGGameResults& results, GameHeader& header);
private:
	YOGServer* server;
};

#endif
