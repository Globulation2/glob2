/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "CreditScreen.h"
#include "GlobalContainer.h"
#include <iostream>
#include <string>
#include <GUIButton.h>
#include <GUIText.h>
using namespace GAGGUI;
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <BinaryStream.h>
using namespace GAGCore;
// using namespace std;


// New class for an auto-scrolling credit screen.

// INCLUDE part
// #ifndef __SCROLLINGTEXT_H
// #define __SCROLLINGTEXT_H
// 
// #include "GUIBase.h"
// #include <string>

	class ScrollingText:public RectangularWidget
	{
	protected:
		std::string filename;
		std::string font;
		std::vector<std::string> text;
		std::vector<int> xPos; // Pre-calculated postions for text centering
		int offset;
		int imgid, imgid0;
	
		// cache, recomputed on internalInit
		GAGCore::Font *fontPtr;
	
	public:
		ScrollingText(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *filename);
		virtual ~ScrollingText() { }
		virtual void internalInit(void);
		virtual void paint(void);
		virtual void onTimer(Uint32 tick);
	};
	
// #endif

////////////////////////////////////////////////
////////////////////////////////////////////////


ScrollingText::ScrollingText(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const char *font, const char *filename)
:RectangularWidget()
{
	this->x = x;
	this->y = y;
	this->w = w;
	this->h = h;
	this->hAlignFlag = hAlign;
	this->vAlignFlag = vAlign;
	
	offset = 0;
	imgid = 0;
	imgid0 = 88;
	
	assert(font);
	assert(filename);
	this->font = font;
	this->filename = filename;
	fontPtr = NULL;
	
	// load text
	InputLineStream *inputLineStream = new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (inputLineStream->isEndOfStream())
	{
		std::cerr << "ScrollingText::ScrollingText() : error, can't open file " << filename << std::endl;
	}
	else
	{
		while (!inputLineStream->isEndOfStream())
		{	// This is the nice way to do it
			text.push_back(inputLineStream->readLine());
		}
	}
	delete inputLineStream;
}

void ScrollingText::internalInit(void)
{
	fontPtr = Toolkit::getFont(font.c_str());
	assert(fontPtr);
	int x, y, w, h;	
	getScreenPos(&x, &y, &w, &h);
	offset = -h;
	
	// Measures all the length of all the lines of the file (usefull for centering)
	for (size_t i = 0; i < text.size(); i++)
	{
		std::string &s = text[i];
		if (s.size() && s[0]!='\n')
		{
			std::string::size_type f = s.find('<');
			std::string::size_type l = s.rfind('>');
			// std::string::size_type deco = s.find('*');
			// If we can find a "<" and a ">" in this line
			if ((f != std::string::npos) && (l != std::string::npos))
			{
				// Rips off the e-mail adresses
				s.erase(f, l-f+1);
			}
		}
		xPos.push_back((w-fontPtr->getStringWidth(s.c_str()))>>1);	
	}
}

void ScrollingText::paint()
{
	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	
	assert(parent);
	assert(parent->getSurface());

	int yPos = y;
	imgid = imgid0 + (offset & 0x7);

	for (size_t i = 0; i < text.size(); i++)
	{
		std::string s = text[i]; // s is one line of the thingy

		// If the line exists and is not empty
		if (s.size() && s[0]!='\n')
		{
 			std::string::size_type deco = s.find('*');
			// If we can find a star in this line
			if (deco != std::string::npos)
			{
				int px = 2*h+(offset-yPos)*4;
				int py = yPos-offset;
				
				Sprite *unitSprite=globalContainer->units;
				unitSprite->setBaseColor(128, 128, 128);
				int decX = (unitSprite->getW(imgid)-32)>>1;
				int decY = (unitSprite->getH(imgid)-32)>>1;
				globalContainer->gfx->drawSprite(px-decX, py-decY, unitSprite, imgid);
				
				yPos += 20;
			}
			else
				parent->getSurface()->drawString(xPos[i], yPos-offset, fontPtr, s.c_str());
			yPos += 20;
		}
		else
		{
			yPos += 6;
		}
	}
}

void ScrollingText::onTimer(Uint32 tick)
{
	offset++;
}

/////////////////////////////////////////////////
/////////////////////////////////////////////////


CreditScreen::CreditScreen()
{
	addWidget(new TextButton(20, 20, 100,  40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[quit]"), 0, 27));
	
	addWidget(new ScrollingText(0, 0 , 0, 0, ALIGN_FILL, ALIGN_FILL, "standard", "AUTHORS"));
}

void CreditScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endExecute(par1);
}










