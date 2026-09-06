// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GUIBase.h"
#include <string>

namespace GAGCore
{
	class Sprite;
}

namespace GAGGUI
{
	class Selector: public RectangularWidget
	{
	protected:
		Uint32 maxValue;
		Uint32 step;
		Uint32 value;
		bool dragging;
		Sint32 id;
		std::string sprite;
		bool taper;
	
	
		//! cache, recomputed on internalInit
		GAGCore::Sprite *archPtr;
	
	public:
		Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned width, unsigned defaultValue=0, unsigned maxValue=16, bool taper=false, unsigned step=1, const std::string sprite="", Sint32 id=-1);
		Selector(int x, int y, Uint32 hAlign, Uint32 vAlign, unsigned width, const std::string& tooltip, const std::string &tooltipFont, unsigned defaultValue=0, unsigned maxValue=16, bool taper=false, unsigned step=1, const std::string sprite="", Sint32 id=-1);
		virtual ~Selector() { }
	
		virtual void internalInit(void);
		virtual void paint(void);
		virtual Uint32 getValue(void) { return value; }
		virtual void setValue(Uint32 v) { clipValue(v); }
	
	protected:
		void clipValue(int v);
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseMotion(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
	};
}
