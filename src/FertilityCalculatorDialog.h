// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007-2008 Bradley Arsenault

#ifndef FertilityCalculatorDialog_h
#define FertilityCalculatorDialog_h

#include "GUIBase.h"
#include "FertilityCalculatorThread.h"
#include <thread>

class Map;
namespace GAGGUI
{
	class Text;
	class ProgressBar;
}
namespace GAGCore
{
	class DrawableSurface;
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
private:
	///This proccesses an incoming event from the fertility calculator thread
	void proccessIncoming(GAGCore::DrawableSurface *background);
	
	Map& map;
	GAGCore::GraphicContext *parentCtx;
	
	GAGGUI::Text* percentDone;
	GAGGUI::ProgressBar* progress;
	
	FertilityCalculatorThread thread;
	std::thread computeThread;
	std::queue<std::shared_ptr<FertilityCalculatorThreadMessage> > incoming;
	std::recursive_mutex incomingMutex;
};


#endif
