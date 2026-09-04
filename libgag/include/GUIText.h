// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GUITEXT_H
#define __GUITEXT_H

#include "GUIBase.h"
#include "GraphicContext.h"
#include <string>

namespace GAGGUI
{
	//! This widget is a simple text widget
	class Text: public RectangularWidget
	{
	protected:
		std::string font;
		std::string text;
		bool keepW;
		bool keepH;
		GAGCore::Font::Style style;
	
		// cache, recomputed at least on paint
		GAGCore::Font *fontPtr;
		
	public:
		Text() { fontPtr = NULL; }
		Text(const std::string &tooltip, const std::string &tooltipFont) : RectangularWidget(tooltip, tooltipFont) { fontPtr = NULL; }

		Text(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text="", int w=0, int h=0) { constructor(x, y, hAlign, vAlign, font, text, w, h); }
		Text(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string &text, const std::string &tooltip, const std::string &tooltipFont, int w=0, int h=0) 
			: RectangularWidget(tooltip, tooltipFont)
				{ constructor(x, y, hAlign, vAlign, font, text.c_str(), w, h); }

		virtual ~Text() { }
		
		virtual void internalInit(void);
		virtual void paint(void);
		virtual const std::string getText() const { return text; }
		virtual void setText(const std::string newText);
		virtual void setStyle(GAGCore::Font::Style style);
		
	protected:
		void constructor(int x, int y, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, int w, int h);
	};
}

#endif
