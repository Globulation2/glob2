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

#include "GUITabScreen.h"

#include <algorithm>
#include "GUIButton.h"
#include "GUITabScreenWindow.h"

namespace GAGGUI
{
		TabScreen::TabScreen()
		{
			activated=0;
			returnCode=0;
		}
		
		void TabScreen::addWidgetToGroup(Widget* widget, int group_n)
		{
			addWidget(widget);
			groups[group_n].push_back(widget);
			if(group_n == activated)
				widget->visible=true;
			else
				widget->visible=false;
			if(groups.size()>1)
			{
				widget->internalInit();
			}
		}
		
		void TabScreen::removeWidgetFromGroup(Widget* widget, int group_n)
		{
			removeWidget(widget);
			std::vector<Widget*>::iterator i = std::find(groups[group_n].begin(), groups[group_n].end(), widget);
			if(i!=groups[group_n].end())
				groups[group_n].erase(i);
		}
		
		void TabScreen::setTabScreenWindowToGroup(TabScreenWindow* window, int group_n)
		{
			windows[group_n] = window;
			if(group_n==activated)
			{
				window->setActivated(true);
			}
			else
			{
				window->setActivated(false);
			}
			returnCodes[group_n] = -1;
		}
		
		void TabScreen::removeTabScreenWindowFromGroup(TabScreenWindow* window, int group_n)
		{
			if(windows.find(group_n) != windows.end())
			{
				windows.erase(windows.find(group_n));
			}
		}
		
		void TabScreen::activateGroup(int group_n)
		{
			for(std::map<int, std::vector<Widget*> >::iterator i = groups.begin(); i!=groups.end(); ++i)
			{
				if(i->first == group_n)
				{
					for(std::vector<Widget*>::iterator j = i->second.begin(); j!=i->second.end(); ++j)
					{
						(*j)->visible=true;
					}
				}
				else
				{
					for(std::vector<Widget*>::iterator j = i->second.begin(); j!=i->second.end(); ++j)
					{
						(*j)->visible=false;
					}
				}
			}
			onGroupActivated(group_n);
			activated = group_n;
			for(std::map<int, TabScreenWindow*>::iterator i = windows.begin(); i!=windows.end(); ++i)
			{
				if(i->first == group_n)
					i->second->setActivated(true);
				else
					i->second->setActivated(false);
			}
			if(windows.find(group_n) != windows.end())
				windows[group_n]->onActivated();
		}
		
		int TabScreen::addGroup(const std::string& title)
		{
			int group_n=returnCode;
			returnCode+=1;
			
			groupButtons[group_n] = new TextButton(0, 0, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", title.c_str(), 0);
			addWidget(groupButtons[group_n]);
			groupButtons[group_n]->internalInit();
			if(groupButtons.size() == 1)
			{
				groupButtons.begin()->second->visible=false;
			}
			else
			{
				for(std::map<int, Widget*>::iterator i=groupButtons.begin(); i!=groupButtons.end(); ++i)
				{
					i->second->visible=true;
				}
			}
			repositionPanelButtons();
			
			return group_n;
		}

		void TabScreen::removeGroup(int group_n)
		{
			for(std::vector<Widget*>::iterator j = groups[group_n].begin(); j!=groups[group_n].end(); ++j)
			{
				removeWidget(*j);
				delete *j;
			}
			groups.erase(groups.find(group_n));
			
			if(windows.find(group_n) != windows.end())
				windows.erase(windows.find(group_n));
			
			if(groupButtons[group_n])
				removeWidget(groupButtons[group_n]);
			delete groupButtons[group_n];
			groupButtons.erase(groupButtons.find(group_n));
			
			if(groupButtons.size() == 1)
			{
				groupButtons.begin()->second->visible=false;
			}
			else
			{
				for(std::map<int, Widget*>::iterator i=groupButtons.begin(); i!=groupButtons.end(); ++i)
				{
					i->second->visible=true;
				}
			}
			repositionPanelButtons();
		}
		
		void TabScreen::onAction(Widget *source, Action action, int par1, int par2)
		{
			bool found=false;
			if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
			{
				for(std::map<int, Widget*>::iterator i=groupButtons.begin(); i!=groupButtons.end(); ++i)
				{
					if(source == i->second)
					{
						activateGroup(i->first);
						found = true;
						break;
					}
				}
			}
			if(!found)
			{
				for(std::map<int, std::vector<Widget*> >::iterator i = groups.begin(); i!=groups.end(); ++i)
				{
					std::vector<Widget*>::iterator j = std::find(i->second.begin(), i->second.end(), source);
					if(j!=i->second.end())
					{
						if(windows.find(i->first) != windows.end())
						{
							windows[i->first]->onAction(source, action, par1, par2);
							break;
						}
					}
				}
			}
		}
		
		
		void TabScreen::onGroupActivated(int group_n)
		{
		
		}
		
		void TabScreen::onTimer(Uint32 tick)
		{
			int last = -1;
			for(std::map<int, TabScreenWindow*>::iterator i = windows.begin(); i!=windows.end();)
			{
				if(!i->second->isStillExecuting())
				{
					std::map<int, TabScreenWindow*>::iterator ni = i;
					i++;
					int rc = ni->second->getReturnCode();
					returnCodes[ni->first] = rc;
					int n = ni->first;
					removeGroup(ni->first);
					if(windows.size() == 0)
					{
						endExecute(rc);
					}
					else if(n == activated)
					{
						if(last!=-1)
						{
							activateGroup(last);
						}
					}
				}
				else
				{
					last=i->first;
					i->second->onTimer(tick);
					i++;
				}
			}
		}
		
		int TabScreen::getReturnCode(int group_n)
		{
			return returnCodes[group_n];
		}
		
		void TabScreen::internalInit(int group_n)
		{
			for(std::vector<Widget*>::iterator j = groups[group_n].begin(); j!=groups[group_n].end(); ++j)
			{
				(*j)->internalInit();
			}
		}
		
		void TabScreen::repositionPanelButtons()
		{
			int x=0;
			for(std::map<int, Widget*>::iterator i=groupButtons.begin(); i!=groupButtons.end(); ++i)
			{
				static_cast<TextButton*>(i->second)->setScreenPosition(10 + 210 * x, 10);
				x++;
			}
		}
};
