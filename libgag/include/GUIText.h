/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
		Text() { fontPtr=NULL; }
		Text(int x, int y, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text="", int w=0, int h=0);
		virtual ~Text() { }
		virtual void init(void);
		virtual void paint(GAGCore::DrawableSurface *gfx);
		virtual const char *getText() const { return text.c_str();}
		virtual void setText(const char *newText);
		virtual void setStyle(GAGCore::Font::Style style);
	};
}

#endif
