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

#include <GUITextInput.h>
#include <assert.h>
#include <Toolkit.h>
#include <GraphicContext.h>

TextInput::TextInput(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *text, bool activated, unsigned maxLength, bool password)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;

	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	this->font=font;
	this->text=text;
	
	cursPos=this->text.length();
	this->maxLength=maxLength;
	textDep=0;
	cursorScreenPos=0;

	this->activated=activated;
	this->password=password;
}

void TextInput::onTimer(Uint32 tick)
{
}

void TextInput::setText(const char *newText)
{
	this->text=newText;
	cursPos=0;
	textDep=0;
	cursorScreenPos=0;
	repaint();
	parent->onAction(this, TEXT_SET, 0, 0);
}

void TextInput::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			if (activated)
			{
				// we move cursor:
				int dx=event->button.x-x-1;

				cursPos = text.length();
				while((cursPos>0) && (fontPtr->getStringWidth(text.c_str()+textDep, cursPos-textDep)>dx))
					--cursPos;

				repaint();
				parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
			}
			else
			{
				activated=true;
				repaint();
				parent->onAction(this, TEXT_ACTIVATED, 0, 0);
			}
		}
	}

	if (activated && event->type==SDL_KEYDOWN)
	{
		SDLKey sym=event->key.keysym.sym;
		SDLMod mod=event->key.keysym.mod;

		switch (sym)
		{
			case SDLK_RIGHT:
			{
				unsigned l=text.length();
				if (mod&KMOD_CTRL)
				{
					bool cont=true;
					while ((cursPos<l) && cont)
					{
						cursPos=getNextUTF8Char(text.c_str(), cursPos);
						switch (text[cursPos])
						{
							case '.':
							case ' ':
							case '\t':
							case ',':
							case '\'':
							cont=false;
							default:
							break;
						}
					}
					repaint();
					parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
				}
				else
				{
					if (cursPos<l)
					{
						cursPos=getNextUTF8Char(text.c_str(), cursPos);
						repaint();
						parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
					}
				}
			}
			break;
			
			case SDLK_LEFT:
			{
				if (mod&KMOD_CTRL)
				{
					bool cont=true;
					while ((cursPos>0) && cont)
					{
						cursPos=getPrevUTF8Char(text.c_str(), cursPos);
						switch (text[cursPos])
						{
							case '.':
							case ' ':
							case '\t':
							case ',':
							case '\'':
							cont=false;
							default:
							break;
						}
					}
					repaint();
					parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
				}
				else
				{
					if (cursPos>0)
					{
						cursPos=getPrevUTF8Char(text.c_str(), cursPos);
						repaint();
						parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
					}
				}
			}
			break;
			
			case SDLK_BACKSPACE:
			{
				if (cursPos>0)
				{
					unsigned last=getPrevUTF8Char(text.c_str(), cursPos);
	
					text.erase(last, cursPos-last);
	
					cursPos=last;
	
					repaint();
					parent->onAction(this, TEXT_MODIFIED, 0, 0);
				}
			}
			break;
			
			case SDLK_DELETE:
			{
				if (cursPos<text.length())
				{
					int utf8l=getNextUTF8Char(text[cursPos]);
	
					text.erase(cursPos, utf8l);
	
					repaint();
					parent->onAction(this, TEXT_MODIFIED, 0, 0);
				}
			}
			break;
			
			case SDLK_HOME:
			{
				cursPos=0;
				repaint();
				parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
			}
			break;
			
			case SDLK_END:
			{
				cursPos=text.length();
				repaint();
				parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
			}
			break;
			
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
			{
				parent->onAction(this, TEXT_VALIDATED, 0, 0);
			}
			break;
			
			case SDLK_ESCAPE:
			{
				parent->onAction(this, TEXT_CANCELED, 0, 0);
			}
			break;
			
			default:
			{
				Uint16 c=event->key.keysym.unicode;
				if (c)
				{
					char utf8text[4];
					UCS16toUTF8(c, utf8text);
					unsigned lutf8=strlen(utf8text);
					if ((maxLength==0) || (text.length()+lutf8<maxLength))
					{
						text.insert(cursPos, utf8text);
						cursPos+=lutf8;
	
						repaint();
	
						parent->onAction(this, TEXT_MODIFIED, 0, 0);
					}
				}
			}
		}
	}
}

void TextInput::recomputeTextInfos(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
#define TEXTBOXSIDEPAD 30

	if (password)
	{
		pwd.clear();
		unsigned l = text.length();
		unsigned p = 0, op = p;
		unsigned compCursPos = 0;
		unsigned pwdCursPos = 0;
		while ((p = getNextUTF8Char(text.c_str(), op)) < l)
		{
			if (compCursPos < cursPos)
				pwdCursPos++;
			compCursPos += (p - op);
			pwd += "*";
			op = p;
		}
		cursorScreenPos=fontPtr->getStringWidth(pwd.c_str(), pwdCursPos);
	}
	else
	{
		// make sure we have always right space at left
		if (cursPos<textDep)
			textDep=cursPos;
	
		// we make cursor not out of the box at left
		textDep++;
		do
		{
			textDep--;
			cursorScreenPos=fontPtr->getStringWidth(text.c_str()+textDep, cursPos-textDep);
		}
		while ((textDep>0) && (cursorScreenPos<TEXTBOXSIDEPAD));
	
		// we make cursor not out of the box at right
		while ( (textDep<text.length()) && (cursorScreenPos>w-TEXTBOXSIDEPAD-4) )
		{
			textDep++;
			cursorScreenPos=fontPtr->getStringWidth(text.c_str()+textDep, cursPos-textDep);
		}
	}
}

void TextInput::paint(void)
{
	static const int r= 180;
	static const int g= 180;
	static const int b= 180;

	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	fontPtr = Toolkit::getFont(font.c_str());
	assert(fontPtr);
	recomputeTextInfos();

	assert(parent);
	assert(parent->getSurface());
	parent->getSurface()->drawRect(x, y, w, h, r, g, b);
	
	if (password)
	{
		parent->getSurface()->drawString(x+2, y+3, w-6, fontPtr, text.c_str()+textDep);
	}
	else
	{
		parent->getSurface()->drawString(x+2, y+3, w-6, fontPtr, pwd.c_str());
	}

	// we draw the cursor:
	if(activated)
	{
		int hbc=fontPtr->getStringHeight(text.c_str());
		parent->getSurface()->drawVertLine(x+2+cursorScreenPos, y+3 , hbc, r, g, b);
	}
}

void TextInput::repaint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	parent->paint(x, y, w, h);
	paint();
	parent->addUpdateRect(x, y, w, h);
}

