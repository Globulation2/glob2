// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <GUITextArea.h>
#include <GUIStyle.h>
#include <Toolkit.h>
#include <GraphicContext.h>
#include <StreamBackend.h>
#include <FileManager.h>
#include <assert.h>
#include <valarray>

using namespace GAGCore;

namespace GAGGUI
{
	TextArea::TextArea(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, bool readOnly, const std::string text, const std::string spritelocation)
	{
		this->x = x;
		this->y = y;
		this->w = w;
		this->h = h;
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;
		
		this->readOnly = readOnly;
		this->sprite = NULL;
		this->spriteWidth = 0;
		// TODO : clean this and store text font
		this->font = Toolkit::getFont(font);
		assert(this->font);
		assert(font.size());
		charHeight = this->font->getStringHeight("");
		assert(charHeight);
		areaHeight = (h-8)/charHeight;
		areaPos=0;
		
		cursorPos = 0;
		cursorPosY = 0;
		cursorScreenPosY = 0;
		
		activated=false;
		
		this->text = text;
		
		if (!spritelocation.empty())
			sprite = Toolkit::getSprite(spritelocation);
		if (sprite)
			spriteWidth = sprite->getW(0);	
	}
	
	TextArea::TextArea(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string &tooltip, const std::string &tooltipFont, bool readOnly, const std::string text, const std::string spritelocation)
		: HighlightableWidget(tooltip, tooltipFont)
	{
		this->x = x;
		this->y = y;
		this->w = w;
		this->h = h;
		this->hAlignFlag = hAlign;
		this->vAlignFlag = vAlign;
		
		this->readOnly = readOnly;
		this->sprite = NULL;
		this->spriteWidth = 0;
		// TODO : clean this and store text font
		this->font = Toolkit::getFont(font);
		assert(this->font);
		assert(font.size());
		charHeight = this->font->getStringHeight("");
		assert(charHeight);
		areaHeight = (h-8)/charHeight;
		areaPos=0;
		
		cursorPos = 0;
		cursorPosY = 0;
		cursorScreenPosY = 0;
		
		activated=false;
		
		this->text = text;
		
		if (!spritelocation.empty())
			sprite = Toolkit::getSprite(spritelocation);
		if (sprite)
			spriteWidth = sprite->getW(0);	
	}
	
	TextArea::~TextArea(void)
	{
		
	}
	
	void TextArea::internalInit(void)
	{
		layout();
	}
	
	void TextArea::paint(void)
	{
		int x, y, w, h;
		getScreenPos(&x, &y, &w, &h);
		
		assert(parent);
		assert(parent->getSurface());
		
		HighlightableWidget::paint();
		
		areaHeight=(h-8)/charHeight;
		parent->getSurface()->setClipRect(x, y, w, h);
		
		for (unsigned i=0;(i<areaHeight)&&((signed)i<(signed)(lines.size()-areaPos));i++)
		{
			size_t row = i+areaPos;
			assert(row<lines.size());
			std::string::size_type len = row+1<lines.size() ? lines[row+1]-lines[row] : std::string::npos;
			std::string substr = text.substr(lines[row], len);
			parent->getSurface()->drawString(x+4+spriteWidth, y+4+(charHeight*i), font, substr.c_str(), w-8-spriteWidth);
			if (sprite && show_image[row] && row<lines_frames.size() && lines_frames[row]>=0)
			{
				parent->getSurface()->drawSprite(x+2, y+4+(charHeight*i), sprite, lines_frames[row]);
			}
		}
	
		if (!readOnly && activated)
		{
			int xPos = x+4+cursorScreenPosY;
			int yPos = y+4+(charHeight*(cursorPosY-areaPos));
			parent->getSurface()->drawLine(xPos, yPos, xPos, yPos + charHeight, Style::style->textColor);
		}
		parent->getSurface()->setClipRect();
	}
	
