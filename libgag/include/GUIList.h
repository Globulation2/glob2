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

#ifndef __GUILIST_H
#define __GUILIST_H

#include "GUIBase.h"
#include <vector>
#include <string>

namespace GAGCore
{
	class Font;
}

namespace GAGGUI
{
	class List: public HighlightableWidget
	{
	protected:
		std::string font;
		std::vector<std::string> strings;
		Sint32 nth;
		size_t disp;
	
		//! Cached variables, do not serialise, reconstructed on paint() call
		//! Length of the scroll box, this is a cache
		unsigned blockLength, blockPos, textHeight;
		GAGCore::Font *fontPtr;
	
	public:
		List() { fontPtr = NULL; }
		List(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font);
		virtual ~List();
	
		virtual void onTimer(Uint32 tick) { }
		virtual void onSDLEvent(SDL_Event *event);
		virtual void init(void);
		virtual void paint(void);
	
		void addText(const char *text, int pos);
		void addText(const char *text);
		void removeText(int pos);
		bool isText(const char *text) const;
		void clear(void);
		const char *getText(int pos) const;
		const char *get(void) const;
		//! Sorts the list (override it if you don't like it)
		virtual void sort(void);
	
		//! Called when selection changes (default: signal parent)
		virtual void selectionChanged();
	
		int getNth(void) const;
		void setNth(int nth);
	};
}

#endif

