// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include "GUIList.h"

///A CheckList is bassically like a list, except that each item is either checked off or it isn't

namespace GAGGUI
{
	class CheckList : public List
	{
	public:
		///Constructs a checklist
		CheckList(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font, bool readOnly=true);
		
		///Adds an item to the end of the list
		void addItem(const std::string& text, bool checked);
		void clear(void);
		///This checks whether an item is checked or not
		bool isChecked(int n);
	private:
		bool readOnly;
	
		std::vector<bool> checks;

		///Draws an item on the screen
		virtual void drawItem(int x, int y, size_t element);
		///Handles an item click
		virtual void handleItemClick(size_t element, int mx, int my);
	};
};
