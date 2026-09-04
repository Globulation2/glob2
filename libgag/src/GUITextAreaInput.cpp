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
		
		if(activated)
		{
			SDL_Keycode sym=event->key.keysym.sym;
			Uint16 mod=event->key.keysym.mod;
			
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
						scrollCursorUpLine();
						parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
					}
				}
				break;
	
				case SDLK_DOWN:
				{
					if ((!readOnly) && (cursorPosY+1<lines.size()))
					{
						scrollCursorDownLine();
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
				
				case SDLK_TAB:
				{
					parent->onAction(this, TEXT_TABBED, 0, 0);
				}
				break;
				
				case SDLK_RETURN:
				if (!readOnly)
				{
					addChar('\n');
				}
				break;
			
				default:
				//Unicode handling moved to onSDLTextInput
				break;
			}
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
		}
	}

	void TextArea::scrollCursorUpLine(void)
	{
		if(cursorPosY>0)
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
		}
		
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