	void TextArea::compute(void)
	{
		// The only variable which is always valid is cursorPos,
		// so now we recompute cursorPosY from it.
		// But it is guarantied that cursorPosY < lines.size();
		
		assert(cursorPosY >= 0);
		assert(cursorPosY < lines.size());
		assert(cursorPos >= 0);
		assert(cursorPos <= text.length());
		
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
				while (cursorPosY+1>=areaPos+areaHeight)
					areaPos++;
			}
			else
			{
				while (cursorPosY>=areaPos+areaHeight)
					areaPos++;
			}
	
			// TODO : UTF8 clean cursor displacement in text should lead to the removal of this code !!
			// we need to assert cursorPos will point on the beginning of a valid UTF8 char
			unsigned utf8CleanCursorPos = lines[cursorPosY];
			while ((utf8CleanCursorPos < text.length())
				&& (utf8CleanCursorPos + getNextUTF8Char(text[utf8CleanCursorPos]) <= cursorPos))
			{
				assert(utf8CleanCursorPos < text.length());
				utf8CleanCursorPos += getNextUTF8Char(text[utf8CleanCursorPos]);
			}
			cursorPos = utf8CleanCursorPos;
	
			// compute displayable cursor Pos
			unsigned cursorPosX = cursorPos-lines[cursorPosY];
			const std::string &temp = text.substr(lines[cursorPosY], cursorPosX);
			cursorScreenPosY = getStringWidth(temp);
		}
	}
	
	int TextArea::getStringWidth(const std::string &s)
	{
		std::map<std::string, int>::iterator it = stringWidthCache.find(s);
		if (it != stringWidthCache.end())
		{
			return it->second;
		}
		else
		{
			int w = font->getStringWidth(s.c_str());
			stringWidthCache[s] = w;
			return w;
		}
	}
	
	void TextArea::setText(const std::string text)
	{
		this->text = text;
		if (cursorPos>this->text.length())
			cursorPos = this->text.length();
		layout();
		compute();
	}
	
	void TextArea::addText(const std::string text)
	{
		assert(text.length()>=0);
		assert(cursorPos <= this->text.length());
		assert(cursorPos >= 0);
	
		if (text.length()>0)
		{
			if (readOnly)
			{
				this->text += text;
			}
			else
			{
				this->text.insert(cursorPos, text);
				cursorPos += text.length();
			}
			
			layout();
			compute();
		}
	}

	void TextArea::addImage(int frame)
	{
		assert(sprite),
		assert(frame <= sprite->getFrameCount()-1);
		this->frames.push_back(frame);
	}
	
	void TextArea::addNoImage(void)
	{
		this->frames.push_back(-1);
	}

	void TextArea::remText(unsigned pos, unsigned len)
	{
		if (pos < text.length())
		{
			text.erase(pos, len);
			
			layout();
			compute();
		}
	}
	
	void TextArea::addChar(const char c)
	{
		char text[2];
		text[0] = c;
		text[1] = 0;
		addText(text);
	}
	
	bool TextArea::load(const std::string filename)
	{
		StreamBackend *stream = Toolkit::getFileManager()->openInputStreamBackend(filename);
		if (stream->isEndOfStream())
		{
			delete stream;
			return false;
		}
		else
		{
			stream->seekFromEnd(0);
			size_t len = stream->getPosition();
			stream->seekFromStart(0);
			
			std::valarray<char> tempText(len+1);
			stream->read(&tempText[0], len);
			tempText[len] = 0;
			
			text = &tempText[0];
			layout();
			setCursorPos(cursorPos);
			
			delete stream;
			return true;
		}
	}
	
	bool TextArea::save(const std::string filename)
	{
		StreamBackend *stream = Toolkit::getFileManager()->openOutputStreamBackend(filename);
		if (stream->isEndOfStream())
		{
			delete stream;
			return false;
		}
		else
		{
			stream->write(text.c_str(), text.length());
			delete stream;
			return true;
		}
	}
}
