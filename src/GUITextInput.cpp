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

#include "GUITextInput.h"

TextInput::TextInput(int x, int y, int w, int h, const Font *font, const char *text, bool activated)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	
	this->font=font;

	if (text)
		strncpy(this->text, text, MAX_TEXT_SIZE);
	else
		this->text[0]=0;
	
	this->text[MAX_TEXT_SIZE-1]=0;
	
	cursPos=strlen(text);

	this->activated=activated;
}

void TextInput::onTimer(Uint32 tick)
{
}

void TextInput::setText(const char *newText)
{
	strncpy(this->text, newText, MAX_TEXT_SIZE);
	cursPos=0;
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
				
				char textBeforeCurs[MAX_TEXT_SIZE];
				strncpy(textBeforeCurs, text, MAX_TEXT_SIZE);
				while(textBeforeCurs[cursPos]&&(cursPos<MAX_TEXT_SIZE))
					cursPos++;
				while((font->getStringWidth(textBeforeCurs)>dx)&&(cursPos>0))
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
			int l=strlen(text);
			if (mod&KMOD_CTRL)
			{
				bool cont=true;
				while ((cursPos<l) && cont)
				{
					cursPos++;
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
					cursPos++;
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
					cursPos--;
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
					cursPos--;
					repaint();
					parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
				}
			}
		}
		else if (sym==SDLK_BACKSPACE)
		{
			if (cursPos>0)
			{
				int l=strlen(text);

				memmove( &(text[cursPos-1]), &(text[cursPos]), l-cursPos+1);

				//printf("clear: l=%d, cursPos=%d, text=%s \n", l, cursPos, text);
				cursPos--;

				repaint();
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);

				//printf("now: l=%d, cursPos=%d, text=%s \n", strlen(text), cursPos, text);
			}

		}
		else if (sym==SDLK_DELETE)
		{
			int l=strlen(text);
			if (cursPos<l)
			{
				memmove( &(text[cursPos]), &(text[cursPos+1]), l-cursPos);

				repaint();
				parent->onAction(this, TEXT_MODIFFIED, 0, 0);
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
			cursPos=strlen(text);
			repaint();
			parent->onAction(this, TEXT_CURSOR_MOVED, 0, 0);
		}
		else if (sym==SDLK_RETURN)
		{
			parent->onAction(this, TEXT_VALIDATED, 0, 0);
		}
		else
		{
			char c=event->key.keysym.unicode;

			if (font->printable(c))
			{
				int l=strlen(text);
				if (l<MAX_TEXT_SIZE)
				{
					memmove( &(text[cursPos+1]), &(text[cursPos]), l-cursPos);

					text[cursPos]=c;
					cursPos++;

					repaint();

					parent->onAction(this, TEXT_MODIFFIED, 0, 0);
				}
			}
		}
	}
}

void TextInput::paint(void)
{
	static const int r= 180;
	static const int g= 180;
	static const int b= 180;
	
	assert(parent);
	assert(parent->getSurface());
	parent->getSurface()->drawRect(x, y, w, h, r, g, b);
	parent->getSurface()->drawString(x+2, y+2, font, text);

	// we draw the cursor:
	if(activated)
	{
		char textBeforeCurs[MAX_TEXT_SIZE];
		strncpy(textBeforeCurs, text, MAX_TEXT_SIZE);
		textBeforeCurs[cursPos]=0;
		int wbc=font->getStringWidth(textBeforeCurs);
		int hbc=font->getStringHeight(textBeforeCurs);
		parent->getSurface()->drawVertLine(x+2+wbc, y+2 , hbc, r, g, b);
	}
}

void TextInput::repaint(void)
{
	assert(parent);
	parent->paint(x, y, w, h);
	paint();
	parent->addUpdateRect(x, y, w, h);
}

