/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "GUITextArea.h"

TextArea::TextArea(int x, int y, int w, int h, const Font *font, bool readOnly=true)
{
	this->readOnly=readOnly;
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->font=font;
	textBuffer=NULL;
	assert(font);
	charHeight=font->getStringHeight(NULL);
	assert(charHeight);
	areaHeight=(h-8)/charHeight;
	areaPos=0;
	textBufferLength=0;
	
	cursorPos=0;
	cursorPosY=0;
	cursorScreenPosY=0;
}

TextArea::~TextArea(void)
{
	if (textBuffer)
		free(textBuffer);
}

void TextArea::internalPaint(void)
{
	assert(parent);
	assert(parent->getSurface());
	parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	if (textBuffer)
	{
		for (unsigned i=0;(i<areaHeight)&&((signed)i<(signed)(lines.size()-areaPos));i++)
		{
			assert(i+areaPos<lines.size());
			parent->getSurface()->drawString(x+4, y+4+(charHeight*i), w-8, font, (textBuffer+lines[i+areaPos]));
		}
	}
	if (!readOnly)
	{
		parent->getSurface()->drawVertLine(x+4+cursorScreenPosY, y+4+(charHeight*(cursorPosY-areaPos)), charHeight, 255, 255, 255);
	}
	/*if (areaPos>0)
	{
		DisplayManager::drawGraphicContent(theme->scrollUp,size.w+theme->xScroll,theme->y1Scroll);
	}
	if (areaPos+areaHeight<lines.size()-1)
	{
		DisplayManager::drawGraphicContent(theme->scrollDown,size.w+theme->xScroll,size.h+theme->y2Scroll);
	}*/
}

void TextArea::paint(void)
{
	internalPaint();
}

void TextArea::repaint(void)
{
	parent->paint(x, y, w, h);
	internalPaint();
	parent->addUpdateRect(x, y, w, h);
}

