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

#include <GUIList.h>
#include <GUIStyle.h>
#include <functional>
#include <algorithm>
#include <assert.h>
#include <Toolkit.h>
#include <GraphicContext.h>

using namespace GAGCore;

namespace GAGGUI
{
	List::List(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->font=font;
		nth=-1;
		disp=0;
		blockLength=0;
		blockPos=0;
		selectionState = NOTHING_PRESSED;
	}
	
	List::List(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font, const std::string& tooltip, const std::string &tooltipFont)
		: HighlightableWidget(tooltip, tooltipFont)
	{
		this->x=x;
		this->y=y;
		this->w=w;
		this->h=h;
		this->hAlignFlag=hAlign;
		this->vAlignFlag=vAlign;
	
		this->font=font;
		nth=-1;
		disp=0;
		blockLength=0;
		blockPos=0;
		selectionState = NOTHING_PRESSED;
	}
	
	List::~List()
	{
	
	}
	
	void List::clear(void)
	{
		strings.clear();
		nth=-1;
	}
	
	
	void List::onTimer(Uint32 tick)
	{
		unsigned count = (h-4) / textHeight;
		switch (selectionState)
		{
			case UP_ARROW_PRESSED:
			if (disp)
				disp--;
			break;
			/*case UP_ZONE_PRESSED:
			if (disp < count)
				disp = 0;
			else
				disp -= count;
			break;
			case DOWN_ZONE_PRESSED:
			disp = std::min(disp + count, strings.size() - count);
			break;*/
			case DOWN_ARROW_PRESSED:
			disp = std::min(disp + 1, strings.size() - count);
			break;
			default:
			break;
		}
		
	}
	
	void List::onSDLMouseButtonDown(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONDOWN);
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		unsigned count = (h-4) / textHeight;
		unsigned wSel;
		int scrollBarW = Style::style->getStyleMetric(Style::STYLE_METRIC_LIST_SCROLLBAR_WIDTH);
		int scrollBarX = x + w - scrollBarW;
		int scrollBarTopH = Style::style->getStyleMetric(Style::STYLE_METRIC_LIST_SCROLLBAR_TOP_WIDTH);
		int scrollBarBottomH = Style::style->getStyleMetric(Style::STYLE_METRIC_LIST_SCROLLBAR_BOTTOM_WIDTH);
		if (strings.size() > count)
		{
			if (isPtInRect(event->button.x, event->button.y, scrollBarX, y, scrollBarW, scrollBarTopH))
			{
				// we scroll one line up
				selectionState = UP_ARROW_PRESSED;
				if (disp)
					disp--;
			}
			else if (isPtInRect(event->button.x, event->button.y, scrollBarX, y+scrollBarTopH, scrollBarW, blockPos))
			{
				// we one page up
				selectionState = UP_ZONE_PRESSED;
				if (disp < count)
					disp = 0;
				else
					disp -= count;
			}
			else if (isPtInRect(event->button.x, event->button.y, scrollBarX, y+scrollBarTopH+blockPos+blockLength, scrollBarW, h-scrollBarTopH-scrollBarBottomH-blockPos-blockLength))
			{
				// we one page down
				selectionState = DOWN_ZONE_PRESSED;
				disp = std::min(disp + count, strings.size() - count);
			}
			else if (isPtInRect(event->button.x, event->button.y, scrollBarX, y+h-scrollBarBottomH, scrollBarW, scrollBarBottomH))
			{
				// we scroll one line down
				selectionState = DOWN_ARROW_PRESSED;
				disp = std::min(disp + 1, strings.size() - count);
			}
			else if (isPtInRect(event->button.x, event->button.y, scrollBarX, y+scrollBarTopH+blockPos, scrollBarW, blockLength))
			{
				selectionState = HANDLE_PRESSED;
				mouseDragStartDisp = disp;
				mouseDragStartPos = event->button.y;
			}
			wSel = w - scrollBarW;
		}
		else
			wSel = w;

