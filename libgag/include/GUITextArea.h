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

#ifndef __GUITEXTAREA_H
#define __GUITEXTAREA_H

#include "GUIBase.h"
#include <vector>
#include <string>
#include <map>

namespace GAGCore
{
	class Font;
	class Sprite;
}

namespace GAGGUI
{
	class TextArea:public HighlightableWidget
	{	
	protected:
		bool readOnly;
		std::string spritelocation;
		int spriteWidth;
		GAGCore::Font *font;
		size_t areaHeight;
		size_t areaPos;
		unsigned int charHeight;
		std::vector <size_t> lines;
		std::vector <int> lines_frames;
		std::vector <int> frames;
		std::string text;
		std::map<std::string, int> stringWidthCache;
		GAGCore::Sprite *sprite;
		
		// edit mod variables
		// this one is the only one always valid, other are recomputed from it
		size_t cursorPos;
		// this one can be invalid, but must be within textBufferLength
		size_t cursorPosY;
		// this one can be anything
		unsigned int cursorScreenPosY;
	
	public:
		TextArea() { font=NULL; }
		TextArea(const std::string &tooltip, const std::string &tooltipFont) : HighlightableWidget(tooltip, tooltipFont) { font=NULL; }
		TextArea(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, bool readOnly=true, const char *text="", const char *spritelocation=NULL);
		TextArea(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const std::string& tooltip, const std::string &tooltipFont, bool readOnly=true, const char *text="", const char *spritelocation=NULL);
		virtual ~TextArea();
	
		virtual void internalInit(void);
		virtual void paint(void);
	
		virtual void setText(const char *text);
		virtual const char *getText(void) { return text.c_str(); }
		virtual void addText(const char *text);
		// migration, const char * have to die at some point
		void addText(const std::string &text) { addText(text.c_str()); }
		virtual void addImage(int frame);
		virtual void addNoImage();
		virtual void addChar(const char c);
		virtual void remText(unsigned pos, unsigned len);
		virtual void scrollDown(void);
		virtual void scrollUp(void);
		virtual void scrollToBottom(void);
		virtual void setCursorPos(unsigned pos);
		//! load content from filename
		virtual bool load(const char *filename);
		//! save content to filename. If file exists, it is overriden
		virtual bool save(const char *filename);
	
	protected:
		//! Lookup a string in cache for its size
		int getStringWidth(const std::string &s);
		//! Create the strings index table from text
		virtual void layout(void);
		//! we make sure the repaint will show something correct
		virtual void compute(void);
		virtual void onSDLKeyDown(SDL_Event *event);
		virtual void onSDLMouseButtonDown(SDL_Event *event);
	};
}

#endif
