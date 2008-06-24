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

#ifndef GUICheckList_h
#define GUICheckList_h

#include "GUIList.h"

///A CheckList is bassically like a list, except that each item is either checked off or it isn't

namespace GAGGUI
{
	class CheckList : public List
	{
	public:
		///Constructs a checklist
		CheckList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font);
		
		///Adds an item to the end of the list
		void addItem(const std::string& text, bool checked);
		void clear(void);
	private:
		std::vector<bool> checks;

		///Draws an item on the screen
		virtual void drawItem(int x, int y, size_t element);
	};
};



#endif
