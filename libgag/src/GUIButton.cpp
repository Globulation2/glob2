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

#include <GUIButton.h>
#include <Toolkit.h>
#include <assert.h>

Button::Button(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *sprite, int standardId, int highlightID, int returnCode, Uint16 unicodeShortcut)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	this->sprite=sprite;
	this->standardId=standardId;
	this->highlightID=highlightID;
	this->returnCode=returnCode;
	this->unicodeShortcut=unicodeShortcut;

	highlighted=false;
	archPtr=NULL;
}


void Button::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (event->type==SDL_MOUSEMOTION)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
		{
			if (!highlighted)
			{
				highlighted=true;
				repaint();
				parent->onAction(this, BUTTON_GOT_MOUSEOVER, returnCode, 0);
			}
		}
		else
		{
			if (highlighted)
			{
				highlighted=false;
				repaint();
				parent->onAction(this, BUTTON_LOST_MOUSEOVER, returnCode, 0);
			}
		}
	}
	else if (event->type==SDL_KEYUP)
	{
		Uint16 typedUnicode=event->key.keysym.unicode;
		if ((unicodeShortcut)&&(typedUnicode==unicodeShortcut))
			parent->onAction(this, BUTTON_SHORTCUT, returnCode, unicodeShortcut);
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h) &&
			(event->button.button == SDL_BUTTON_LEFT))
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
	}
	else if (event->type==SDL_MOUSEBUTTONUP)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h) &&
			(event->button.button == SDL_BUTTON_LEFT))
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
}

void Button::repaint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (visible)
	{
		if (highlighted)
		{
			if (highlightID>=0)
				parent->getSurface()->drawSprite(x, y, archPtr, highlightID);
			else
				parent->paint(x, y, w, h);
		}
		else
		{
			if (standardId>=0)
				parent->getSurface()->drawSprite(x, y, archPtr, standardId);
			else
				parent->paint(x, y, w, h);
		}
	}
	parent->addUpdateRect(x, y, w, h);
}

void Button::paint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if ((visible)&&((standardId>=0)||(highlightID>=0)))
	{
		archPtr=Toolkit::getSprite(sprite.c_str());
		assert(archPtr);
		parent->getSurface()->drawSprite(x, y, archPtr, standardId);
	}
	highlighted=false;
}

TextButton::TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *sprite, int standardId, int highlightID, const char *font, const char *text, int returnCode, Uint16 unicode)
:Button(x, y, w, h, hAlign, vAlign, sprite, standardId, highlightID, returnCode, unicode)
{
	assert(font);
	assert(text);
	this->font=font;
	this->text=text;
}

void TextButton::paint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	fontPtr=Toolkit::getFont(font.c_str());
	assert(fontPtr);

	int decX=(w-fontPtr->getStringWidth(this->text.c_str()))>>1;
	int decY=(h-fontPtr->getStringHeight(this->text.c_str()))>>1;

	Button::paint();
	if (visible)
	{
		parent->getSurface()->drawString(x+decX, y+decY, fontPtr, "%s", text.c_str());
		parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	}
}

void TextButton::setText(const char *text)
{
	assert(text);
	this->text=text;
	repaint();
}

void TextButton::repaint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	assert(fontPtr);

	Button::repaint();
	parent->paint(x, y, w, h);
	if (visible)
	{
		int decX=(w-fontPtr->getStringWidth(this->text.c_str()))>>1;
		int decY=(h-fontPtr->getStringHeight(this->text.c_str()))>>1;

		parent->getSurface()->drawString(x+decX, y+decY, fontPtr, "%s", text.c_str());
		if (highlighted)
		{
			parent->getSurface()->drawRect(x+1, y+1, w-2, h-2, 255, 255, 255);
			parent->getSurface()->drawRect(x, y, w, h, 255, 255, 255);
		}
		else
			parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	}
	parent->addUpdateRect(x, y, w, h);
}

// FIXME : use intermediate class for highlight handling

OnOffButton::OnOffButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, bool startState, int returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	this->state=startState;
	this->returnCode=returnCode;
	highlighted=false;
}

