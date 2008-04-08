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

#ifndef GUITabScreenWindow_h
#define GUITabScreenWindow_h

#include "GUIBase.h"

namespace GAGGUI
{
	class TabScreen;
};

namespace GAGGUI
{
	///A TabScreenWindow is like a Screen, except that its meant to operate as a tab
	///in a TabScreen, rather than a whole screen by itself. A TabScreenWindow bassically
	///recieves events from the widgets in its TabScreen group, and has a return code
	class TabScreenWindow
	{
	public:
		///Constructs a TabScreenWindow
		TabScreenWindow(TabScreen* parent, const std::string& tabName);
		~TabScreenWindow();
	
		///Adds a widget
		void addWidget(Widget* widget);
		
		///Removes a widget
		void removeWidget(Widget* widget);
		
		///Handles an action from a Widget, overridden in sub classes
		virtual void onAction(Widget *source, Action action, int par1, int par2);
		
		///Handles on timer
		virtual void onTimer(Uint32 tick);
		
		///Returns the return code
		int getReturnCode();
		
		///True if this TabScreenWindow is still executing, false otherwise
		bool isStillExecuting();
	protected:
		friend class TabScreen;
	
		///Ends the execution of the TabScreenWindow with the given end value
		void endExecute(int returnCode);
	private:
		TabScreen* parent;
		int tabNumber;
		int returnCode;
		bool isExecuting;
	};
};

#endif
