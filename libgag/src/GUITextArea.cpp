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

#include <GUITextArea.h>
#include <GUIStyle.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <StreamBackend.h>
#include <FileManager.h>
#include <assert.h>
#include <iostream>
#include <algorithm>
#include <valarray>

using namespace GAGCore;

namespace GAGGUI
{
	TextArea::TextArea(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, bool readOnly, const char *text, const char *spritelocation)
	{
		this->x = x;
		this->y = y;
		this->w = w;
		this->h = h;
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;
		
		this->readOnly = readOnly;
		this->sprite = NULL;
		this->spriteWidth = 0;
		// TODO : clean this and store text font
		this->font = Toolkit::getFont(font);
		assert(this->font);
		assert(font);
		charHeight = this->font->getStringHeight((const char *)NULL);
		assert(charHeight);
		areaHeight = (h-8)/charHeight;
		areaPos=0;
		
		cursorPos = 0;
		cursorPosY = 0;
		cursorScreenPosY = 0;
		
		this->text = text;
		
		if (spritelocation)
			sprite = Toolkit::getSprite(spritelocation);
		if (sprite)
			spriteWidth = sprite->getW(0);	
	}
	
	TextArea::TextArea(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const std::string &tooltip, const std::string &tooltipFont, bool readOnly, const char *text, const char *spritelocation)
		: HighlightableWidget(tooltip, tooltipFont)
	{
		this->x = x;
		this->y = y;
		this->w = w;
		this->h = h;
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;
		
		this->readOnly = readOnly;
		this->sprite = NULL;
		this->spriteWidth = 0;
		// TODO : clean this and store text font
		this->font = Toolkit::getFont(font);
		assert(this->font);
		assert(font);
		charHeight = this->font->getStringHeight((const char *)NULL);
		assert(charHeight);
		areaHeight = (h-8)/charHeight;
		areaPos=0;
		
		cursorPos = 0;
		cursorPosY = 0;
		cursorScreenPosY = 0;
		
		this->text = text;
		
		if (spritelocation)
			sprite = Toolkit::getSprite(spritelocation);
		if (sprite)
			spriteWidth = sprite->getW(0);	
	}
	
	TextArea::~TextArea(void)
	{
		
	}
	
