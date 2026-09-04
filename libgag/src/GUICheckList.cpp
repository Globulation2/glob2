// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "GUICheckList.h"
#include "GUIStyle.h"

namespace GAGGUI
{
	CheckList::CheckList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font, bool readOnly)
		: List(x, y, w, h, hAlign, vAlign, font), readOnly(readOnly)
	{
		
	}
	
		
	void CheckList::addItem(const std::string& text, bool checked)
	{
		addText(text);
		checks.push_back(checked);
	}
	
	
	void CheckList::clear(void)
	{
		List::clear();
		checks.clear();
	}
	
	
	bool CheckList::isChecked(int n)
	{
		return checks[n];
	}
	
	
	void CheckList::drawItem(int x, int y, size_t element)
	{
		if(element < getCount())
		{
			int xShift = 20;
			int spriteYShift = (textHeight-16) >> 1;
			Style::style->drawOnOffButton(parent->getSurface(), x, y+spriteYShift, 16, 16, 0, checks[element]);
			parent->getSurface()->drawString(x+xShift, y, fontPtr, (strings[element]).c_str());
		}
	}
	
	
	void CheckList::handleItemClick(size_t element, int mx, int my)
	{
		if(!readOnly)
		{
			int spriteYShift = (textHeight-16) >> 1;
			if(mx >= 0 && mx <= 16 && my>=spriteYShift && my<=((int)textHeight-spriteYShift))
			{
				checks[element] = !checks[element];
			}
		}
	}
}
