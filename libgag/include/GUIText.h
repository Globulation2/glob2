/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

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