	void TextArea::onSDLKeyDown(SDL_Event *event)
	{
		assert(event->type == SDL_KEYDOWN);
		SDLKey sym=event->key.keysym.sym;
		SDLMod mod=event->key.keysym.mod;
		
		switch (sym)
		{
			case SDLK_DELETE:
			if (!readOnly)
			{
				if (cursorPos < text.length())
				{
					size_t len=getNextUTF8Char(text.c_str(), cursorPos);
					remText(cursorPos, len-cursorPos);
					parent->onAction(this, TEXT_MODIFIED, 0, 0);
				}
			}
			break;
			
			case SDLK_BACKSPACE:
			if (!readOnly)
			{
				if (cursorPos)
				{
					size_t newPos=getPrevUTF8Char(text.c_str(), cursorPos);
					size_t len=cursorPos-newPos;
					cursorPos=newPos;
					remText(newPos, len);
					parent->onAction(this, TEXT_MODIFIED, 0, 0);
				}
			}
			break;
			
			case SDLK_HOME:
			{
				if (SDL_GetModState() & KMOD_CTRL)
				{
					cursorPos=0;
				}
				else
				{
					cursorPos=lines[cursorPosY];
				}
				compute();
				parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
			}
			break;
			
			case SDLK_END:
			{
				if (SDL_GetModState() & KMOD_CTRL)
				{
					cursorPos=text.length();
				}
				else
				{
					if (cursorPosY<lines.size()-1)
						cursorPos=lines[cursorPosY+1]-1;
					else
						cursorPos=text.length();
				}
				compute();
				parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
			}
			break;
			
			case SDLK_PAGEUP:
			{
				if (areaPos>0)
				{
					assert(lines.size()>areaHeight);
					
					// compute new areaPos
					areaPos-=MIN(areaPos, areaHeight);

					// if in edit mode, replace cursor
					if (!readOnly)
					{
						// TODO : UTF8 clean cursor displacement in text
						size_t cursorPosX=cursorPos-lines[cursorPosY];
						size_t newPosY=cursorPosY>areaPos+areaHeight-2 ? areaPos+areaHeight-2 : cursorPosY;
						if (newPosY!=cursorPosY)
						{
							size_t newLineLen=lines[cursorPosY]-lines[newPosY];

							if (cursorPosX<newLineLen)
							{
								cursorPos=lines[newPosY]+cursorPosX;
							}
							else
							{
								cursorPos=lines[newPosY]+newLineLen-1;
							}

							parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
						}
					}

					compute();
				}
			}
			break;
			
			case SDLK_PAGEDOWN:
			{
				if (lines.size()>=areaHeight)
				{
					if (areaPos<lines.size()-areaHeight)
					{
						// compute new areaPos
						areaPos+=std::min(lines.size()-areaHeight-areaPos, areaHeight);
						
						// if in edit mode, replace cursor
						if (!readOnly)
						{
							// TODO : UTF8 clean cursor displacement in text
							size_t cursorPosX=cursorPos-lines[cursorPosY];
							size_t newPosY=cursorPosY<areaPos+1 ? areaPos+1 : cursorPosY;
							if (newPosY!=cursorPosY)
							{
								size_t newLineLen;
								if (newPosY==lines.size()-1)
								{
									newLineLen=text.length()-lines[newPosY];
								}
								else
								{
									newLineLen=lines[newPosY+1]-lines[newPosY]-1;
								}
								
								if (cursorPosX<newLineLen)
								{
									cursorPos=lines[newPosY]+cursorPosX;
								}
								else
								{
									cursorPos=lines[newPosY]+newLineLen-1;
								}
								
								parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
							}
						}
						
						compute();
					}
				}
			}
			break;
			
			case SDLK_UP:
			{
				if ((!readOnly) && (cursorPosY>0))
				{
					// TODO : UTF8 clean cursor displacement in text
					size_t cursorPosX=cursorPos-lines[cursorPosY];
					size_t newLineLen=lines[cursorPosY]-lines[cursorPosY-1];
					
					if (cursorPosX<newLineLen)
					{
						cursorPos=lines[cursorPosY-1]+cursorPosX;
					}
					else
					{
						cursorPos=lines[cursorPosY]-1;
					}
					
					compute();
					parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
				}
			}
			break;

			case SDLK_DOWN:
			{
				if ((!readOnly) && (cursorPosY+1<lines.size()))
				{
					// TODO : UTF8 clean cursor displacement in text
					size_t cursorPosX=cursorPos-lines[cursorPosY];
					size_t newLineLen;
					
					if (cursorPosY==lines.size()-2)
					{
						newLineLen=text.length()-lines[cursorPosY+1];
					}
					else
					{
						newLineLen=lines[cursorPosY+2]-lines[cursorPosY+1]-1;
					}
					
					if (cursorPosX < newLineLen)
					{
						cursorPos=lines[cursorPosY+1]+cursorPosX;
					}
					else
					{
						cursorPos=lines[cursorPosY+1]+newLineLen;
					}

					compute();
					parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
				}
			}
			break;
			
			case SDLK_LEFT:
			if (!readOnly)
			{
				if (mod&KMOD_CTRL)
				{
					bool cont=true;
					while ((cursorPos>0) && cont)
					{
						cursorPos=getPrevUTF8Char(text.c_str(), cursorPos);
						switch (text[cursorPos])
						{
							case '.':
							case ' ':
							case '\t':
							case ',':
							case '\'':
							case '\r':
							case '\n':
							cont=false;
							default:
							break;
						}
					}
					compute();
					parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
				}
				else
				{
					if (cursorPos>0)
					{
						cursorPos=getPrevUTF8Char(text.c_str(), cursorPos);
						compute();
						parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
					}
				}
			}
			break;
		
			case SDLK_RIGHT:
			if (!readOnly)
			{
				if (cursorPos<text.length())
				{
					if (mod&KMOD_CTRL)
					{
						bool cont=true;
						while (cont)
						{
							assert(cursorPos < text.length());
							cursorPos=getNextUTF8Char(text.c_str(), cursorPos);
							if (cursorPos < text.length())
							{
								switch (text[cursorPos])
								{
									case '.':
									case ' ':
									case '\t':
									case ',':
									case '\'':
									case '\r':
									case '\n':
									cont = false;
									default:
									break;
								}
							}
							else
								cont = false;
						}
						compute();
						parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
					}
					else
					{	
						cursorPos=getNextUTF8Char(text.c_str(), cursorPos);
						compute();
						parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
					}
				}
			}
			break;
			
			case SDLK_ESCAPE:
			parent->onAction(this, TEXT_CANCELED, 0, 0);
			break;
			
			case SDLK_RETURN:
			if (!readOnly)
			{
				addChar('\n');
			}
			break;
		
			default:
			if (!readOnly)
			{
				Uint16 c=event->key.keysym.unicode;
				if (c)
				{
					char utf8text[4];
					UCS16toUTF8(c, utf8text);
					addText(utf8text);
					parent->onAction(this, TEXT_MODIFIED, 0, 0);
				}
			}
			break;
		}
	}

