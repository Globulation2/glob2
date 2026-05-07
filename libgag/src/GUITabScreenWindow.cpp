// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "GUITabScreenWindow.h"
#include "GUITabScreen.h"

namespace GAGGUI
{
	TabScreenWindow::TabScreenWindow(TabScreen* parent, const std::string& tabName)
		: parent(parent), tabNumber(0), returnCode(0), isExecuting(true)
	{
		activated=false;
		tabNumber = parent->addGroup(tabName);
		parent->setTabScreenWindowToGroup(this, tabNumber);
	}
	
	
	TabScreenWindow::~TabScreenWindow()
	{
		parent->removeGroup(tabNumber);
	}

	void TabScreenWindow::addWidget(Widget* widget)
	{
		parent->addWidgetToGroup(widget, tabNumber);
	}
		
	void TabScreenWindow::removeWidget(Widget* widget)
	{
		parent->removeWidgetFromGroup(widget, tabNumber);
	}
		
	void TabScreenWindow::onAction(Widget *source, Action action, int par1, int par2)
	{
		
	}

	void TabScreenWindow::onTimer(Uint32 tick)
	{
		
	}
		
	int TabScreenWindow::getReturnCode()
	{
		return returnCode;
	}
	
	bool TabScreenWindow::isStillExecuting()
	{
		return isExecuting;
	}
	
	void TabScreenWindow::internalInit()
	{
		parent->internalInit(tabNumber);
	}
	
	int TabScreenWindow::getTabNumber()
	{
		return tabNumber;
	}
	
	bool TabScreenWindow::isActivated()
	{
		return activated;
	}
	
	void TabScreenWindow::onActivated()
	{
		
	}
	
	void TabScreenWindow::endExecute(int nreturnCode)
	{
		isExecuting = false;
		returnCode = nreturnCode;
	}
	
	void TabScreenWindow::setActivated(bool nactivated)
	{
		activated=nactivated;
	}
};


