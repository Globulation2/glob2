// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
		SDL_Keycode typedUnicode=event->key.keysym.sym;
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
		this->font=font;
		this->text=text;
		fontPtr=NULL;
	}

	TextButton::TextButton(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string text, int returnCode, const std::string& tooltip, const std::string &tooltipFont, Uint16 unicode) :
		Button(x, y, w, h, hAlign, vAlign, returnCode, tooltip, tooltipFont, unicode)
	{
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
	
    std::vector<std::string> TextButton::wrappedLines(int width) const
    {
        if (fontPtr->getStringWidth(text) <= width) return {text};
        std::vector<std::string> lines;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = start, lastSpace = std::string::npos;
            while (end < text.size()) {
                size_t next = end + 1;
                while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80) ++next;
                if (end > start && fontPtr->getStringWidth(text.substr(start, next - start)) > width) break;
                if (text[end] == ' ') lastSpace = end;
                end = next;
            }
            if (end < text.size() && lastSpace != std::string::npos && lastSpace > start) end = lastSpace;
            lines.push_back(text.substr(start, end - start));
            start = end;
            while (start < text.size() && text[start] == ' ') ++start;
        }
        return lines;
    }

    int TextButton::wrappedHeight(int width) const
    {
        return int(wrappedLines(std::max(1, width - 24)).size()) * fontPtr->getStringHeight(text) + 16;
    }

    void TextButton::paintResponsive()
    {
        int x, y, w, h;
        getScreenPos(&x, &y, &w, &h);
        auto* surface = parent->getSurface();
        const auto lines = wrappedLines(std::max(1, w - 24));
        const int lineHeight = fontPtr->getStringHeight(text);
        // The shipped button artwork has a 40-unit cap height.
        Style::style->drawTextButtonBackground(surface, x, y + (h - 40) / 2, w, 40, getNextHighlightValue());
        if (lines.size() > 1) surface->drawFilledRect(x + 12, y + 4, w - 24, h - 8, GAGCore::Color(45, 62, 24, 230));
        int lineY = y + (h - int(lines.size()) * lineHeight) / 2;
        for (const auto& line : lines) {
            surface->drawString(x + (w - fontPtr->getStringWidth(line)) / 2, lineY, fontPtr, line);
            lineY += lineHeight;
        }
    }

    int TextButton::textWidth() const { return fontPtr ? fontPtr->getStringWidth(text) : 0; }

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
	
}