	void TextArea::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		if (isOnWidget(event->button.x, event->button.y))
		{
			if (event->button.button == 4)
			{
				scrollUp();
			}
			else if (event->button.button == 5)
			{
				scrollDown();
			}
		}
		/* Junk from older event handling system
		else if (event->type==SDL_MOUSEBUTTONDOWN)
		{
			if ((event->button.x>size.x+size.w+theme->xScroll) &&
				(event->button.x<size.x+size.w+theme->xScroll+theme->scrollUp->getW()) &&
				(event->button.y>size.y+theme->y1Scroll) &&
				(event->button.y<size.y+theme->y1Scroll+theme->scrollUp->getH()))
			{
				if (areaPos>0)
				{
					areaPos--;
				}
				return true;
			}
			else if ((event->button.x>size.x+size.w+theme->xScroll) &&
					(event->button.x<size.x+size.w+theme->xScroll+theme->scrollUp->getW()) &&
					(event->button.y>size.y+size.h+theme->y2Scroll) &&
					(event->button.y<size.y+size.h+theme->y2Scroll+theme->scrollUp->getH()))
			{
				if (areaPos<lines.size()-areaHeight-1)
				{
					areaPos++;
				}
				return true;
			}
		}*/
		//return false;
	}
	
	void TextArea::setCursorPos(unsigned pos)
	{
		cursorPos = std::min(pos, (unsigned int)text.length());
		compute();
	}
	
	void TextArea::internalInit(void)
	{
		layout();
	}
	
	void TextArea::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		HighlightableWidget::paint();
		
		areaHeight=(h-8)/charHeight;
		parent->getSurface()->setClipRect(x, y, w, h);
		
		for (unsigned i=0;(i<areaHeight)&&((signed)i<(signed)(lines.size()-areaPos));i++)
		{
			assert(i+areaPos<lines.size());
			if (i+areaPos<lines.size()-1)
			{
				const std::string &substr = text.substr(lines[i+areaPos], lines[i+areaPos+1]-lines[i+areaPos]);
				parent->getSurface()->drawString(x+4+spriteWidth, y+4+(charHeight*i), font, substr.c_str(), w-8-spriteWidth);
				
				if (sprite && i+areaPos<lines_frames.size() && lines_frames[i+areaPos]>=0)
				{
					parent->getSurface()->drawSprite(x+2, y+4+(charHeight*i), sprite, lines_frames[i+areaPos]);	
				}
			}
			else
			{
				const std::string &substr = text.substr(lines[i+areaPos]);
				parent->getSurface()->drawString(x+4+spriteWidth, y+4+(charHeight*i), font, substr.c_str(), w-8-spriteWidth);
				
				if (sprite && i+areaPos<lines_frames.size() && lines_frames[i+areaPos]>=0)
				{
					parent->getSurface()->drawSprite(x+2, y+4+(charHeight*i), sprite, lines_frames[i+areaPos]);	
				}
			}
		}
	
		if (!readOnly)
		{
			int xPos = x+4+cursorScreenPosY;
			int yPos = y+4+(charHeight*(cursorPosY-areaPos));
			parent->getSurface()->drawLine(xPos, yPos, xPos, yPos + charHeight, Style::style->textColor);
		}
		parent->getSurface()->setClipRect();
	}
	
	void TextArea::compute(void)
	{
		// The only variable which is always valid is cursorPos,
		// so now we recompute cursorPosY from it.
		// But it is guarantied that cursorPosY < lines.size();
		
		assert(cursorPosY >= 0);
		assert(cursorPosY < lines.size());
		assert(cursorPos >= 0);
		assert(cursorPos <= text.length());
		
		if (!readOnly)
		{
			// increment it (if needed)
			while ((cursorPosY < lines.size()-1)  && (lines[cursorPosY+1] <= cursorPos))
				cursorPosY++;
	
			// decrement it (if needed)
			while (cursorPos < lines[cursorPosY])
				cursorPosY--;
	
			// make sure the cursor the visible window follow the cursor
			if (cursorPosY>0)
			{
				while (cursorPosY-1<areaPos)
					areaPos--;
			}
			else
			{
				while (cursorPosY<areaPos)
					areaPos--;
			}
			if (cursorPosY<lines.size()-1)
			{
				while (cursorPosY+1>=areaPos+areaHeight)
					areaPos++;
			}
			else
			{
				while (cursorPosY>=areaPos+areaHeight)
					areaPos++;
			}
	
			// TODO : UTF8 clean cursor displacement in text should lead to the removal of this code !!
			// we need to assert cursorPos will point on the beginning of a valid UTF8 char
			unsigned utf8CleanCursorPos = lines[cursorPosY];
			while ((utf8CleanCursorPos < text.length())
				&& (utf8CleanCursorPos + getNextUTF8Char(text[utf8CleanCursorPos]) <= cursorPos))
			{
				assert(utf8CleanCursorPos < text.length());
				utf8CleanCursorPos += getNextUTF8Char(text[utf8CleanCursorPos]);
			}
			cursorPos = utf8CleanCursorPos;
	
			// compute displayable cursor Pos
			unsigned cursorPosX = cursorPos-lines[cursorPosY];
			const std::string &temp = text.substr(lines[cursorPosY], cursorPosX);
			cursorScreenPosY = getStringWidth(temp);
		}
	}
	
	int TextArea::getStringWidth(const std::string &s)
	{
		std::map<std::string, int>::iterator it = stringWidthCache.find(s);
		if (it != stringWidthCache.end())
		{
			return it->second;
		}
		else
		{
			int w = font->getStringWidth(s.c_str());
			stringWidthCache[s] = w;
			return w;
		}
	}
	
	void TextArea::layout(void)
	{
		int x, y, w, h;
		unsigned line = 0;
		getScreenPos(&x, &y, &w, &h);
		
		unsigned pos = 0;
		int length = w-4-getStringWidth("W")-spriteWidth;
		
		lines.clear();
		lines.push_back(0);
		lines_frames.clear();
		std::string lastWord;
		std::string lastLine;
		int spaceLength = getStringWidth(" ");
		
		while (pos<text.length())
		{
			switch (text[pos])
			{
				case ' ':
				case '\t':
				{
					int actLineLength = getStringWidth(lastLine);
					int actWordLength = getStringWidth(lastWord);
					if (actWordLength+actLineLength+spaceLength < length)
					{
						if (lastLine.length())
							lastLine += " ";
						lastLine += lastWord;
						lastWord.clear();
					}
					else
					{
						if (sprite && frames.size() > line)
						{
							lines_frames.push_back(frames[line]);
						}
						lines.push_back(pos-lastWord.size());
						lastLine = lastWord;
						lastWord.clear();
					}
				}
				break;
				
				case '\n':
				case '\r':
				{
					int actLineLength = getStringWidth(lastLine);
					int actWordLength = getStringWidth(lastWord);
					if (actWordLength+actLineLength+spaceLength >= length)
					{
						if (sprite && frames.size() > line)
						{	
							lines_frames.push_back(frames[line]);
						}	
						lines.push_back(pos-lastWord.size());
					}
					if (sprite && frames.size() > line)
					{
						lines_frames.push_back(frames[line]);
						line++;
					}
					lines.push_back(pos+1);
					lastWord.clear();
					lastLine.clear();
				}
				break;
				
				default:
				{
					int actLineLength = getStringWidth(lastLine);
					int actWordLength = getStringWidth(lastWord);
					int actCharLength = getStringWidth(std::string(1,text[pos]).c_str());
					if (actWordLength+actLineLength+actCharLength > length)
					{
						lastLine += lastWord;
						if (sprite && frames.size() > line)
						{
							lines_frames.push_back(frames[line]);
						}
						lines.push_back(pos-1);
						lastWord.clear();
						lastLine.clear();
					}
					lastWord += text[pos];					
				}
			}
			pos++;
		}
		
		int actLineLength = getStringWidth(lastLine);
		int actWordLength = getStringWidth(lastWord);
		if (actWordLength+actLineLength+spaceLength >= length)
		{
			if (sprite && frames.size() > line)
			{
				lines_frames.push_back(frames[line]);
			}
			lines.push_back(pos-lastWord.size());
		}
		
		if (cursorPosY >= lines.size())
			cursorPosY = lines.size()-1;
	}
	
	void TextArea::setText(const char *text)
	{
		this->text = text;
		if (cursorPos>this->text.length())
			cursorPos = this->text.length();
		layout();
		compute();
	}
	
	void TextArea::addText(const char *text)
	{
		assert(text);
		assert(cursorPos <= this->text.length());
		assert(cursorPos >= 0);
	
		if (text)
		{
			if (readOnly)
			{
				this->text += text;
			}
			else
			{
				this->text.insert(cursorPos, text);
				cursorPos += strlen(text);
			}
			
			layout();
			compute();
		}
	}

	void TextArea::addImage(int frame)
	{
		assert(sprite),
		assert(frame <= sprite->getFrameCount()-1);
		this->frames.push_back(frame);
	}
	
	void TextArea::addNoImage(void)
	{
		this->frames.push_back(-1);
	}

	void TextArea::remText(unsigned pos, unsigned len)
	{
		if (pos < text.length())
		{
			text.erase(pos, len);
			
			layout();
			compute();
		}
	}
	
	void TextArea::addChar(const char c)
	{
		char text[2];
		text[0] = c;
		text[1] = 0;
		addText(text);
	}
	
	void TextArea::scrollDown(void)
	{
		if (lines.size()>=areaHeight)
		{
			if (areaPos<lines.size()-areaHeight-1)
			{
				areaPos++;
				compute();
			}
		}
	}
	
	void TextArea::scrollUp(void)
	{
		if (lines.size()>=areaHeight)
		{
			if (areaPos>0)
			{
				areaPos--;
				compute();
			}
		}
	}
	
	void TextArea::scrollToBottom(void)
	{
		while ((signed)areaPos<(signed)lines.size()-(signed)areaHeight-1)
		{
			areaPos++;
		}
		compute();
	}
	
	bool TextArea::load(const char *filename)
	{
		StreamBackend *stream = Toolkit::getFileManager()->openInputStreamBackend(filename);
		if (stream->isEndOfStream())
		{
			delete stream;
			return false;
		}
		else
		{
			stream->seekFromEnd(0);
			size_t len = stream->getPosition();
			stream->seekFromStart(0);
			
			std::valarray<char> tempText(len+1);
			stream->read(&tempText[0], len);
			tempText[len] = 0;
			
			text = &tempText[0];
			layout();
			setCursorPos(cursorPos);
			
			delete stream;
			return true;
		}
	}
	
	bool TextArea::save(const char *filename)
	{
		StreamBackend *stream = Toolkit::getFileManager()->openOutputStreamBackend(filename);
		if (stream->isEndOfStream())
		{
			delete stream;
			return false;
		}
		else
		{
			stream->write(text.c_str(), text.length());
			delete stream;
			return true;
		}
	}
}
