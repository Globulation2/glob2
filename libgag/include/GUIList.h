// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "GUIBase.h"
#include <algorithm>
#include <optional>
#include <vector>
#include <string>

namespace GAGCore
{
	class Font;
}

namespace GAGGUI
{
	//! A widget that display a list of strings.
	class List: public HighlightableWidget
	{
	protected:
		//! the name of the used font
		std::string font;
		//! the list of strings
		std::vector<std::string> strings;
		//! the current selected entry. -1 means no selection
		Sint32 nth;
		//! index of the first entry to be shown, used for scrolling
		size_t disp;
	
		//! Cached variables, do not serialise, reconstructed on paint() call
		//! Length of the scroll box, this is a cache
		unsigned blockLength, blockPos, textHeight;
		//! Pointer to font, this is a cache
		GAGCore::Font *fontPtr;
		
		//! Possible states of selection inside this widget
		enum SelectionState
		{
			NOTHING_PRESSED = 0,
			UP_ARROW_PRESSED,
			UP_ZONE_PRESSED,
			DOWN_ZONE_PRESSED,
			DOWN_ARROW_PRESSED,
			HANDLE_PRESSED
		} selectionState; //!< state of selection inside this widget
		//! Mouse position when handle dragging started
		int mouseDragStartPos;
		//! Initial displacement when handle dragging started
		int mouseDragStartDisp;
	
	public:
		//! Creator
		List() { fontPtr = NULL; }
		//! Creator, with arguments. x, y, w, h are the positional information. hAlign and vAlign the layouting flags. font the name of the font to use
		List(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font);
		//! Creator with tooltip
		List(const std::string& tooltip, const std::string& tooltipFont) : HighlightableWidget(tooltip, tooltipFont) { fontPtr = NULL; }
		//! Creator with tooltip, with arguments. x, y, w, h are the positional information. hAlign and vAlign the layouting flags. font the name of the font to use
		List(int x, int y, int w, int h, Uint32 hAlign, Uint32 vAlign, const std::string &font, const std::string& tooltip, const std::string &tooltipFont);
		//! Destructor
		virtual ~List();
	
		virtual void onTimer(Uint32 tick);
		virtual void internalInit(void);
		virtual void paint(void);
	
		//! Add text to pos in the list
		void addText(const std::string &text, size_t pos);
		//! Add text to the end of the list
		void addText(const std::string &text);
		//! Remove text at pos in the list
		void removeText(size_t pos);
		//! Returns true if text is in the list
		bool isText(const std::string &text) const;
		//! Removes all the content of the list
		void clear(void);
		//! Returns the value of pos. If pos is out of bounds, returns ""
		const std::string &getText(size_t pos) const;
		//! Returns the value of the current selection. If no selection (mtf == -1), returns "
		const std::string &get(void) const;
		//! Changes the text at pos. Asserts pos is in range.
		void setText(size_t pos, const std::string& text);
		//! Returns the number of entries in the list
		size_t getCount(void) const;
		//! Sorts the list (override it if you don't like it)
		virtual void sort(void);
	
		//! Called when selection changes (default: signal parent)
		virtual void selectionChanged();
	
		//! Return the index of the current selection. Returns -1 if no selection
		int getSelectionIndex(void) const;
		//! Return the current selection as an optional index. std::nullopt if no selection.
		std::optional<size_t> selection(void) const;
		//! Set the index of the current selection. Set -1 for no selection
		void setSelectionIndex(int index);
		//! Set or clear the current selection. std::nullopt clears it; an
		//! out-of-range index is ignored (same policy as setSelectionIndex).
		void setSelection(std::optional<size_t> index);
		//! The selection to restore after entries were removed, given the
		//! previously selected index and the list's new entry count: nothing
		//! if the list is now empty, otherwise previousIndex clamped to the
		//! new last entry. Pure helper — safe against the size_t underflow
		//! of the naive `min(previousIndex, newCount - 1)` when newCount is 0.
		static std::optional<size_t> selectionAfterRemoval(size_t previousIndex, size_t newCount)
		{
			if (newCount == 0)
				return std::nullopt;
			return std::min(previousIndex, newCount - 1);
		}
		
		//! Scrolls the List to be centered on item
		void centerOnItem(int index);
		
	protected:
		virtual void onSDLMouseButtonDown(SDL_Event *event);
		virtual void onSDLMouseButtonUp(SDL_Event *event);
		virtual void onSDLMouseMotion(SDL_Event *event);
		virtual void onSDLMouseWheel(SDL_Event *event);
		//! Draw an item of the list, called by paint
		virtual void drawItem(int x, int y, size_t element);
		//! Handles an item being clicked, with mx and my being relative to the corner of the item.
		//! This is called whether or not the element was already selected
		virtual void handleItemClick(size_t element, int mx, int my);
	};
}

