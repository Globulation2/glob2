/*
  Copyright (C) 2007-2008 Bradley Arsenault

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

#ifndef FertilityCalculatorDialog_h
#define FertilityCalculatorDialog_h

#include "GUIBase.h"
#include "FertilityCalculatorThread.h"

class Map;
namespace GAGGUI
{
	class Text;
}

///This dialog shows progress of the fertility computation
class FertilityCalculatorDialog:public GAGGUI::OverlayScreen
{
public:
	FertilityCalculatorDialog(GAGCore::GraphicContext *parentCtx, Map& map);
	virtual ~FertilityCalculatorDialog() { }
	virtual void onAction(GAGGUI::Widget *source, GAGGUI::Action action, int par1, int par2);
	
	///This screen is modal, this executes it
	void execute();
	
	///This proccesses an incoming event from the fertility calculator thread
	void proccessIncoming();
private:
	Map& map;
	GAGCore::GraphicContext *parentCtx;
	
	GAGGUI::Text* percentDone;
	
	FertilityCalculatorThread thread;
	std::queue<boost::shared_ptr<FertilityCalculatorThreadMessage> > incoming;
	boost::recursive_mutex incomingMutex;
};


#endif
