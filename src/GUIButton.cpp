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

#include "GUIButton.h"

Button::Button(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, int returnCode, Uint16 unicodeShortcut)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->arch=arch;
	this->standardId=standardId;
	this->highlightID=highlightID;
	this->returnCode=returnCode;
	this->unicodeShortcut=unicodeShortcut;
	highlighted=false;
}


void Button::onSDLEvent(SDL_Event *event)
{
	assert(parent);
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
	else if (event->type==SDL_KEYDOWN)
	{
		Uint16 typedUnicode=event->key.keysym.unicode;
		if ((unicodeShortcut)&&(typedUnicode==unicodeShortcut))
			parent->onAction(this, BUTTON_SHORTCUT, returnCode, unicodeShortcut);
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
	}
	else if (event->type==SDL_MOUSEBUTTONUP)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
}

void Button::repaint(void)
{
	assert(parent);
	assert(parent->getSurface());
	if (visible)
	{
		if (highlighted)
		{
			if (highlightID>=0)
				parent->getSurface()->drawSprite(x, y, arch, highlightID);
			else
				parent->paint(x, y, w, h);
		}
		else
		{
			if (standardId>=0)
				parent->getSurface()->drawSprite(x, y, arch, standardId);
			else
				parent->paint(x, y, w, h);
		}
	}
	parent->addUpdateRect(x, y, w, h);
}

void Button::paint(void)
{
	assert(parent);
	assert(parent->getSurface());
	if ((visible)&&(standardId>=0))
		parent->getSurface()->drawSprite(x, y, arch, standardId);
	highlighted=false;
}

TextButton::TextButton(int x, int y, int w, int h, Sprite *arch, int standardId, int highlightID, const Font *font, const char *text, int returnCode, Uint16 unicode)
:Button(x, y, w, h, arch, standardId, highlightID, returnCode, unicode)
{
	this->text=NULL;
	this->font=font;
	internalSetText(text);
}

void TextButton::paint(void)
{
	assert(parent);
	assert(parent->getSurface());
	Button::paint();
	if (visible)
	{
		parent->getSurface()->drawString(x+decX, y+decY, font, text);
		parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
	}
}

void TextButton::internalSetText(const char *text)
{
	assert(font);
	int textLength=strlen(text);
	if (this->text)
		delete[] this->text;
	this->text=new char[textLength+1];
	strncpy(this->text, text, textLength+1);
	decX=(w-font->getStringWidth(text))>>1;
	decY=(h-font->getStringHeight(text))>>1;
}

void TextButton::setText(const char *text)
{
	internalSetText(text);
	repaint();
}

void TextButton::repaint(void)
{
	assert(parent);
	assert(parent->getSurface());
	Button::repaint();
	parent->paint(x, y, w, h);
	if (visible)
	{
		parent->getSurface()->drawString(x+decX, y+decY, font, text);
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

OnOffButton::OnOffButton(int x, int y, int w, int h, bool startState, int returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->state=startState;
	this->returnCode=returnCode;
	highlighted=false;
}

void OnOffButton::onSDLEvent(SDL_Event *event)
{
	assert(parent);
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
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
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
	assert(parent);
	assert(parent->getSurface());
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

ColorButton::ColorButton(int x, int y, int w, int h, int returnCode)
{
	this->x=x;
	this->y=y;
	this->w=w;
	this->h=h;
	this->returnCode=returnCode;
	highlighted=false;
	selColor=0;
}

void ColorButton::onSDLEvent(SDL_Event *event)
{
	assert(parent);
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
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			selColor++;
			if (selColor>=(signed)vr.size())
				selColor=0;
			repaint();

			parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, selColor);
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
		}
	}
	else if (event->type==SDL_MOUSEBUTTONUP)
	{
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
		}
	}
}

void ColorButton::internalPaint(void)
{
	assert(parent);
	assert(parent->getSurface());
	if (highlighted)
	{
		parent->getSurface()->drawRect(x+1, y+1, w-2, h-2, 255, 255, 255);
		parent->getSurface()->drawRect(x, y, w, h, 255, 255, 255);
		if (vr.size())
			parent->getSurface()->drawFilledRect(x+2, y+2, w-4, h-4, vr[selColor], vg[selColor], vb[selColor]);
	}
	else
	{
		parent->getSurface()->drawRect(x, y, w, h, 180, 180, 180);
		if (vr.size())
			parent->getSurface()->drawFilledRect(x+1, y+1, w-2, h-2, vr[selColor], vg[selColor], vb[selColor]);
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
	parent->paint(x, y, w, h);
	if (visible)
		internalPaint();
	parent->addUpdateRect(x, y, w, h);
}
