// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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
		
		///Calls internal init on all sub codes
		void internalInit();
		
		///Returns the tab number
		int getTabNumber();
		
		///Returns true if this window is activated
		bool isActivated();
		
		virtual void onActivated();
	protected:
		friend class TabScreen;
	
		///Ends the execution of the TabScreenWindow with the given end value
		void endExecute(int returnCode);
		
		///Sets whether this window is acticated or not
		void setActivated(bool activated);
		
		///This is the parent of this tab screen window
		TabScreen* parent;
	private:
		int tabNumber;
		int returnCode;
		bool isExecuting;
		bool activated;
	};
};
