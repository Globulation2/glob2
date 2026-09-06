// SPDX-License-Identifier: GPL-3.0-or-later

#include <GUITextArea.h>
#include <assert.h>
#include <algorithm>

namespace GAGGUI
{
	void TextArea::onSDLTextInput(SDL_Event *event)
	{
		assert(event->type == SDL_TEXTINPUT);
		if (activated && !readOnly)
		{
			char* c=event->text.text;
			if (c)
			{
				addText(c);
				parent->onAction(this, TEXT_MODIFIED, 0, 0);
			}
		}
	}

	void TextArea::onSDLKeyDown(SDL_Event *event)
	{
		assert(event->type == SDL_KEYDOWN);
		if (!activated)
			return;
		
		SDL_Keycode sym=event->key.keysym.sym;
		bool ctrl=event->key.keysym.mod & KMOD_CTRL;
		
		switch (sym)
		{
			case SDLK_DELETE:
			if (!readOnly)
				deleteForward();
			break;
			
			case SDLK_BACKSPACE:
			if (!readOnly)
				deleteBackward();
			break;
			
			case SDLK_HOME:
			moveToLineStart();
			break;
			
			case SDLK_END:
			moveToLineEnd();
			break;
			
			case SDLK_PAGEUP:
			pageUp();
			break;
			
			case SDLK_PAGEDOWN:
			pageDown();
			break;
			
			case SDLK_UP:
			if (!readOnly && cursorPosY>0)
			{
				scrollCursorUpLine();
				notifyCursorMoved();
			}
			break;
			
			case SDLK_DOWN:
			if (!readOnly && cursorPosY+1<lines.size())
			{
				scrollCursorDownLine();
				notifyCursorMoved();
			}
			break;
			
			case SDLK_LEFT:
			if (!readOnly)
				moveLeft(ctrl);
			break;
			
			case SDLK_RIGHT:
			if (!readOnly)
				moveRight(ctrl);
			break;
			
			case SDLK_ESCAPE:
			parent->onAction(this, TEXT_CANCELED, 0, 0);
			break;
			
			case SDLK_TAB:
			parent->onAction(this, TEXT_TABBED, 0, 0);
			break;
			
			case SDLK_RETURN:
			if (!readOnly)
				addChar('\n');
			break;
			
			default:
			break;
		}
	}

