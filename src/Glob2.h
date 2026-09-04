// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GLOB2_H
#define __GLOB2_H

//! This class is used to handle the whole game
class Glob2
{
	static const bool verbose = false;
public:
	//! true while the game is running
	bool isRunning;

public:
	void drawYOGSplashScreen();
	void mutiplayerYOG();
	int runNoX();
	///Runs random games non stop until the game crashes
	int runTestGames();
	///Generates random maps non stop until the game crashes
	int runTestMapGeneration();
	int run(int argc, char *argv[]);
};

#endif
