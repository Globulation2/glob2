/*
  Copyright (C) 2008 Bradley Arsenault

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
