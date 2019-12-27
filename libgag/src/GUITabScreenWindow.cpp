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


