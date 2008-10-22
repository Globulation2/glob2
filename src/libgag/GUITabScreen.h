/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef GUITabScreen_h
#define GUITabScreen_h

#include "GUIBase.h"
#include <map>
#include <vector>

namespace GAGGUI
{
	class TabScreenWindow;
};

namespace GAGGUI
{
	///This window has multiple selectable screens
	class TabScreen : public Screen
	{
	public:
		TabScreen(bool fullScreen, bool longerButtons=false);
	
		///This adds a widget to a particular group. This calls add widget automatically
		void addWidgetToGroup(Widget* widget, int group_n);
		
		///This removes a widget from a particular group. This calls remove widget automatically
		void removeWidgetFromGroup(Widget* widget, int group_n);
		
		///This sets a particular TabScreenWindow to a group_n. TabScreenWindows recieve events from the widgets
		///in their group.
		void setTabScreenWindowToGroup(TabScreenWindow* window, int group_n);
		
		///This removes a particular TabScreenWindow from a group_n
		void removeTabScreenWindowFromGroup(TabScreenWindow* window, int group_n);
		
		///This activates a particular group
		void activateGroup(int group_n);
		
		///This sets the title of the group. This must be done before any widgets are added to it, returning the group number
		int addGroup(const std::string& title);

		///This sets the title of of a given group
		void modifyTitle(int group, const std::string& title);

		///This removes a title for a group, removing the group and any widgets in it
		void removeGroup(int group_n);
		
		///Recieves the action. Child classes should call this one first
		void onAction(Widget *source, Action action, int par1, int par2);
		
		///This is called when a group has been activated
		virtual void onGroupActivated(int group_n);
		
		///Should be called by subclasses. Will go through each TabScreenWindow, called its onTimer,
		///remove it if it isn't executing, and the whole thing will close if there are no more TabScreenWindows.
		///The return code is the same as the one for the most recently closed window
		virtual void onTimer(Uint32 tick);
		
		///Returns the code that the specific tab screen group number ended with, and -1 if that groups
		///tab screen is still executing
		int getReturnCode(int group_n);
		
		///This causes the entire tab screen to end
		void completeEndExecute(int return_code);
	private:
		friend class TabScreenWindow;
	
		///Calls internal init on all sub widgets
		void internalInit(int group_n);
		
		///Re-orders all panel buttons
		void repositionPanelButtons();
	
		std::map<int, std::vector<Widget*> > groups;
		std::map<int, TabScreenWindow*> windows;
		std::map<int, Widget*> groupButtons;
		std::map<int, int> returnCodes;
		int activated;
		int returnCode;
		bool fullScreen;
		bool longerButtons;
	};
};

#endif
