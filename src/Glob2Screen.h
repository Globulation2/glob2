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
	// Scheduled and blocking menus share the same presentation.
	FrontendScope theme{true};
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
	FrontendScope theme{true};
};


