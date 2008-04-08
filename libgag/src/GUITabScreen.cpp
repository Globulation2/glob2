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
		void TabScreen::addWidgetToGroup(Widget* widget, int group_n)
		{
			addWidget(widget);
			groups[group_n].push_back(widget);
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
		}
		
		int TabScreen::addGroup(const std::string& title)
		{
			int group_n=0;
			while(groupButtons.find(group_n) != groupButtons.end())
			{
				group_n+=1;
			}
			
			groupButtons[group_n] = new TextButton(10 + 210 * group_n, 10, 200, 40, ALIGN_SCREEN_CENTERED, ALIGN_SCREEN_CENTERED, "menu", title.c_str(), 0);
			addWidget(groupButtons[group_n]);
			if(groupButtons.size() == 1)
			{
				groupButtons[0]->visible=false;
			}
			else
			{
				for(int i=0; i<groupButtons.size(); ++i)
				{
					groupButtons[i]->visible=true;
				}
			}
			
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
			
			removeWidget(groupButtons[group_n]);
			delete groupButtons[group_n];
			groupButtons.erase(groupButtons.find(group_n));
			
			if(groupButtons.size() == 1)
			{
				groupButtons[0]->visible=false;
			}
			else
			{
				for(int i=0; i<groupButtons.size(); ++i)
				{
					groupButtons[i]->visible=true;
				}
			}
		}
		
		void TabScreen::onAction(Widget *source, Action action, int par1, int par2)
		{
			bool found=false;
			if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
			{
				for(int i=0; i<groupButtons.size(); ++i)
				{
					if(source == groupButtons[i])
					{
						activateGroup(i);
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
		
		void TabScreen::onTimer(Uint32 tick)
		{
			for(std::map<int, TabScreenWindow*>::iterator i = windows.begin(); i!=windows.end();)
			{
				if(!i->second->isStillExecuting())
				{
					std::map<int, TabScreenWindow*>::iterator ni = i++;
					int rc = ni->second->getReturnCode();
					delete ni->second;
					
					if(windows.size() == 0)
					{
						endExecute(rc);
					}
				}
				else
				{
					i->second->onTimer(tick);
					i++;
				}
			}
		}
		
		void TabScreen::onGroupActivated(int group_n)
		{
		
		}
};
