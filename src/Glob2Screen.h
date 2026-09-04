// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GLOB2_SCREEN_H
#define __GLOB2_SCREEN_H

#include <GUIBase.h>
#include <GUITabScreen.h>

using namespace GAGCore;
using namespace GAGGUI;

class Glob2Screen : public Screen
{
public:
	Glob2Screen();
	virtual ~Glob2Screen();
	virtual void paint(void);
	
private:
	unsigned getNextTerrain(void);
	int randomSeed;
};

class Glob2TabScreen : public TabScreen
{
public:
	Glob2TabScreen(bool fullScreen, bool longerButtons=false);
	virtual ~Glob2TabScreen();
	virtual void paint(void);
	
private:
	unsigned getNextTerrain(void);
	int randomSeed;
};

#endif

