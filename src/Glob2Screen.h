// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GUIBase.h>
#include <GUITabScreen.h>
#include "FrontendTheme.h"

using namespace GAGCore;
using namespace GAGGUI;

class Glob2Screen : public Screen
{
public:
	Glob2Screen();
	virtual ~Glob2Screen();
	void paint(void) override;
	int execute(GAGCore::DrawableSurface* gfx, int stepLength) override;

private:
	// execute() only covers the old blocking loop; screens driven through
	// ScreenStack::push() (browser cooperative scheduling) never call it, so
	// they'd never get the theme's widget colors otherwise. A member (alive
	// for the whole screen, not just one paint() call) covers both paths,
	// same as GameSessionScreen/MapEditorScreen/CampaignEditor already do to
	// opt out. Declared first so it's active before anything else runs.
	FrontendScope theme;
	unsigned getNextTerrain(void);
	Uint32 randomSeed; // Background LCG intentionally wraps modulo 2^32.
};

class Glob2TabScreen : public TabScreen
{
public:
	Glob2TabScreen(bool fullScreen, bool longerButtons=false);
	virtual ~Glob2TabScreen();
	void paint(void) override;
	int execute(GAGCore::DrawableSurface* gfx, int stepLength) override;

private:
	// See Glob2Screen for why this needs to be a long-lived member.
	FrontendScope theme;
	unsigned getNextTerrain(void);
	Uint32 randomSeed; // Background LCG intentionally wraps modulo 2^32.
};


