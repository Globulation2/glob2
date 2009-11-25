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

#include <GUIButton.h>
#include <GUIStyle.h>
#include <Toolkit.h>
#include <assert.h>
#include <GraphicContext.h>

using namespace GAGCore;

namespace GAGGUI
{
	Button::Button(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode, Uint16 unicodeShortcut) :
		HighlightableWidget(returnCode)
	{
		this->x = x;
		this->y = y;
		this->w = w;
		this->h = h;
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;
	
		this->unicodeShortcut=unicodeShortcut;
		this->isClickable = true;
	}

	Button::Button(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, int returnCode,
		const std::string& tooltip, const std::string &tooltipFont, Uint16 unicodeShortcut) :
		HighlightableWidget(tooltip, tooltipFont, returnCode)
	{
		this->x = x;
		this->y = y;
		this->w = w;
		this->h = h;
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;
	
		this->unicodeShortcut=unicodeShortcut;
		this->isClickable = true;
	}
	
	void Button::onSDLKeyDown(SDL_Event *event)
	{
		assert(event->type == SDL_KEYDOWN);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		Uint16 typedUnicode=event->key.keysym.unicode;
		if ((unicodeShortcut)&&(typedUnicode==unicodeShortcut))
			parent->onAction(this, BUTTON_SHORTCUT, returnCode, unicodeShortcut);
	}

