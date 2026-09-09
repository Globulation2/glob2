// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

//! This class is used to handle the whole game
class Glob2
{
	static const bool verbose = false;
public:
	//! true while the game is running
	bool isRunning;

public:
	void drawYOGSplashScreen();
	void multiplayerYOG();
	int runNoX();
	///Runs random games non stop until the game crashes
	int runTestGames();
	///Runs four-player Nicowar version comparison games non stop
	int runNicowarVersionTestGames();
	///Runs one deterministic Nicowar tournament worker match
	int runNicowarTournamentMatch();
	int runNicowar2v2TournamentMatch();
	int runNicowarScenarioMatch();
	int runMaximaCastorMatch();
	///Prints the maps eligible for Nicowar tournaments
	int listNicowarTournamentMaps();
	int listNicowarScenarioMaps();
	///Generates random maps non stop until the game crashes
	int runTestMapGeneration();
	int run(int argc, char *argv[]);
};