void TextArea::onSDLEvent(SDL_Event *event)
{
	if (event->type==SDL_KEYDOWN)
	{
		switch (event->key.keysym.sym)
		{
			case SDLK_DELETE:
			if (!readOnly)
			{
				remText(cursorPos, 1);
			}
			break;
			
			case SDLK_BACKSPACE:
			if (!readOnly)
			{
				if (cursorPos)
				{
					cursorPos--;
					remText(cursorPos, 1);
				}
			}
			break;
			
			case SDLK_UP:
			{
				if (readOnly)
				{
					scrollUp();
				}
				else if (cursorPosY>0)
				{
					unsigned int cursorPosX=cursorPos-lines[cursorPosY];
					unsigned int newLineLen=lines[cursorPosY]-lines[cursorPosY-1];
					
					if (cursorPosX<newLineLen)
					{
						cursorPos=lines[cursorPosY-1]+cursorPosX;
					}
					else
					{
						cursorPos=lines[cursorPosY]-1;
					}
					
					computeAndRepaint();
				}
			}
			break;
				
			case SDLK_DOWN:
			{
				if (readOnly)
				{
					scrollDown();
				}
				else if (cursorPosY+1<lines.size())
				{
					unsigned int cursorPosX=cursorPos-lines[cursorPosY];
					unsigned int newLineLen;
					
					if (cursorPosY==lines.size()-2)
					{
						newLineLen=textBufferLength-lines[cursorPosY+1];
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
					
					computeAndRepaint();
				}
			}
			break;
			
			case SDLK_LEFT:
			if (!readOnly)
			{
				if (cursorPos>0)
				{
					cursorPos--;
					computeAndRepaint();
				}
			}
			break;
		
			case SDLK_RIGHT:
			if (!readOnly)
			{
				if (cursorPos<textBufferLength)
				{
					cursorPos++;
					computeAndRepaint();
				}
			}
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
				unsigned char c=(char)event->key.keysym.unicode;
				if ((c>31) && (c<128))
				{
					addChar(c);
				}
			}
			break;
		}
	}
	/*else if (event->type==SDL_MOUSEBUTTONDOWN)
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

void TextArea::computeAndRepaint(void)
{
	// The only variable which is always valid is cursorPos,
	// so now we recompute cursorPosY from it.
	// But it is guarantied that cursorPosY < lines.size();
	
	assert(cursorPosY >= 0);
	assert(cursorPosY < lines.size());
	assert(cursorPos >= 0);
	assert(cursorPos <= textBufferLength);
	
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
			while (cursorPosY+1>areaPos+areaHeight)
				areaPos++;
		}
		else
		{
			while (cursorPosY>areaPos+areaHeight)
				areaPos++;
		}

		// compute displayable cursor Pos
		char temp[1024];
		unsigned cursorPosX = cursorPos-lines[cursorPosY];
		assert(cursorPosX < 1024);
		memcpy(temp, textBuffer+lines[cursorPosY], cursorPosX);
		temp[cursorPosX]=0;
		cursorScreenPosY=font->getStringWidth(temp);
	}
	
	// repaint
	repaint();
}


void TextArea::setText(const char *text)
{
	assert(text);
	if (text)
	{
		if (textBuffer)
		{
			free(textBuffer);
			lines.clear();
		}
		textBufferLength=strlen(text);
		textBuffer=(char *)malloc(textBufferLength+1);
		unsigned pos=0;

		char temp[1024];
		int temppos;
		int lastWhite=-1;
		lines.push_back(0);
		
		temppos=0;
		temp[temppos]=0;
		
		// TODO : add getStringWidth with a number of char paramater to get ride of temp
		
		while (pos<textBufferLength)
		{
			while ((font->getStringWidth(temp)<w-8)&&(pos<textBufferLength)&&(text[pos]!='\n'))
			{
				if (text[pos]==' ')
					lastWhite=pos;
				textBuffer[pos]=text[pos];
				temp[temppos]=text[pos];
				temppos++;
				pos++;
				assert(temppos < 1024);
				temp[temppos]=0;
			}
			if (pos<textBufferLength)
			{
				if (text[pos]=='\n')
				{
					textBuffer[pos]='\n';
					pos++;
					lines.push_back(pos);
					temppos=0;
					assert(temppos < 1024);
					temp[temppos]=0;
				}
				else // line overflow
				{
					if ((lastWhite!=-1) && (lastWhite>(int)lines[lines.size()-1]))
					{
						pos=lastWhite;
						textBuffer[pos]='\n';
						pos++;
					}
					lines.push_back(pos);
					temppos=0;
					assert(temppos < 1024);
					temp[temppos]=0;
				}
			}
			else
			{
				textBuffer[pos]=0;
			}
		}
		if (pos==textBufferLength)
			textBuffer[pos]=0;
		
		computeAndRepaint();
	}
}

void TextArea::addText(const char *text)
{
	assert(text);
	assert(cursorPos <= textBufferLength);
	assert(cursorPos >= 0);
	
	if (text)
	{
		int ts=strlen(text);

		char *temp=(char *)malloc(textBufferLength+ts+1);

		if (readOnly)
		{
			memcpy(temp, textBuffer, textBufferLength);
			memcpy(temp+textBufferLength, text, ts);
		}
		else
		{
			memcpy(temp, textBuffer, cursorPos);
			memcpy(temp+cursorPos, text, ts);
			memcpy(temp+cursorPos+ts, textBuffer+cursorPos, textBufferLength-cursorPos);
			cursorPos+=ts;
		}
		
		temp[textBufferLength+ts]=0;
		
		setText(temp);
		
		free(temp);
	}
}

void TextArea::remText(unsigned pos, unsigned len)
{
	if (pos < textBufferLength)
	{
		// if we wanna delete past the end
		if (pos + len >= textBufferLength)
			len = textBufferLength - pos;
		
		unsigned newLen = textBufferLength-len;
		char *temp=(char *)malloc(newLen+1);
		
		memcpy(temp, textBuffer, pos);
		memcpy(temp+pos, textBuffer+pos+len, textBufferLength-len-pos);
		temp[newLen] = 0;
		
		// make sure the cursor isn't past the end now
		if (cursorPos > newLen)
			cursorPos = newLen;
		// because we can decrease line count, let's zero the Y offset
		cursorPosY = 0;
		
		setText(temp);
		
		free(temp);
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
		}
	}
	repaint();
}

void TextArea::scrollUp(void)
{
	if (lines.size()>=areaHeight)
	{
		if (areaPos>0)
		{
			areaPos--;
		}
	}
	repaint();
}

void TextArea::scrollToBottom(void)
{
	while ((signed)areaPos<(signed)lines.size()-(signed)areaHeight-1)
	{
		areaPos++;
	}
	repaint();
}