	void Button::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		if (isOnWidget(event->button.x, event->button.y) &&
				  (event->button.button == SDL_BUTTON_LEFT)
				  && isClickable)
		parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
	}
	
	void Button::onSDLMouseButtonUp(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONUP);
		if (isOnWidget(event->button.x, event->button.y) &&
				(event->button.button == SDL_BUTTON_LEFT)
				  && isClickable)
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
	
	
	TextButton::TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, int returnCode, Uint16 unicode) :
		Button(x, y, w, h, hAlign, vAlign, returnCode, unicode)
	{
		assert(font.size());
		assert(text.size());
		this->font=font;
		this->text=text;
		fontPtr=NULL;
	}

	TextButton::TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, int returnCode, const std::string& tooltip, const std::string &tooltipFont, Uint16 unicode) :
		Button(x, y, w, h, hAlign, vAlign, returnCode, tooltip, tooltipFont, unicode)
	{
		assert(font.size());
		assert(text.size());
		this->font=font;
		this->text=text;
		fontPtr=NULL;
	}


	void TextButton::internalInit(void)
	{
		Button::internalInit();
		fontPtr = Toolkit::getFont(font.c_str());
		assert(fontPtr);
	}
	
	void TextButton::paint()
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		Style::style->drawTextButtonBackground(parent->getSurface(), x, y, w, h, getNextHighlightValue());
		
		int decX=(w-fontPtr->getStringWidth(this->text.c_str()))>>1;
		int decY=(h-fontPtr->getStringHeight(this->text.c_str()))>>1;
	
		parent->getSurface()->drawString(x+decX, y+decY, fontPtr, text.c_str());
	}
	
	void TextButton::setText(const std::string text)
	{
		assert(text.size());
		this->text=text;
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
		isClickable=true;
	}
	
	OnOffButton::OnOffButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, bool startState, int returnCode, const std::string &tooltip, const std::string &tooltipFont)
	:HighlightableWidget(tooltip, tooltipFont, returnCode)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->state=startState;
		isClickable=true;
	}
	
	void OnOffButton::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		if (isOnWidget(event->button.x, event->button.y) &&
			(event->button.button == SDL_BUTTON_LEFT) && isClickable)
		{
			state=!state;
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, state == true ? 1 : 0);
		}
	}
	
	void OnOffButton::onSDLMouseButtonUp(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONUP);
		if (isOnWidget(event->button.x, event->button.y) && isClickable)
				parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
	
	void OnOffButton::paint()
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		Style::style->drawOnOffButton(parent->getSurface(), x, y, w, h, getNextHighlightValue(), state);
	}
	
	void OnOffButton::setState(bool newState)
	{
		if (newState!=state)
		{
			state=newState;
		}
	}
	
	
	TriButton::TriButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Uint8 startState, int returnCode)
	:HighlightableWidget(returnCode)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->state=startState;
		isClickable=true;
	}
	
	TriButton::TriButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, Uint8 startState, int returnCode, const std::string &tooltip, const std::string &tooltipFont)
	:HighlightableWidget(tooltip, tooltipFont, returnCode)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->state=startState;
		isClickable=true;
	}
	
	void TriButton::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		if (isOnWidget(event->button.x, event->button.y) &&
			(event->button.button == SDL_BUTTON_LEFT) && isClickable)
		{
			if(state == 0)
				state=1;
			else if(state == 1)
				state = 2;
			else
				state = 0;
			parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, state);
		}
	}
	
	void TriButton::onSDLMouseButtonUp(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONUP);
		if (isOnWidget(event->button.x, event->button.y) && isClickable)
				parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
	}
	
	void TriButton::paint()
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		Style::style->drawTriButton(parent->getSurface(), x, y, w, h, getNextHighlightValue(), state);
	}
	
	void TriButton::setState(Uint8 newState)
	{
		state = newState;
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
		this->isClickable=true;
		
		selColor=0;
	}

	ColorButton::ColorButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string& tooltip, const std::string &tooltipFont, int returnCode)
	:HighlightableWidget(tooltip, tooltipFont, returnCode)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
		this->isClickable=true;
	
		selColor=0;
	}
	
	void ColorButton::onSDLMouseButtonDown(SDL_Event *event)
	{
		if (isOnWidget(event->button.x, event->button.y) && v.size() && isClickable)
		{
			if (event->button.button == SDL_BUTTON_LEFT)
			{
				selColor++;
				if (selColor>=(signed)v.size())
					selColor=0;
		
				parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, selColor);
				parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			}
			else if (event->button.button == SDL_BUTTON_RIGHT)
			{
				selColor--;
				if (selColor<0)
					selColor=(signed)v.size()-1;
				
				parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, selColor);
				parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			}
		}
	}
	
	void ColorButton::onSDLMouseButtonUp(SDL_Event *event)
	{
		if (isOnWidget(event->button.x, event->button.y) &&
				(event->button.button == SDL_BUTTON_LEFT) && isClickable)
		{
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
		}
	}
	
	void ColorButton::paint()
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		if (v.size())
			parent->getSurface()->drawFilledRect(x+1, y+1, w-2, h-2, v[selColor]);
		HighlightableWidget::paint();
	}
	
	MultiTextButton::MultiTextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, int returnCode, Uint16 unicode) :
		TextButton(x, y, w, h, hAlign, vAlign, font, text, returnCode, unicode)
	{
		textIndex = 0;
	}
	MultiTextButton::MultiTextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, int returnCode, const std::string& tooltip, const std::string &tooltipFont, Uint16 unicode) :
		TextButton(x, y, w, h, hAlign, vAlign, font, text, returnCode, tooltip, tooltipFont, unicode)
	{
		textIndex = 0;
	}
	
	void MultiTextButton::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		if (isOnWidget(event->button.x, event->button.y) && texts.size() && isClickable)
		{
			if (event->button.button == SDL_BUTTON_LEFT)
			{
				textIndex++;
				if (textIndex >= texts.size())
					textIndex = 0;
				setText(texts.at(textIndex));
	
				parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, textIndex);
				parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			}
			else if (event->button.button == SDL_BUTTON_RIGHT)
			{
				if (textIndex > 0)
					textIndex--;
				else
					textIndex = texts.size()-1;
				setText(texts.at(textIndex));
				
				parent->onAction(this, BUTTON_STATE_CHANGED, returnCode, textIndex);
				parent->onAction(this, BUTTON_PRESSED, returnCode, 0);
			}
		}
	}
	
	void MultiTextButton::onSDLMouseButtonUp(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONUP);
		if (isOnWidget(event->button.x, event->button.y) &&
			(event->button.button == SDL_BUTTON_LEFT) && isClickable)
		{
			parent->onAction(this, BUTTON_RELEASED, returnCode, 0);
		}
	}
	
	void MultiTextButton::addText(const std::string s)
	{
		texts.push_back(s);
	}
	
	void MultiTextButton::clearTexts(void)
	{
		texts.clear();
	}
	
	void MultiTextButton::setIndex(int i)
	{
		textIndex = i;
		setText(texts.at(textIndex));
	}
	
	void MultiTextButton::setIndexFromText(const std::string &s)
	{
		for (size_t i = 0; i < texts.size(); i++)
		{
			if (texts[i] == s)
				setIndex(i);
		}
	}
}