void OnOffButton::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (event->type==SDL_MOUSEMOTION)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
		{
			if (!highlighted)
			{
				highlighted=true;
				repaint();
				parent->onAction(this, BUTTON_GOT_MOUSEOVER, returnCode, 0);
			}
		}
		else
		{
			if (highlighted)
			{
				highlighted=false;
				repaint();
				parent->onAction(this, BUTTON_LOST_MOUSEOVER, returnCode, 0);
			}
		}
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h) &&
			(event->button.button == SDL_BUTTON_LEFT))
		{
			state=!state;
			repaint();
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, state == true ? 1 : 0);
		}
	}
	else if (event->type==SDL_MOUSEBUTTONUP)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
}

void OnOffButton::internalPaint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (highlighted)
	{
		parent->getSurface()->drawRect(x+1, y+1, w-2, h-2, 255, 255, 255);
		parent->getSurface()->drawRect(x, y, w, h, 255, 255, 255);
	}
	else
		parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	if (state)
	{
		parent->getSurface()->drawLine(x+(w/5)+1, y+(h/2), x+(w/2), y+4*(w/5)-1, 0, 255, 0);
		parent->getSurface()->drawLine(x+(w/5), y+(h/2), x+(w/2), y+4*(w/5), 0, 255, 0);
		parent->getSurface()->drawLine(x+(w/2), y+4*(w/5)-1, x+4*(w/5), y+(w/5), 0, 255, 0);
		parent->getSurface()->drawLine(x+(w/2), y+4*(w/5), x+4*(w/5)-1, y+(w/5), 0, 255, 0);
	}
}

void OnOffButton::paint(void)
{
	highlighted=false;
	if (visible)
		internalPaint();
}

void OnOffButton::repaint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	parent->paint(x, y, w, h);
	if (visible)
		internalPaint();
	parent->addUpdateRect(x, y, w, h);
}

void OnOffButton::setState(bool newState)
{
	if (newState!=state)
	{
		state=newState;
		repaint();
	}
}

ColorButton::ColorButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;
	
	this->returnCode=returnCode;
	highlighted=false;
	selColor=0;
}

void ColorButton::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (event->type==SDL_MOUSEMOTION)
	{
		if (isPtInRect(event->motion.x, event->motion.y, x, y, w, h))
		{
			if (!highlighted)
			{
				highlighted=true;
				repaint();
				parent->onAction(this, BUTTON_GOT_MOUSEOVER, returnCode, 0);
			}
		}
		else
		{
			if (highlighted)
			{
				highlighted=false;
				repaint();
				parent->onAction(this, BUTTON_LOST_MOUSEOVER, returnCode, 0);
			}
		}
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h) && v.size())
		{
			if (event->button.button == SDL_BUTTON_LEFT)
			{
				selColor++;
				if (selColor>=(signed)v.size())
					selColor=0;
				repaint();

				parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, selColor);
				parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			}
			else if (event->button.button == SDL_BUTTON_RIGHT)
			{
				selColor--;
				if (selColor<0)
					selColor=(signed)v.size()-1;
				repaint();
				
				parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, selColor);
				parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			}
		}
	}
	else if (event->type==SDL_MOUSEBUTTONUP)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h) &&
			(event->button.button == SDL_BUTTON_LEFT))
		{
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
		}
	}
}

void ColorButton::internalPaint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	if (highlighted)
	{
		parent->getSurface()->drawRect(x+1, y+1, w-2, h-2, 255, 255, 255);
		parent->getSurface()->drawRect(x, y, w, h, 255, 255, 255);
		if (v.size())
			parent->getSurface()->drawFilledRect(x+2, y+2, w-4, h-4, v[selColor].r, v[selColor].g, v[selColor].b);
	}
	else
	{
		parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
		if (v.size())
			parent->getSurface()->drawFilledRect(x+1, y+1, w-2, h-2, v[selColor].r, v[selColor].g, v[selColor].b);
	}
}

void ColorButton::paint(void)
{
	highlighted=false;
	if (visible)
		internalPaint();
}

void ColorButton::repaint(void)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	parent->paint(x, y, w, h);
	if (visible)
		internalPaint();
	parent->addUpdateRect(x, y, w, h);
}