		if (isPtInRect(event->button.x, event->button.y, x, y, wSel, h))
		{
			if (event->button.button == SDL_BUTTON_LEFT)
			{
				int id=event->button.y-y-2;
				id/=textHeight;
				id+=disp;
				if ((id>=0) &&(id<(int)strings.size()))
				{
					if (this->nth != id) 
					{
						nth=id;
						this->selectionChanged();
					}
				}
			}
		}
		if (isPtInRect(event->button.x, event->button.y, x, y, w, h))
		{
			if (event->button.button == 4)
			{
				// we scroll one line up
				if (disp)
				{
					disp--;
				}
			}
			else if (event->button.button == 5)
			{
				// we scroll one line down
				if (disp<strings.size()-count)
				{
					disp++;
				}
			}
		}
	}
	
	void List::onSDLMouseButtonUp(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEBUTTONUP);
		selectionState = NOTHING_PRESSED;
	}
	
	void List::onSDLMouseMotion(SDL_Event *event)
	{
		assert(event->type == SDL_MOUSEMOTION);
		HighlightableWidget::onSDLMouseMotion(event);
		if (selectionState == HANDLE_PRESSED)
		{
			int count = (h-4) / textHeight;
			int newPos = event->motion.y - mouseDragStartPos;
			int newDisp = mouseDragStartDisp + (newPos * (int)(strings.size() - count)) / (int)((h - 43) - blockLength);
			if (newDisp < 0)
				disp = 0;
			else if (newDisp > (int)(strings.size() - count))
				disp = strings.size() - count;
			else
				disp = newDisp;
		}
	}
	
	void List::selectionChanged()
	{
		this->parent->onAction(this, LIST_ELEMENT_SELECTED, this->nth, 0);
	}
	
	void List::internalInit(void)
	{
		fontPtr = Toolkit::getFont(font.c_str());
		assert(fontPtr);
		textHeight = fontPtr->getStringHeight((const char *)NULL);
		assert(textHeight > 0);
	}
	
	void List::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		int scrollBarW = Style::style->getStyleMetric(Style::STYLE_METRIC_LIST_SCROLLBAR_WIDTH);
		int scrollBarX = x + w - scrollBarW;
		int scrollBarTopH = Style::style->getStyleMetric(Style::STYLE_METRIC_LIST_SCROLLBAR_TOP_WIDTH);
		int scrollBarBottomH = Style::style->getStyleMetric(Style::STYLE_METRIC_LIST_SCROLLBAR_BOTTOM_WIDTH);
		int frameTopHeight = Style::style->getStyleMetric(Style::STYLE_METRIC_FRAME_TOP_HEIGHT);
		int frameLeftWidth = Style::style->getStyleMetric(Style::STYLE_METRIC_FRAME_LEFT_WIDTH);
		int frameRightWidth = Style::style->getStyleMetric(Style::STYLE_METRIC_FRAME_RIGHT_WIDTH);
		int frameBottomHeight = Style::style->getStyleMetric(Style::STYLE_METRIC_FRAME_BOTTOM_HEIGHT);
		
		int elementsHeight = h - frameTopHeight - frameBottomHeight;
		int count = elementsHeight / textHeight;
		int elementLength;
		
		if (static_cast<int>(strings.size()) > count)
		{
			// recompute slider informations
			int leftSpace = h - scrollBarTopH - scrollBarBottomH;
			if (leftSpace)
			{
				blockLength = (count * leftSpace) / strings.size();
				blockPos = (disp * (leftSpace - blockLength)) / (strings.size() - count);
				Style::style->drawScrollBar(parent->getSurface(), scrollBarX, y, w, h, blockPos, blockLength);
			}
			else
			{
				// degenerate case, bad gui design
				std::cerr << "List::paint() : your gui is badly designed, I do not have vertical room for slider !" << std::endl;
				blockLength = 0;
				blockPos = 0;
			}
			elementLength = w - scrollBarW - frameLeftWidth - frameRightWidth;
		}
		else
		{
			disp = 0;
			elementLength = w - frameLeftWidth - frameRightWidth;
		}
		
		// draw content
		parent->getSurface()->setClipRect(x + frameLeftWidth, y + frameTopHeight, elementLength, elementsHeight);
		int yPos = y + frameTopHeight;
		int nextSize = textHeight;
		size_t i = 0;
		
		while ((nextSize < elementsHeight) && (i < strings.size()))
		{
			drawItem(x + frameLeftWidth * 2, yPos, static_cast<size_t>(i+disp));
			if (static_cast<int>(i + disp) == nth)
				Style::style->drawFrame(parent->getSurface(), x + frameLeftWidth, yPos, elementLength, textHeight, Color::ALPHA_TRANSPARENT);
			// TODO : colorise selection frame
			nextSize += textHeight;
			i++;
			yPos += textHeight;
		}
		
		parent->getSurface()->setClipRect();
		
		// draw frame
		if (static_cast<int>(strings.size()) > count)
			Style::style->drawFrame(parent->getSurface(), x, y, w - Style::style->getStyleMetric(Style::STYLE_METRIC_LIST_SCROLLBAR_WIDTH), h, getNextHighlightValue());
		else
			Style::style->drawFrame(parent->getSurface(), x, y, w, h, getNextHighlightValue());
	}
	
	void List::drawItem(int x, int y, size_t element)
	{
		parent->getSurface()->drawString(x, y, fontPtr, (strings[element]).c_str());
	}
	
	void List::addText(const std::string &text, size_t pos)
	{
		if (pos < strings.size())
		{
			strings.insert(strings.begin()+pos, text);
		}
		else if (pos==0)
		{
			strings.push_back(text);
		}
	}
	
	void List::addText(const std::string &text)
	{
		strings.push_back(text);
	}
	
	void List::sort(void)
	{
		std::sort(strings.begin(), strings.end());
	}
	
	void List::removeText(size_t pos)
	{
		if (pos < strings.size())
		{
			strings.erase(strings.begin()+pos);
			if (static_cast<int>(pos) < nth)
				nth--;
		}
		int count = (h-4) / textHeight;
		if(disp + count > strings.size())
			disp-=1;
	}
	
	bool List::isText(const std::string &text) const
	{
		for (size_t i=0; i<strings.size(); i++)
		{
			if (strings[i] == text)
				return true;
		}
		return false;
	}
	
	const std::string &List::getText(size_t pos) const
	{
		if (pos < strings.size())
		{
			return strings[pos];
		}
		else
			assert(false);
	}
	
	const std::string &List::get(void) const
	{
		if (nth >= 0)
			return getText(static_cast<size_t>(nth));
		else
			assert(false);
	}
	
	void List::setText(size_t pos, const std::string& text)
	{
		assert(pos < strings.size());
		strings[pos]=text;
	}
	
	size_t List::getCount(void) const
	{
		return strings.size();
	}
	
	int List::getSelectionIndex(void) const
	{
		return nth;
	}
	
	void List::setSelectionIndex(int index)
	{
		if ((index >= -1 ) && (index < static_cast<int>(strings.size())))
			this->nth = index;
	}
	
	void List::centerOnItem(int index)
	{
		int count = (h-4) / textHeight;
		disp = std::max(std::min(index - count/2, int(strings.size() - count)), 0);
	}
}
