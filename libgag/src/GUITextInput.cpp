/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
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

TextInput::TextInput()
{
	activated=false;
	cursPos=0;
	textDep=0;
	cursorScreenPos=0;
}

TextInput::TextInput(int x, int y, int w, int h, const char *font, const char *text, bool activated, unsigned textLength)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;

	this->font=font;
	this->text=text;

	cursPos=this->text.length();
	textDep=0;
	cursorScreenPos=0;

	this->activated=activated;
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
	if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			if (activated)
			{
				// we move cursor:
				int dx=event->button.x-x-1;

				std::string textBeforeCurs=text;
				while((cursPos<text.length()) && textBeforeCurs[cursPos])
					cursPos++;
				while((cursPos>0) && (fontPtr->getStringWidth(textBeforeCurs.c_str()+textDep)>dx))
					textBeforeCurs[--cursPos]=0;

				repaint();
				parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
			}
			else
			{
				repaint();
				activated=true;
				parent->onAction(this, TEXT_ACTIVATED, 0, 0);
			}
		}
	}

	if (activated && event->type==SDL_KEYDOWN)
	{
		SDLKey sym=event->key.keysym.sym;
		SDLMod mod=event->key.keysym.mod;

		if (sym==SDLK_RIGHT)
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
		else if (sym==SDLK_LEFT)
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
		else if (sym==SDLK_BACKSPACE)
		{
			if (cursPos>0)
			{
				// TODO : fix this in a clean way
				/*unsigned l=strlen(text);
				unsigned last=getPrevUTF8Char(text.c_str(), cursPos);

				memmove( &(text[last]), &(text[cursPos]), l-cursPos+1);

				cursPos=last;

				repaint();
				parent->onAction(this, TEXT_MODIFIED, 0, 0);*/
			}

		}
		else if (sym==SDLK_DELETE)
		{
			unsigned l=text.length();
			if (cursPos<l)
			{
				// TODO : fix this in a clean way
				/*
				int utf8l=getNextUTF8Char(text[cursPos]);

				memmove( &(text[cursPos]), &(text[cursPos+utf8l]), l-cursPos-utf8l+1);

				repaint();
				parent->onAction(this, TEXT_MODIFIED, 0, 0);*/
			}
		}
		else if (sym==SDLK_HOME)
		{
			cursPos=0;
			repaint();
			parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
		}
		else if (sym==SDLK_END)
		{
			cursPos=text.length();
			repaint();
			parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
		}
		else if (sym==SDLK_RETURN)
		{
			parent->onAction(this, TEXT_VALIDATED, 0, 0);
		}
		else if (sym==SDLK_ESCAPE)
		{
			parent->onAction(this, TEXT_CANCELED, 0, 0);
		}
		else
		{
			Uint16 c=event->key.keysym.unicode;
			if (c)
			{
				// TODO : fix this in a clean way
				/*
				char utf8text[4];
				UCS16toUTF8(c, utf8text);
				unsigned l=text.length();
				unsigned lutf8=strlen(utf8text);
				if (l+lutf8<textLength-1)
				{
					memmove( &(text[cursPos+lutf8]), &(text[cursPos]), l+1-cursPos);

					memcpy( &(text[cursPos]), utf8text, lutf8);
					cursPos+=lutf8;

					repaint();

					parent->onAction(this, TEXT_MODIFIED, 0, 0);
				}
				*/
			}
		}
	}
}

void TextInput::recomputeTextInfos(void)
{
	int textLength = text.length();
	VARARRAY(char,temp, textLength);

#define TEXTBOXSIDEPAD 30

	// make sure we have always right space at left
	if (cursPos<textDep)
		textDep=cursPos;
	strcpy(temp, text.c_str()+textDep);
	temp[cursPos-textDep]=0;
	cursorScreenPos=fontPtr->getStringWidth(temp);
	while ((cursorScreenPos<TEXTBOXSIDEPAD)&&(textDep>0))
	{
		textDep--;
		strcpy(temp,&(text[textDep]));
		temp[cursPos-textDep]=0;
		cursorScreenPos=fontPtr->getStringWidth(temp);
	}
	
	// make sure we have always right space at right
	while ( (cursorScreenPos>w-TEXTBOXSIDEPAD-4) &&	(textDep<textLength) )
	{
		textDep++;

		strcpy(temp,&(text[textDep]));
		temp[cursPos-textDep]=0;
		cursorScreenPos=fontPtr->getStringWidth(temp);
	}

}

void TextInput::paint(void)
{
	static const int r= 180;
	static const int g= 180;
	static const int b= 180;

	fontPtr = Toolkit::getFont(font.c_str());
	assert(fontPtr);
	recomputeTextInfos();

	assert(parent);
	assert(parent->getSurface());
	parent->getSurface()->drawRect(x, y, w, h, r, g, b);
	parent->getSurface()->drawString(x+2, y+3, w-6, fontPtr, "%s", text.c_str()+textDep);

	// we draw the cursor:
	if(activated)
	{
		VARARRAY(char,textBeforeCurs,text.length());
		strncpy(textBeforeCurs, text.c_str(), text.length());
		textBeforeCurs[cursPos]=0;
		//int wbc=font->getStringWidth(textBeforeCurs);
		int hbc=fontPtr->getStringHeight(textBeforeCurs);
		parent->getSurface()->drawVertLine(x+2+cursorScreenPos/*wbc*/, y+3 , hbc, r, g, b);
	}
}

void TextInput::repaint(void)
{
	assert(parent);
	parent->paint(x, y, w, h);
	paint();
	parent->addUpdateRect(x, y, w, h);
}

