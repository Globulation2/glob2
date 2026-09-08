// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "CreditScreen.h"
#include "GlobalContainer.h"
#include "render/UnitAnimation.h"
#include <iostream>
#include <string>
#include <GUIButton.h>
using namespace GAGGUI;
#include <Toolkit.h>
#include <StringTable.h>
#include <Stream.h>
#include <FileManager.h>
using namespace GAGCore;


// New class for an auto-scrolling credit screen.

// Credits decorations use worker walking direction 3.
static constexpr int kWorkerWalkFrameBase = unitAnimationFrame(64, 3, 0);
static constexpr int kWorkerWalkFrameCount = UNIT_ANIMATION_FRAMES_PER_DIRECTION;

class ScrollingText:public RectangularWidget
{
protected:
	std::string filename;
	std::string font;
	std::vector<std::string> text;
	std::vector<int> xPos; // Pre-calculated positions for text centering
	int offset;
	// whether the units sprite contains the worker walk frames; when false,
	// the '*' decoration is skipped instead of indexing the sprite out of range
	bool unitFramesAvailable;

	// cache, recomputed on internalInit
	GAGCore::Font *fontPtr;

public:
	ScrollingText(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string filename);
	virtual ~ScrollingText() { }
	virtual void internalInit(void);
	virtual void paint(void);
	virtual void onTimer(Uint32 tick);
};

////////////////////////////////////////////////
////////////////////////////////////////////////


ScrollingText::ScrollingText(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string font, const std::string filename)
:RectangularWidget()
{
	this->x = x;
	this->y = y;
	this->w = w;
	this->h = h;
	this->hAlignFlag = hAlign;
	this->vAlignFlag = vAlign;
	
	offset = 0;
	unitFramesAvailable = false;

	assert(font.size());
	assert(filename.size());
	this->font = font;
	this->filename = filename;
	fontPtr = NULL;
	
	// load text; InputLineStream owns the backend and deletes it in its
	// destructor, so a stack object releases the file handle even if
	// readLine() throws
	InputLineStream inputLineStream(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (inputLineStream.isEndOfStream())
	{
		std::cerr << "ScrollingText::ScrollingText() : error, can't open file " << filename << std::endl;
	}
	else
	{
		while (!inputLineStream.isEndOfStream())
		{	// This is the nice way to do it
			text.push_back(inputLineStream.readLine());
		}
	}
}

void ScrollingText::internalInit(void)
{
	fontPtr = Toolkit::getFont(font.c_str());
	assert(fontPtr);

	unitFramesAvailable = globalContainer->units
		&& (globalContainer->units->getFrameCount() >= kWorkerWalkFrameBase + kWorkerWalkFrameCount);
	if (!unitFramesAvailable)
		std::cerr << "ScrollingText::internalInit() : warning, units sprite lacks worker walk frames "
			<< kWorkerWalkFrameBase << ".." << (kWorkerWalkFrameBase + kWorkerWalkFrameCount - 1)
			<< ", credits decoration disabled" << std::endl;

	int x, y, w, h;
	getScreenPos(&x, &y, &w, &h);
	offset = -h + 25;
	
	// Measures all the length of all the lines of the file (useful for centering)
	for (size_t i = 0; i < text.size(); i++)
	{
		std::string &s = text[i];
		if (s.size() && s[0]!='\n')
		{
			std::string::size_type f = s.find('<');
			std::string::size_type l = s.rfind('>');
			// If we can find a "<" and a ">" in this line
			if ((f != std::string::npos) && (l != std::string::npos))
			{
				// Rips off the e-mail addresses
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
	// Mask negative scroll offsets and preserve the eight-scroll-step cycle.
	const int imgid = kWorkerWalkFrameBase + ((offset & 7) * UNIT_ANIMATION_FRAME_MULTIPLIER);

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
				if (unitFramesAvailable)
				{
					int px = 2*h+(offset-yPos)*4;
					int py = yPos-offset;

					Sprite *unitSprite=globalContainer->units;
					unitSprite->setBaseColor(128, 128, 128);
					int decX = (unitSprite->getW(imgid)-32)>>1;
					int decY = (unitSprite->getH(imgid)-32)>>1;
					globalContainer->gfx->drawSprite(px-decX, py-decY, unitSprite, imgid);
				}

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
	addWidget(new TextButton(20, 20, 100,  40, ALIGN_RIGHT, ALIGN_BOTTOM, "menu", Toolkit::getStringTable()->getString("[Back]"), 0, 27));
	
	addWidget(new ScrollingText(0, 0 , 0, 0, ALIGN_FILL, ALIGN_FILL, "standard", "data/authors.txt"));
}

void CreditScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endExecute(par1);
}
