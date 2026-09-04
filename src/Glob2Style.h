// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GLOB2_STYLE_H
#define __GLOB2_STYLE_H

#include <GUIStyle.h>

using namespace GAGCore;
using namespace GAGGUI;

class Glob2Style : public Style
{
public:
	Glob2Style();
	~Glob2Style();

protected:
	virtual void drawTextButtonBackground(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
	virtual void drawFrame(GAGCore::DrawableSurface *target, int x, int y, int w, int h, unsigned highlight);
	virtual void drawScrollBar(GAGCore::DrawableSurface *target, int x, int y, int w, int h, int blockPos, int blockLength);
	virtual void drawProgressBar(GAGCore::DrawableSurface *target, int x, int y, int w, int value, int range);
	
	virtual int getStyleMetric(StyleMetrics metric);
	
protected:
	Sprite *sprite;
};

#endif
