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
:HighlightableWidget(returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	if (sprite)
		this->sprite=sprite;
	this->standardId=standardId;
	this->highlightID=highlightID;
	this->unicodeShortcut=unicodeShortcut;

	archPtr=NULL;
}


void Button::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	HighlightableWidget::onSDLEvent(event);

	if (event->type==SDL_KEYUP)
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

void Button::internalInit(int x, int y, int w, int h)
{
	if ((standardId>=0)||(highlightID>=0))
	{
		archPtr=Toolkit::getSprite(sprite.c_str());
		assert(archPtr);
	}
}

void Button::internalRepaint(int x, int y, int w, int h)
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


TextButton::TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *sprite, int standardId, int highlightID, const char *font, const char *text, int returnCode, Uint16 unicode)
:Button(x, y, w, h, hAlign, vAlign, sprite, standardId, highlightID, returnCode, unicode)
{
	assert(font);
	assert(text);
	this->font=font;
	this->text=text;
	fontPtr=NULL;
}

void TextButton::internalInit(int x, int y, int w, int h)
{
	Button::internalInit(x, y, w, h);
	fontPtr=Toolkit::getFont(font.c_str());
	assert(fontPtr);
}

void TextButton::internalRepaint(int x, int y, int w, int h)
{
	Button::internalRepaint(x, y, w, h);

	int decX=(w-fontPtr->getStringWidth(this->text.c_str()))>>1;
	int decY=(h-fontPtr->getStringHeight(this->text.c_str()))>>1;

	parent->getSurface()->drawString(x+decX, y+decY, fontPtr, "%s", text.c_str());
	if (highlighted)
	{
		parent->getSurface()->drawRect(x+1, y+1, w-2, h-2, 255, 255, 255);
		parent->getSurface()->drawRect(x, y, w, h, 255, 255, 255);
	}
	else
	{
		parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	}
}

void TextButton::setText(const char *text)
{
	assert(text);
	this->text=text;
	repaint();
}


OnOffButton::OnOffButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, bool startState, int returnCode)
:HighlightableWidget(returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	this->state=startState;
}

void OnOffButton::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	HighlightableWidget::onSDLEvent(event);

	if (event->type==SDL_MOUSEBUTTONDOWN)
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

void OnOffButton::internalRepaint(int x, int y, int w, int h)
{
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

void OnOffButton::setState(bool newState)
{
	if (newState!=state)
	{
		state=newState;
		repaint();
	}
}

ColorButton::ColorButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode)
:HighlightableWidget(returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->hAlignFlag=hAlign;
	this->vAlignFlag=vAlign;

	selColor=0;
}

void ColorButton::onSDLEvent(SDL_Event *event)
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);

	HighlightableWidget::onSDLEvent(event);

	if (event->type==SDL_MOUSEBUTTONDOWN)
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

void ColorButton::internalRepaint(int x, int y, int w, int h)
{
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