	void TextArea::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		if (isOnWidget(event->button.x, event->button.y))
		{
			if(activated)
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
			else if(!activated && event->button.button == SDL_BUTTON_LEFT)
			{
				activated=true;
				parent->onAction(this, TEXT_ACTIVATED, 0, 0);
			}
		}
	}
	
	bool TextArea::isWordBreak(char c)
	{
		switch (c)
		{
			case '.':
			case ' ':
			case '\t':
			case ',':
			case '\'':
			case '\r':
			case '\n':
			return true;
			default:
			return false;
		}
	}
	
	void TextArea::notifyCursorMoved(void)
	{
		parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
	}
	
	size_t TextArea::lineLength(size_t line) const
	{
		if (line == lines.size()-1)
			return text.length()-lines[line];
		return lines[line+1]-lines[line]-1;
	}
	
	size_t TextArea::cursorColumn(void) const
	{
		return cursorPos-lines[cursorPosY];
	}
	
	void TextArea::deleteForward(void)
	{
		if (cursorPos < text.length())
		{
			size_t next=getNextUTF8Char(text.c_str(), cursorPos);
			remText(cursorPos, next-cursorPos);
			parent->onAction(this, TEXT_MODIFIED, 0, 0);
		}
	}
	
	void TextArea::deleteBackward(void)
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
	
	void TextArea::moveToLineStart(void)
	{
		if (SDL_GetModState() & KMOD_CTRL)
			cursorPos=0;
		else
			cursorPos=lines[cursorPosY];
		compute();
		notifyCursorMoved();
	}
	
	void TextArea::moveToLineEnd(void)
	{
		if (SDL_GetModState() & KMOD_CTRL)
			cursorPos=text.length();
		else
			cursorPos=lines[cursorPosY]+lineLength(cursorPosY);
		compute();
		notifyCursorMoved();
	}
	
	void TextArea::moveCursorUpTo(size_t newPosY)
	{
		// TODO : UTF8 clean cursor displacement in text
		size_t column=cursorColumn();
		size_t span=lines[cursorPosY]-lines[newPosY];
		if (column<span)
			cursorPos=lines[newPosY]+column;
		else
			cursorPos=lines[cursorPosY]-1;
	}
	
	void TextArea::pageUp(void)
	{
		if (areaPos==0)
			return;
		assert(lines.size()>areaHeight);
		
		areaPos-=MIN(areaPos, areaHeight);
		
		if (!readOnly)
		{
			size_t newPosY=cursorPosY>areaPos+areaHeight-2 ? areaPos+areaHeight-2 : cursorPosY;
			if (newPosY!=cursorPosY)
			{
				moveCursorUpTo(newPosY);
				notifyCursorMoved();
			}
		}
		
		compute();
	}
	
	void TextArea::pageDown(void)
	{
		if (lines.size()<areaHeight)
			return;
		if (areaPos>=lines.size()-areaHeight)
			return;
		
		areaPos+=std::min(lines.size()-areaHeight-areaPos, areaHeight);
		
		if (!readOnly)
		{
			// TODO : UTF8 clean cursor displacement in text
			size_t newPosY=cursorPosY<areaPos+1 ? areaPos+1 : cursorPosY;
			if (newPosY!=cursorPosY)
			{
				size_t column=cursorColumn();
				size_t newLineLen=lineLength(newPosY);
				if (column<newLineLen)
					cursorPos=lines[newPosY]+column;
				else
					cursorPos=lines[newPosY]+newLineLen-1;
				notifyCursorMoved();
			}
		}
		
		compute();
	}
	
	void TextArea::moveLeft(bool wordwise)
	{
		if (wordwise)
		{
			while (cursorPos>0)
			{
				cursorPos=getPrevUTF8Char(text.c_str(), cursorPos);
				if (isWordBreak(text[cursorPos]))
					break;
			}
		}
		else
		{
			if (cursorPos==0)
				return;
			cursorPos=getPrevUTF8Char(text.c_str(), cursorPos);
		}
		compute();
		notifyCursorMoved();
	}
	
	void TextArea::moveRight(bool wordwise)
	{
		if (cursorPos>=text.length())
			return;
		if (wordwise)
		{
			while (true)
			{
				assert(cursorPos < text.length());
				cursorPos=getNextUTF8Char(text.c_str(), cursorPos);
				if (cursorPos >= text.length() || isWordBreak(text[cursorPos]))
					break;
			}
		}
		else
		{
			cursorPos=getNextUTF8Char(text.c_str(), cursorPos);
		}
		compute();
		notifyCursorMoved();
	}
	
	void TextArea::setCursorPos(unsigned pos)
	{
		cursorPos = std::min(pos, (unsigned int)text.length());
		compute();
	}
	
	void TextArea::setCursorPos(unsigned line, unsigned column)
	{
		unsigned lineCounter = 0;
		unsigned columnCounter = 0;
		unsigned p = 0;
		while (p < text.size())
		{
			// the line we want is long enough
			if ((lineCounter >= line) && (columnCounter >= column))
				break;
			// the line we wanted was not long enough
			if (lineCounter > line)
				break;
			
			if (text[p] == '\n')
			{
				lineCounter++;
				columnCounter = 0;
			}
			else
				columnCounter++;
			p++;
		}
		setCursorPos(p);
	}
	
	void TextArea::getCursorPos(unsigned &pos) const
	{
		pos = cursorPos;
	}
	
	void TextArea::getCursorPos(unsigned &line, unsigned &column) const
	{
		line = 0;
		column = 0;
		unsigned p = 0;
		while (p < cursorPos)
		{
			if (text[p] == '\n')
			{
				line++;
				column = 0;
			}
			else
				column++;
			p++;
		}
	}
	
	void TextArea::scrollDown(void)
	{
		if (lines.size()>=areaHeight)
		{
			if (areaPos<lines.size()-areaHeight-1)
			{
				areaPos++;
				scrollCursorDownLine();
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
				scrollCursorUpLine();
			}
		}
	}
	
	void TextArea::scrollCursorDownLine(void)
	{
		if(cursorPosY < lines.size()-1)
		{
			// TODO : UTF8 clean cursor displacement in text
			size_t column=cursorColumn();
			size_t newLineLen=lineLength(cursorPosY+1);
			if (column < newLineLen)
				cursorPos=lines[cursorPosY+1]+column;
			else
				cursorPos=lines[cursorPosY+1]+newLineLen;
			compute();
		}
	}

	void TextArea::scrollCursorUpLine(void)
	{
		if(cursorPosY>0)
			moveCursorUpTo(cursorPosY-1);
		compute();
	}
	
	void TextArea::scrollToBottom(void)
	{
		while ((signed)areaPos<(signed)lines.size()-(signed)areaHeight-1)
		{
			areaPos++;
		}
		compute();
	}
}
