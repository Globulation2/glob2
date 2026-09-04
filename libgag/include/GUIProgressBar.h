// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GUIPROGRESS_BAR_H
#define __GUIPROGRESS_BAR_H

#include "GUIBase.h"
#include "GraphicContext.h"

namespace GAGGUI
{
	//! This widget displays a progress bar
	class ProgressBar: public RectangularWidget
	{
	protected:
		int value; //!< current value, between 0 and range
		int range; //!< value should be between 0 and range
		std::string font; //!< the name of the used font
		std::string format; //!< the format used for text replacement
		
		GAGCore::Font *fontPtr; //!< pointer to font, this is a cache
		
	public:
		ProgressBar(int x, int y, int w, Uint32 hAlign, Uint32 vAlign, int range = 100, int value = 0, const char* font = 0, std::string format = "%0");
		virtual ~ProgressBar() { }
		
		void setValue(int value) { this->value = value; }
		
		virtual void internalInit(void);
		virtual void paint(void);
	};
}

#endif
